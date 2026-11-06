#include "ui_system.h"

#include "ui_navigator.h"
#include "ui_renderer.h"

#include "rhi/rt64_render_hooks.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <variant>

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
std::unique_ptr<Renderer> renderer;
std::unique_ptr<System> system_interface;
Rml::Context* context = nullptr;
std::unique_ptr<Navigator> navigator;
bool started = false;
bool start_failed = false;

// Requests from other threads, applied on the presentation thread.
struct OpenRequest {
    std::string screen;
    bool on_top = false;  // keep the open screens (Back returns to them)
};
struct CloseRequest {};
struct SettingRequest {
    std::string name;
    std::string value;
};
struct ListRequest {
    std::string name;
    std::vector<ListItem> items;
};
struct TextRequest {
    std::string name;
    std::string text;
};
struct PadRequest {
    bool pad;
};
using Request = std::variant<OpenRequest, CloseRequest, Action, SettingRequest, ListRequest, TextRequest, PadRequest>;
std::mutex requests_mutex;
std::deque<Request> requests;
std::deque<std::string> commands;
std::string shown_screen;  // under requests_mutex
std::atomic<bool> open_flag{false};

// The HUD (set_hud), under its own mutex.
std::mutex hud_mutex;
std::optional<HudView> hud_wanted;
Rml::ElementDocument* hud_document = nullptr;
std::string hud_shown_rml;

std::string asset(const char* name) { return (assets_path / name).string(); }

// Starts RmlUi on the presentation thread, when a menu first opens.
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
    navigator = std::make_unique<Navigator>(context, assets_path / "screens");
    std::fprintf(stdout, "{\"event\":\"ui_started\",\"width\":%u,\"height\":%u}\n", width, height);
    std::fflush(stdout);
    return true;
}

// Applies queued requests to the navigator.
void apply_requests() {
    std::deque<Request> pending;
    {
        std::lock_guard lock(requests_mutex);
        pending.swap(requests);
    }
    for (auto& request : pending) {
        if (auto* open = std::get_if<OpenRequest>(&request)) {
            if (!(open->on_top ? navigator->show(open->screen) : navigator->open(open->screen)))
                std::fprintf(stderr, "{\"event\":\"ui_screen_missing\",\"screen\":\"%s\"}\n", open->screen.c_str());
        } else if (std::holds_alternative<CloseRequest>(request)) {
            navigator->close();
        } else if (auto* setting = std::get_if<SettingRequest>(&request)) {
            navigator->set_setting(setting->name, setting->value);
        } else if (auto* list = std::get_if<ListRequest>(&request)) {
            navigator->set_list(list->name, std::move(list->items));
        } else if (auto* pad = std::get_if<PadRequest>(&request)) {
            navigator->set_pad(pad->pad);
        } else if (auto* text = std::get_if<TextRequest>(&request)) {
            navigator->set_text(text->name, text->text);
        } else {
            navigator->handle(std::get<Action>(request));
        }
    }
    std::lock_guard lock(requests_mutex);
    while (auto command = navigator->take_command()) commands.push_back(*command);
    shown_screen = navigator->current_screen();
    // Keep the flag set while requests are still waiting.
    open_flag.store(navigator->is_open() || std::any_of(requests.begin(), requests.end(), [](const Request& r) {
                        return std::holds_alternative<OpenRequest>(r);
                    }));
}

const char* magic_name(int magic) {
    static const char* names[] = {"Firecracker", "Dynamite", "Magnet", "Atom", "Rocket", "Ray Gun"};
    return magic >= 0 && magic < 6 ? names[magic] : "";
}

std::string percent(float value) {
    return std::to_string(int(std::clamp(value, 0.0f, 1.0f) * 1000.0f) / 10.0f) + "%";
}

std::string escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else out += c;
    }
    return out;
}

// The HUD's markup for these values (placeholder art).
std::string hud_rml(const HudView& hud) {
    std::string rml;
    rml += "<div id='left'>";
    rml += "<div id='lives'><div id='hearts'>";
    for (int i = 0; i < 3; ++i) rml += i < hud.hearts ? "<div class='heart'/>" : "<div class='heart lost'/>";
    rml += "</div>";
    const bool hurry = hud.drop_left < 0.35f;
    rml += std::string("<div id='drop' class='bar") + (hurry ? " hurry" : "") + "'><div class='fill' style='width: " +
           percent(hud.drop_left) + "'/></div></div>";
    rml += "<div id='magic'><div id='item'>";
    rml += hud.magic >= 0 ? "<div class='name'>" + std::string(magic_name(hud.magic)) + "</div>" : "";
    rml += "</div><div id='meter' class='bar'><div class='fill' style='width: " + percent(hud.meter) + "'/></div></div>";
    rml += "<div id='next'>";
    for (std::size_t i = 0; i < hud.next.size(); ++i) {
        const auto& piece = hud.next[i];
        const int cell = i == 0 ? 34 : 26;
        int width = 1, height = 1;
        for (const auto& [x, y] : piece.cells) width = std::max(width, x + 1), height = std::max(height, y + 1);
        rml += "<div class='piece' style='width: " + std::to_string(width * cell) + "dp; height: " +
               std::to_string(height * cell) + "dp;'>";
        if (piece.mystery) {
            rml += "<div class='mystery'>?</div>";
        } else {
            for (const auto& [x, y] : piece.cells)
                rml += "<div class='cell' style='left: " + std::to_string(x * cell) + "dp; top: " +
                       std::to_string(y * cell) + "dp; width: " + std::to_string(cell - 3) + "dp; height: " +
                       std::to_string(cell - 3) + "dp; background-color: " + piece.colour + ";'/>";
        }
        rml += "</div>";
    }
    rml += "</div></div>";
    rml += "<div id='right'>";
    rml += "<div id='mode'>" + escape(hud.mode) + "</div><div id='stage'>" + escape(hud.stage) + "</div>";
    rml += "<div id='middle'>";
    if (!hud.time.empty()) rml += "<div id='time'>" + escape(hud.time) + "</div>";
    if (hud.progress >= 0.0f)
        rml += "<div id='progress' class='bar'><div class='fill' style='width: " + percent(hud.progress) + "'/></div>";
    rml += "</div></div>";
    return rml;
}

