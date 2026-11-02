// Navigator logic with RmlUi and a renderer that draws nothing.
#include "ui_navigator.h"

#include <RmlUi/Core.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace blockplanet::ui;

namespace {

class NullRenderer final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 1; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 1; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 1; }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};

class QuietSystem final : public Rml::SystemInterface {
public:
    bool LogMessage(Rml::Log::Type, const Rml::String&) override { return true; }
};

void screen(const std::filesystem::path& dir, const char* name, const char* items) {
    std::ofstream(dir / (std::string(name) + ".rml")) << "<rml><head></head><body>" << items << "</body></rml>";
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto dir = std::filesystem::path(argv[1]) /
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(dir);
    screen(dir, "main",
           "<div class='item' id='solo' data-action='open:solo'/>"
           "<div class='item disabled' id='skipped' data-action='command:never'/>"
           "<div class='item' id='quit' data-action='command:exit'/>"
           "<div class='item' id='close' data-action='close'/>");
    screen(dir, "options",
           "<div class='item value' id='sphere' data-setting='sphere' data-options='extended:Extended,original:Original'>"
           "Sphere<span class='value'/></div>"
           "<div class='list' data-list='chapters'/>"
           "<div class='item' id='back' data-action='back'/>");
    screen(dir, "solo",
           "<div><div class='item' id='story' data-action='command:story'/></div>"
           "<div class='item' id='back' data-action='back'/>");

    NullRenderer renderer;
    QuietSystem system;
    Rml::SetRenderInterface(&renderer);
    Rml::SetSystemInterface(&system);
    assert(Rml::Initialise());
    Rml::Context* context = Rml::CreateContext("test", Rml::Vector2i(1920, 1080));
    {
        Navigator nav(context, dir);
        assert(!nav.is_open() && !nav.open("missing"));
        assert(nav.open("main") && nav.is_open() && nav.current_screen() == "main");
        assert(nav.focused_item() == "solo");
        // Disabled items are skipped; focus wraps around.
        nav.handle(Action::Down);
        assert(nav.focused_item() == "quit");
        nav.handle(Action::Down);
        nav.handle(Action::Down);
        assert(nav.focused_item() == "solo");
        nav.handle(Action::Up);
        assert(nav.focused_item() == "close");
        nav.handle(Action::Down);
        // Opening a screen, nested items, commands, and going back.
        nav.handle(Action::Confirm);
        assert(nav.current_screen() == "solo" && nav.focused_item() == "story");
        nav.handle(Action::Confirm);
        assert(nav.take_command() == std::optional<std::string>("story") && !nav.take_command());
        nav.handle(Action::Down);
        nav.handle(Action::Confirm);  // the Back item
        assert(nav.current_screen() == "main" && nav.focused_item() == "solo");  // focus remembered
        nav.handle(Action::Confirm);
        nav.handle(Action::Back);  // the Back action
        assert(nav.current_screen() == "main");
        nav.handle(Action::Down);
        nav.handle(Action::Confirm);
        assert(nav.take_command() == std::optional<std::string>("exit"));
        // Back from the first screen, or a close item, closes the menus.
        nav.handle(Action::Back);
        assert(!nav.is_open());
        assert(nav.open("main"));
        nav.handle(Action::Up);
        nav.handle(Action::Confirm);
        assert(!nav.is_open());

        // Setting rows show the current value and report changes.
        nav.set_setting("sphere", "extended");
        nav.set_list("chapters", {{"chapter1", "Chapter 1", "completed", "command:chapter:1", true},
                                  {"chapter2", "Chapter 2", "", "command:chapter:2", false},
                                  {"chapter3", "Chapter 3", "next", "command:chapter:3", true}});
        assert(nav.open("options") && nav.focused_item() == "sphere");
        auto shown = [&] {
            Rml::ElementList values;
            context->GetDocument(context->GetNumDocuments() - 1)->GetElementsByClassName(values, "value");
            return values.empty() ? std::string() : values.back()->GetInnerRML();  // the row also has the class
        };
        assert(shown() == "Extended");
        nav.handle(Action::Right);
        assert(shown() == "Original" && nav.take_command() == std::optional<std::string>("setting:sphere=original"));
        nav.handle(Action::Left);
        assert(shown() == "Extended" && nav.take_command() == std::optional<std::string>("setting:sphere=extended"));
        nav.handle(Action::Confirm);  // Confirm also changes a setting row
        assert(nav.take_command() == std::optional<std::string>("setting:sphere=original"));
        nav.set_setting("sphere", "extended");  // the game's value wins
        assert(shown() == "Extended" && nav.focused_item() == "sphere");
        // Filled-in lists: disabled entries are skipped.
        nav.handle(Action::Down);
        assert(nav.focused_item() == "chapter1");
        nav.handle(Action::Down);
        assert(nav.focused_item() == "chapter3");
        nav.handle(Action::Confirm);
        assert(nav.take_command() == std::optional<std::string>("chapter:3"));
        nav.close();

        // Screens shown on top keep the ones below; a list changed while its
        // screen is underneath is refilled (focus kept in range), and names
        // are shown as text.
        assert(nav.open("main") && nav.show("options"));
        nav.handle(Action::Down);
        nav.handle(Action::Down);
        assert(nav.focused_item() == "chapter3");
        assert(nav.show("solo") && nav.current_screen() == "solo");
        nav.set_list("chapters", {{"only", "A <b> & C", "", "command:only", true}});
        nav.handle(Action::Back);
        assert(nav.current_screen() == "options" && nav.focused_item() == "back");  // index 3 clamped to 2
        assert(context->GetDocument(context->GetNumDocuments() - 1)->GetElementById("only")->GetInnerRML() ==
               "A &lt;b&gt; &amp; C");
        nav.handle(Action::Back);
        assert(nav.current_screen() == "main");
        nav.close();
    }
    Rml::Shutdown();
    std::filesystem::remove_all(dir);
    return 0;
}
