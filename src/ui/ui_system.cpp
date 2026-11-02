#include "ui_system.h"

#include "ui_renderer.h"

#include "rhi/rt64_render_hooks.h"

#include <RmlUi/Core.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace blockplanet::ui {
namespace {

// Menus are laid out for a 1080-pixel-high screen and scaled to the window.
constexpr float design_height = 1080.0f;

class System final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type <= Rml::Log::LT_WARNING)
            std::fprintf(stderr, "{\"event\":\"ui_log\",\"type\":%d,\"message\":\"%s\"}\n", int(type), message.c_str());
        return true;
    }

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

std::filesystem::path assets_path;
bool test_screen = false;
std::unique_ptr<Renderer> renderer;
std::unique_ptr<System> system_interface;
Rml::Context* context = nullptr;
bool started = false;

std::string asset(const char* name) { return (assets_path / name).string(); }

// Starts RmlUi on the presentation thread, at the first frame.
bool start(std::uint32_t width, std::uint32_t height) {
    started = true;
    system_interface = std::make_unique<System>();
    Rml::SetSystemInterface(system_interface.get());
    Rml::SetRenderInterface(renderer.get());
    if (!Rml::Initialise()) return false;
    for (const char* font : {"fonts/LatoLatin-Regular.ttf", "fonts/LatoLatin-Bold.ttf"}) {
        if (!Rml::LoadFontFace(asset(font))) std::fprintf(stderr, "{\"event\":\"ui_font_missing\",\"font\":\"%s\"}\n", font);
    }
    context = Rml::CreateContext("main", Rml::Vector2i(int(width), int(height)));
    if (context == nullptr) return false;
    context->SetDensityIndependentPixelRatio(float(height) / design_height);
    if (test_screen) {
        if (Rml::ElementDocument* document = context->LoadDocument(asset("test.rml"))) document->Show();
    }
    std::fprintf(stdout, "{\"event\":\"ui_started\",\"width\":%u,\"height\":%u}\n", width, height);
    std::fflush(stdout);
    return true;
}

void hook_init(plume::RenderInterface* render_interface, plume::RenderDevice* device) {
    renderer = std::make_unique<Renderer>(render_interface, device);
}

void hook_draw(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer) {
    if (!renderer || !test_screen) return;  // nothing to show yet
    const std::uint32_t width = framebuffer->getWidth();
    const std::uint32_t height = framebuffer->getHeight();
    renderer->begin_frame(list, framebuffer);
    if (!started && !start(width, height)) context = nullptr;
    if (context != nullptr) {
        if (context->GetDimensions() != Rml::Vector2i(int(width), int(height))) {
            context->SetDimensions(Rml::Vector2i(int(width), int(height)));
            context->SetDensityIndependentPixelRatio(float(height) / design_height);
        }
        context->Update();
        context->Render();
    }
    renderer->end_frame();
}

void hook_deinit() {
    if (started) {
        if (context != nullptr) Rml::RemoveContext("main");
        context = nullptr;
        Rml::Shutdown();
    }
    renderer.reset();
}

} // namespace

void install(const std::filesystem::path& assets) {
    assets_path = assets;
    const char* test = std::getenv("BLOCKPLANET_UI_TEST");
    test_screen = test != nullptr && std::string(test) == "1";
    RT64::SetRenderHooks(hook_init, hook_draw, hook_deinit);
}

} // namespace blockplanet::ui