// Shows, updates or hides the HUD document. Returns whether it is shown.
bool update_hud_document() {
    std::optional<HudView> hud;
    {
        std::lock_guard lock(hud_mutex);
        hud = hud_wanted;
    }
    const bool show = hud.has_value() && !navigator->is_open();
    if (!show) {
        if (hud_document != nullptr) hud_document->Hide();
        return false;
    }
    if (hud_document == nullptr) {
        hud_document = context->LoadDocument(asset("hud.rml"));
        if (hud_document == nullptr) {
            std::fprintf(stderr, "{\"event\":\"ui_screen_missing\",\"screen\":\"hud\"}\n");
            std::lock_guard lock(hud_mutex);
            hud_wanted.reset();
            return false;
        }
    }
    const auto rml = hud_rml(*hud);
    if (rml != hud_shown_rml) {
        if (auto* root = hud_document->GetElementById("hud")) root->SetInnerRML(rml);
        hud_shown_rml = rml;
    }
    if (!hud_document->IsVisible()) hud_document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    return true;
}

void hook_init(plume::RenderInterface* render_interface, plume::RenderDevice* device) {
    renderer = std::make_unique<Renderer>(render_interface, device);
}

void hook_draw(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer) {
    if (!renderer || start_failed) return;
    if (!started) {
        // Start RmlUi only once a menu is wanted.
        std::lock_guard lock(requests_mutex);
        std::lock_guard hud_lock(hud_mutex);
        if (!hud_wanted && std::none_of(requests.begin(), requests.end(),
                                        [](const Request& r) { return std::holds_alternative<OpenRequest>(r); }))
            return;
    }
    const std::uint32_t width = framebuffer->getWidth();
    const std::uint32_t height = framebuffer->getHeight();
    renderer->begin_frame(list, framebuffer);
    if (!started && !start(width, height)) {
        start_failed = true;
        open_flag.store(false);
        renderer->end_frame();
        return;
    }
    apply_requests();
    const bool hud_visible = update_hud_document();
    if (navigator->is_open() || hud_visible) {
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
    hud_document = nullptr;
    navigator.reset();
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
    RT64::SetRenderHooks(hook_init, hook_draw, hook_deinit);
    const char* test = std::getenv("BLOCKPLANET_UI_TEST");
    if (test != nullptr && std::string(test) == "1") request_open("main");
}

void request_open(const std::string& screen) {
    std::lock_guard lock(requests_mutex);
    requests.push_back(OpenRequest{screen});
    open_flag.store(true);
}

void request_show(const std::string& screen) {
    std::lock_guard lock(requests_mutex);
    requests.push_back(OpenRequest{screen, true});
    open_flag.store(true);
}

void request_close() {
    std::lock_guard lock(requests_mutex);
    requests.push_back(CloseRequest{});
}

void queue_action(Action action) {
    std::lock_guard lock(requests_mutex);
    requests.push_back(action);
}

void set_setting(const std::string& name, const std::string& value) {
    std::lock_guard lock(requests_mutex);
    requests.push_back(SettingRequest{name, value});
}

void set_list(const std::string& name, std::vector<ListItem> items) {
    std::lock_guard lock(requests_mutex);
    requests.push_back(ListRequest{name, std::move(items)});
}

void set_text(const std::string& name, const std::string& text) {
    std::lock_guard lock(requests_mutex);
    requests.push_back(TextRequest{name, text});
}

void set_pad(bool pad) {
    std::lock_guard lock(requests_mutex);
    requests.push_back(PadRequest{pad});
}

void send_command(const std::string& command) {
    std::lock_guard lock(requests_mutex);
    commands.push_back(command);
}

std::string current_screen() {
    std::lock_guard lock(requests_mutex);
    return open_flag.load() ? shown_screen : std::string();
}

void set_hud(std::optional<HudView> hud) {
    std::lock_guard lock(hud_mutex);
    hud_wanted = std::move(hud);
}

bool menus_open() { return open_flag.load(); }

std::optional<std::string> take_command() {
    std::lock_guard lock(requests_mutex);
    if (commands.empty()) return std::nullopt;
    auto command = std::move(commands.front());
    commands.pop_front();
    return command;
}

} // namespace blockplanet::ui
