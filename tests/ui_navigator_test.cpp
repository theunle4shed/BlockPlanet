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

    screen(dir, "keys",
           "<div data-text='typed'/>"
           "<div class='item' id='top' data-action='back'/>"
           "<div data-columns='3' data-list='keyboard'/>"
           "<div class='item' id='done' data-action='command:done'/>");

    screen(dir, "slide",
           "<div class='item value slider' id='vol' data-setting='vol' data-options='0:0%,50:50%,100:100%'>"
           "Volume<span class='value'/><div class='bar'><div class='fill'/></div></div>");

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

        // Grids: Left and Right move along, Up and Down move a row and
        // leave the grid at its edges. Text fields show set_text.
        std::vector<ListItem> keys;
        for (const char* key : {"a", "b", "c", "d", "e"}) keys.push_back({key, key, "", std::string("command:") + key, true});
        nav.set_list("keyboard", keys);
        nav.set_text("typed", "x<y");
        assert(nav.open("keys") && nav.focused_item() == "top");
        assert(context->GetDocument(context->GetNumDocuments() - 1)->QuerySelector("[data-text]")->GetInnerRML() ==
               "x&lt;y");
        nav.handle(Action::Down);
        assert(nav.focused_item() == "a");
        nav.handle(Action::Down);
        assert(nav.focused_item() == "d");
        nav.handle(Action::Right);
        assert(nav.focused_item() == "e");
        nav.handle(Action::Right);
        assert(nav.focused_item() == "d");  // wraps around the row
        nav.handle(Action::Right);
        nav.handle(Action::Up);
        assert(nav.focused_item() == "b");
        nav.handle(Action::Up);
        assert(nav.focused_item() == "top");
        nav.handle(Action::Up);
        assert(nav.focused_item() == "done");
        nav.handle(Action::Up);
        assert(nav.focused_item() == "e");  // into the grid from below
        nav.handle(Action::Down);
        assert(nav.focused_item() == "done");
        nav.handle(Action::Up);    // e
        nav.handle(Action::Up);    // b
        nav.handle(Action::Right); // c
        nav.handle(Action::Down);  // row 1 is shorter, so its last item
        assert(nav.focused_item() == "e");

        // Wide items and wrapping: a 4-column grid over a bottom row of
        // [x][ wide ][y]; Up from the top row goes to the bottom row, and
        // the column is kept across the wide item.
        std::vector<ListItem> wide;
        for (const char* key : {"k0", "k1", "k2", "k3"}) wide.push_back({key, key, "", "command:k", true});
        wide.push_back({"x", "x", "", "command:x", true});
        wide.push_back({"wide", "wide", "", "command:w", true, 2});
        wide.push_back({"y", "y", "", "command:y", true});
        nav.set_list("keyboard", wide);
        screen(dir, "wrap", "<div data-columns='4' data-wrap='' data-list='keyboard'/>");
        assert(nav.show("wrap") && nav.focused_item() == "k0");
        nav.handle(Action::Right);
        nav.handle(Action::Right);
        nav.handle(Action::Up);
        assert(nav.focused_item() == "wide");  // k2 is over the wide item
        nav.handle(Action::Down);
        assert(nav.focused_item() == "k2");    // back to the same column
        nav.handle(Action::Left);
        nav.handle(Action::Down);
        assert(nav.focused_item() == "wide");
        nav.handle(Action::Up);
        assert(nav.focused_item() == "k1");
        nav.handle(Action::Left);
        nav.handle(Action::Left);
        assert(nav.focused_item() == "k3");    // wraps along the row
        nav.handle(Action::Down);
        assert(nav.focused_item() == "y");
        nav.handle(Action::Right);
        assert(nav.focused_item() == "x");
        // The controller class follows set_pad, on open and new screens.
        auto top_document = [&] { return context->GetDocument(context->GetNumDocuments() - 1); };
        assert(!top_document()->IsClassSet("pad"));
        nav.set_pad(true);
        assert(top_document()->IsClassSet("pad"));
        nav.handle(Action::Back);
        assert(nav.show("solo") && top_document()->IsClassSet("pad"));
        nav.set_pad(false);
        assert(!top_document()->IsClassSet("pad"));
        nav.handle(Action::Back);
        // Sliders stop at their ends and fill their bar to the value.
        nav.set_setting("vol", "50");
        assert(nav.show("slide"));
        auto fill_width = [&] {
            Rml::ElementList fills;
            context->GetDocument(context->GetNumDocuments() - 1)->GetElementsByClassName(fills, "fill");
            return fills.front()->GetLocalProperty("width")->ToString();
        };
        assert(fill_width().rfind("50", 0) == 0);
        nav.handle(Action::Right);
        assert(nav.take_command() == std::optional<std::string>("setting:vol=100") && fill_width().rfind("100", 0) == 0);
        nav.handle(Action::Right);  // already at the end
        assert(!nav.take_command());
        nav.handle(Action::Left);
        nav.handle(Action::Left);
        nav.handle(Action::Left);
        assert(nav.take_command() == std::optional<std::string>("setting:vol=50") &&
               nav.take_command() == std::optional<std::string>("setting:vol=0") && !nav.take_command());
        nav.handle(Action::Back);
        nav.set_text("typed", "z");
        assert(context->GetDocument(context->GetNumDocuments() - 1)->QuerySelector("[data-text]")->GetInnerRML() == "z");
        nav.close();
    }
    Rml::Shutdown();
    std::filesystem::remove_all(dir);
    return 0;
}
