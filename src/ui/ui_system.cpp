#include "ui_system.h"

#include "tetrisphere/under_layer.h"

#include "grid_texture.h"
#include "ui_grid.h"
#include "ui_navigator.h"
#include "ui_renderer.h"

#include "rhi/rt64_render_hooks.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <fstream>
#include <array>
#include <cmath>
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

// The grid test screen (below).
bool grid_lab_command(const std::string& command);

// Menus are laid out for a 1080-pixel-high screen and scaled to the window.
constexpr float design_height = 1080.0f;
// Graphics > Text size (docs/STYLE.md, D1): it multiplies every dp, so menus,
// the HUD and cards grow together.
float text_scale = 1.0f;
float text_scale_of(const std::string& size) {
    return size == "small" ? 0.85f : size == "large" ? 1.2f : size == "larger" ? 1.4f : 1.0f;
}
float dp_ratio(float height) { return height / design_height * text_scale; }

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
// The menu grid has a context of its own, drawn before the menus' (behind
// them) or, while the game's picture is laid over it, before the game's
// picture (tetrisphere/under_layer.h).
Rml::Context* grid_context = nullptr;
std::unique_ptr<Navigator> navigator;
std::unique_ptr<MenuGrid> grid;  // the menu grid (ui_grid.h)
bool started = false;
bool start_failed = false;

// Requests from other threads, applied on the presentation thread.
struct OpenRequest {
    std::string screen;
    bool on_top = false;  // keep the open screens (Back returns to them)
};
struct CloseRequest {};
struct RestoreRequest { std::vector<std::pair<std::string, std::size_t>> path; };
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
    bool buttons = false;  // render [family:name] tokens as button icons
};
struct AltButtonRequest {
    std::string spec;  // "[family:name]", as in the cards
};
struct PadRequest {
    bool pad;
};
using Request = std::variant<OpenRequest, CloseRequest, RestoreRequest, Action, SettingRequest, ListRequest, TextRequest, PadRequest,
                             AltButtonRequest>;
std::mutex requests_mutex;
std::deque<Request> requests;
std::deque<std::string> commands;
std::vector<std::pair<std::string, std::size_t>> shown_path;  // the last open menus (under requests_mutex)
std::string shown_screen;  // under requests_mutex
std::atomic<bool> open_flag{false};

// The HUD (set_hud), under its own mutex.
std::mutex hud_mutex;
std::optional<HudView> hud_wanted;
std::optional<std::array<float, 4>> hud_item;  // x, y, width, height in pixels
std::array<float, 2> hud_item_frame{};          // the context's size then
Rml::ElementDocument* hud_document = nullptr;
std::string hud_shown_rml, game_text_shown_rml;

std::string asset(const char* name) { return (assets_path / name).string(); }

// Starts RmlUi on the presentation thread, when a menu first opens.
bool start(std::uint32_t width, std::uint32_t height) {
    started = true;
    system_interface = std::make_unique<System>();
    Rml::SetSystemInterface(system_interface.get());
    Rml::SetRenderInterface(renderer.get());
    if (!Rml::Initialise()) return false;
    for (const char* font : {"fonts/LatoLatin-Regular.ttf", "fonts/LatoLatin-Bold.ttf", "fonts/LatoLatin-Italic.ttf", "fonts/LatoLatin-BoldItalic.ttf", "fonts/lucide.ttf"}) {
        if (!Rml::LoadFontFace(asset(font))) std::fprintf(stderr, "{\"event\":\"ui_font_missing\",\"font\":\"%s\"}\n", font);
    }
    context = Rml::CreateContext("main", Rml::Vector2i(int(width), int(height)));
    if (context == nullptr) return false;
    context->SetDensityIndependentPixelRatio(dp_ratio(float(height)));
    grid_context = Rml::CreateContext("grid", Rml::Vector2i(int(width), int(height)));
    if (grid_context == nullptr) return false;
    grid_context->SetDensityIndependentPixelRatio(dp_ratio(float(height)));
    navigator = std::make_unique<Navigator>(context, assets_path / "screens");
    grid = std::make_unique<MenuGrid>(asset("grid.rml"));
    for (const auto& [name, value] : grid_lab_values()) navigator->set_setting(name, value);
    navigator->set_setting("grid_theme", "blue");
    navigator->set_setting("grid_show", "both");
    std::fprintf(stdout, "{\"event\":\"ui_started\",\"width\":%u,\"height\":%u}\n", width, height);
    std::fflush(stdout);
    return true;
}

std::string with_buttons(const std::string& escaped_text);
std::string escape(const std::string& text);

// The card panel's scrolling (presentation thread): the page shown (its
// heading and first words), how far it is scrolled, whether it follows the
// text as it types in (until the player scrolls), the most the text may take
// before it scrolls (from the last layout: the panel stops short of the top
// of the screen), and whether it overflows (for the hint).
struct CardScroll {
    std::string page;
    float top = 0.0f;
    bool follow = true;
    float max_px = 0.0f;
    bool overflow = false;
    bool at_end = true;  // scrolled to the end (or nothing to scroll)
};
CardScroll card_scroll;
std::atomic<bool> card_needs_scroll_flag{false};
std::mutex card_scroll_mutex;
float card_scroll_lines = 0.0f;  // requested, not yet applied

// After layout: keeps the card panel's text within the screen (scrolling it),
// follows the text as it types in, and applies the player's scrolling.
void apply_card_scroll(float window_h) {
    float lines = 0.0f;
    {
        std::lock_guard lock(card_scroll_mutex);
        lines = card_scroll_lines;
        card_scroll_lines = 0.0f;
    }
    Rml::Element* scroll = hud_document != nullptr && hud_document->IsVisible() ? hud_document->GetElementById("card-scroll") : nullptr;
    if (scroll == nullptr) {
        card_scroll = CardScroll{};
        card_needs_scroll_flag.store(false);
        return;
    }
    Rml::Element* panel = scroll->GetParentNode();
    const auto page = scroll->GetAttribute<Rml::String>("data-page", "");
    if (page != card_scroll.page) {  // a new page: from its top, following
        card_scroll.page = page;
        card_scroll.top = 0.0f;
        // Text typing in is followed; text shown whole (joined pages) starts at its top.
        card_scroll.follow = scroll->GetAttribute<Rml::String>("data-follow", "1") != "0";
    }
    // The most the text may take: the panel's bottom edge stays put, and its
    // top stops 3% of the screen short of the top.
    const float panel_top = panel->GetAbsoluteOffset(Rml::BoxArea::Border).y;
    const float panel_h = panel->GetBox().GetSize(Rml::BoxArea::Border).y;
    const float shown = scroll->GetClientHeight();
    if (panel_h > 0.0f) {
        const float others = panel_h - shown;
        // Research: BLOCKPLANET_CARD_TOP=0.5 keeps the panel's top that far
        // down the screen (to test the scrolling).
        static const float top_margin = [] {
            const char* v = std::getenv("BLOCKPLANET_CARD_TOP");
            return v != nullptr ? std::clamp(float(std::atof(v)), 0.0f, 0.9f) : 0.03f;
        }();
        // A panel anchored at its top grows downwards: it stops 3% short of the bottom.
        const bool from_top = panel->GetAttribute<Rml::String>("data-anchor", "bottom") == "top";
        const float wanted = from_top ? window_h * (1.0f - 0.03f) - panel_top - others
                                      : (panel_top + panel_h) - window_h * top_margin - others;
        if (std::abs(wanted - card_scroll.max_px) > 2.0f) card_scroll.max_px = std::max(0.0f, wanted);
    }
    const float max_top = std::max(0.0f, scroll->GetScrollHeight() - shown);
    const float line = scroll->GetBox().GetSize().y > 0.0f ? panel->GetComputedValues().font_size() * 1.32f : 20.0f;
    if (lines != 0.0f) {
        card_scroll.follow = false;
        card_scroll.top = std::clamp(card_scroll.top + lines * line, 0.0f, max_top);
        if (card_scroll.top >= max_top - 1.0f) card_scroll.follow = true;
    }
    if (card_scroll.follow) card_scroll.top = max_top;
    card_scroll.top = std::min(card_scroll.top, max_top);
    scroll->SetScrollTop(card_scroll.top);
    card_scroll.overflow = max_top > 1.0f;
    card_scroll.at_end = card_scroll.top >= max_top - 1.0f;
    card_needs_scroll_flag.store(card_scroll.overflow && !card_scroll.at_end);
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
        } else if (auto* restore = std::get_if<RestoreRequest>(&request)) {
            navigator->restore(restore->path);
        } else if (auto* setting = std::get_if<SettingRequest>(&request)) {
            navigator->set_setting(setting->name, setting->value);
            if (setting->name == "text_size") {
                text_scale = text_scale_of(setting->value);
                context->SetDensityIndependentPixelRatio(dp_ratio(float(context->GetDimensions().y)));
            }
        } else if (auto* list = std::get_if<ListRequest>(&request)) {
            navigator->set_list(list->name, std::move(list->items));
        } else if (auto* pad = std::get_if<PadRequest>(&request)) {
            navigator->set_pad(pad->pad);
        } else if (auto* text = std::get_if<TextRequest>(&request)) {
            if (text->buttons) navigator->set_rml(text->name, with_buttons(escape(text->text)));
            else navigator->set_text(text->name, text->text);
        } else if (auto* alt = std::get_if<AltButtonRequest>(&request)) {
            navigator->set_alt_button(with_buttons(alt->spec));
        } else {
            navigator->handle(std::get<Action>(request));
        }
    }
    std::lock_guard lock(requests_mutex);
    while (auto command = navigator->take_command()) {
        // The grid test screen's settings and buttons stay here.
        if (!grid_lab_command(*command)) commands.push_back(*command);
    }
    if (navigator->is_open()) shown_path = navigator->path();
    shown_screen = navigator->current_screen();
    // Keep the flag set while requests are still waiting.
    open_flag.store(navigator->is_open() || std::any_of(requests.begin(), requests.end(), [](const Request& r) {
                        return std::holds_alternative<OpenRequest>(r) || std::holds_alternative<RestoreRequest>(r);
                    }));
}

const char* magic_name(int magic) {
    // Held magic 0 to 5, lowest to highest (the manual: Firecracker, Bundle O' Dynamite,
    // Electro Magnet, Atom, Bomb, Ray Gun; shortened to fit the item box).
    static const char* names[] = {"Firecracker", "Dynamite", "Magnet", "Atom", "Bomb", "Ray Gun"};
    return magic >= 0 && magic < 6 ? names[magic] : "";
}

std::string percent(float value) {
    return std::to_string(int(std::clamp(value, 0.0f, 1.0f) * 1000.0f) / 10.0f) + "%";
}

// A number that changes while it is read (a clock, a score): each digit in a
// box of one width, as Lato's figures are proportional and the number would
// otherwise jiggle (docs/STYLE.md, Numbers).
std::string digits(const std::string& escaped_text) {
    std::string out;
    for (char c : escaped_text) {
        if (c >= '0' && c <= '9') out += std::string("<span class='d'>") + c + "</span>";
        else out += c;
    }
    return out;
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

// The HUD's markup for these values (placeholder art): one tinted container
// per side, its groups separated by dividers. The magic group leaves the item
// box clear, so the game's 3D model drawn under the HUD shows through.
std::string pieces_rml(const std::vector<HudPieceView>& next, std::size_t count);

std::string pieces_rml(const std::vector<HudPieceView>& next, std::size_t count) {
    std::string rml;
    for (std::size_t i = 0; i < std::min(count, next.size()); ++i) {
        const auto& piece = next[i];
        const int cell = i == 0 ? 34 : 26;
        int width = 1, height = 1;
        for (const auto& [x, y] : piece.cells) width = std::max(width, x + 1), height = std::max(height, y + 1);
        rml += "<div class='piece' style='width: " + std::to_string(width * cell) + "dp; height: " +
               std::to_string(height * cell) + "dp;'>";
        if (piece.random_block) {
            rml += "<div class='random-block'>?</div>";
        } else {
            for (const auto& [x, y] : piece.cells)
                rml += "<div class='cell' style='left: " + std::to_string(x * cell) + "dp; top: " +
                       std::to_string(y * cell) + "dp; width: " + std::to_string(cell - 3) + "dp; height: " +
                       std::to_string(cell - 3) + "dp; background-color: " + piece.colour + ";'/>";
        }
        rml += "</div>";
    }
    return rml;
}

// One player's VS column: hearts and drop timer, X count, magic, next.
std::string versus_column(const HudView::VersusPlayer& p, const char* id, const char* name) {
    std::string rml = std::string("<div id='") + id + "' class='side versus'>";
    rml += std::string("<div class='group'><div class='label'>") + name + "</div><div class='hearts'>";
    for (int i = 0; i < 3; ++i) rml += i < p.hearts ? "<div class='heart'/>" : "<div class='heart lost'/>";
    rml += std::string("</div><div class='bar vs-drop") + (p.drop_left < 0.35f ? " hurry" : "") +
           "'><div class='fill' style='width: " + std::to_string(int(std::clamp(p.drop_left, 0.0f, 1.0f) * 100)) + "%;'/></div></div>";
    rml += "<div class='rule'><div class='line'/></div>";
    rml += "<div class='group'><div class='label'>Attack</div><div class='count'>\u00D7" + std::to_string(p.multiplier) + "</div></div>";
    rml += "<div class='rule'><div class='line'/></div>";
    rml += std::string("<div class='group'><div class='label'>Magic</div><div class='vs-item'>") +
           (p.magic >= 0 ? magic_name(p.magic) : "") + "</div><div class='bar vs-meter'><div class='fill' style='width: " +
           std::to_string(int(std::clamp(p.meter, 0.0f, 1.0f) * 100)) + "%;'/></div></div>";
    rml += "<div class='rule'><div class='line'/></div>";
    rml += "<div class='group vs-next'><div class='label'>Next</div>" + pieces_rml(p.next, 2) + "</div>";
    rml += "</div>";
    return rml;
}

// Button codes in the game's text ("[k:Z]", "[x:A]", "[p:Cross]", "[n:B]",
// family then name) as button symbols: a key cap, an Xbox letter in its
// colour, a PlayStation shape, a Switch letter.
std::string with_buttons(const std::string& escaped_text) {
    std::string out;
    std::size_t at = 0;
    while (true) {
        const std::size_t open = escaped_text.find('[', at);
        const std::size_t close = open == std::string::npos ? open : escaped_text.find(']', open);
        if (open == std::string::npos || close == std::string::npos || close < open + 3 || escaped_text[open + 2] != ':') {
            out += escaped_text.substr(at);
            return out;
        }
        out += escaped_text.substr(at, open - at);
        const char family = escaped_text[open + 1];
        const std::string name = escaped_text.substr(open + 3, close - open - 3);
        const bool shoulder = family != 'k' && ((name.size() == 2 && (name[0] == 'L' || name[0] == 'R' || name[0] == 'Z')) ||
                                                name == "L" || name == "R");
        if (family == 'd') {  // the d-pad: a small cross on a dark cap
            out += "<span class='btn pad dpad'><span class='dpad-cross'><span class='arm h'/><span class='arm v'/></span></span>";
        } else if (shoulder) {  // bumpers and triggers: a dark rounded bar with its name
            out += "<span class='btn shoulder'>" + name + "</span>";
        } else if (family == 'k') {
            out += "<span class='btn key'>" + name + "</span>";
        } else if (family == 'x') {
            const char* colour = name == "A" ? "#3fae3f" : name == "B" ? "#d6402f" : name == "X" ? "#2f6fd6" : "#e0b020";
            out += std::string("<span class='btn pad' style='background-color: ") + colour + ";'>" + name + "</span>";
        } else if (family == 'p') {
            const std::string shape = name == "Cross" ? "cross" : name == "Circle" ? "circle" : name == "Square" ? "square" : "triangle";
            out += shape == "cross" ? "<span class='btn pad ps'><span class='ps-cross'><span class='bar a'/><span class='bar b'/></span></span>"
                                    : "<span class='btn pad ps'><span class='ps-" + shape + "'/></span>";
        } else {
            out += "<span class='btn pad nx'>" + name + "</span>";
        }
        at = close + 1;
    }
}

std::string game_text_rml(const HudView& hud) {
    std::string rml;
    const auto pct = [](float v) { return std::to_string(v * 100.0f) + "%"; };
    if (const auto& p = hud.card_panel) {
        // Runs keep the game's colours (and its fade as each word types in);
        // a highlighted run (the game's yellow) is bold. Spaces go inside the
        // spans, as spaces between elements are dropped.
        const auto runs = [&](const std::vector<std::pair<std::string, std::string>>& parts) {
            std::string out;
            for (std::size_t k = 0; k < parts.size(); ++k) {
                if (parts[k].first == "\n") {
                    out += "<br/>";
                    continue;
                }
                const auto& colour = parts[k].second;
                const bool yellow = colour.size() >= 7 && std::stoi(colour.substr(3, 2), nullptr, 16) > 170;
                // "fe" alpha: a glossary term where it is introduced.
                const bool introduced = yellow && colour.size() == 9 && colour.compare(7, 2, "fe") == 0;
                out += std::string("<span class='") + (introduced ? "hl new" : yellow ? "hl" : "") + "' style='color: " + colour + ";'>" +
                       with_buttons(escape(parts[k].first)) + (k + 1 < parts.size() && parts[k + 1].first != "\n" ? " " : "") + "</span>";
            }
            return out;
        };
        // The page, for the scroll state: its heading and first words.
        std::string page;
        for (const auto& part : p->heading) page += part.first + " ";
        for (std::size_t k = 0; k < p->body.size() && k < 4; ++k) page += p->body[k].first + " ";
        if (!p->blocks.empty())
            for (std::size_t k = 0; k < p->blocks.front().runs.size() && k < 4; ++k) page += p->blocks.front().runs[k].first + " ";
        const std::string anchor = p->top >= 0.0f ? "top: " + pct(p->top) : "bottom: " + pct(p->bottom);
        rml += std::string("<div class='card-panel") + (card_scroll.overflow ? " overflow" : "") + "' data-anchor='" +
               (p->top >= 0.0f ? "top" : "bottom") + "' style='right: " + pct(p->right) + "; " + anchor + "; width: " + pct(p->w) +
               ";'>";
        const std::string limit = card_scroll.max_px > 0.0f ? "max-height: " + std::to_string(int(card_scroll.max_px)) + "px;" : "";
        if (p->blocks.empty()) {
            if (!p->heading.empty()) rml += "<div class='card-heading'>" + runs(p->heading) + "</div>";
            rml += "<div id='card-scroll' class='card-scroll' data-page='" + escape(page) + "' style='" + limit + "'><div class='card-body'>" +
                   runs(p->body) + "</div></div>";
        } else {
            // Joined pages: everything scrolls together, headings included.
            rml += "<div id='card-scroll' class='card-scroll' data-follow='0' data-page='" + escape(page) + "' style='" + limit + "'>";
            for (const auto& block : p->blocks)
                rml += std::string("<div class='") + (block.heading ? "card-heading" : "card-body card-para") + "'>" + runs(block.runs) + "</div>";
            rml += "</div>";
        }
        // Until the text has been scrolled to its end, A does not move on: the
        // hint asks for scrolling instead (the owner's choice).
        std::string hint = p->hint.empty() ? std::string() : with_buttons(escape(p->hint));
        if (card_scroll.overflow && !card_scroll.at_end && !p->scroll_hint.empty())
            hint = with_buttons(escape(p->scroll_hint + " to continue"));
        else if (card_scroll.overflow && !p->scroll_hint.empty())
            hint += std::string(hint.empty() ? "" : "<span>&nbsp;&nbsp;</span>") + with_buttons(escape(p->scroll_hint));
        rml += "<div class='card-hint'>" + (hint.empty() ? std::string("&nbsp;") : hint) + "</div>";
        rml += "</div>";
        // The tail, beside the panel (a child would be clipped by it), from the
        // same bottom edge: half of it overlaps the panel's rim.
        if (p->tail >= 0.0f) {
            const float left = 1.0f - p->right - p->w + p->tail * p->w;
            rml += "<div class='card-tail' style='left: " + pct(left) + "; bottom: " + pct(p->bottom) + ";'/>";
        }
    }
    for (const auto& t : hud.game_text)
    {
        std::string body;
        if (t.parts.empty()) {
            body = with_buttons(escape(t.text));
        } else {
            for (std::size_t k = 0; k < t.parts.size(); ++k) {
                if (t.parts[k].first == "\n") {  // a line break
                    body += "<br/>";
                    continue;
                }
                // The space goes inside the span (spaces between elements are dropped).
                body += std::string("<span style='color: ") + t.parts[k].second + ";'>" + with_buttons(escape(t.parts[k].first)) +
                        (k + 1 < t.parts.size() && t.parts[k + 1].first != "\n" ? " " : "") + "</span>";
            }
        }
        const std::string wrap = t.wrap > 0 ? "width: " + pct(t.wrap) + "; white-space: normal; line-height: 1.3;" : "";
        rml += "<div class='game-text' data-w='" + std::to_string(t.width) + "' style='left: " + pct(t.x) + "; top: " +
               pct(t.y) + "; font-size: " + std::to_string(t.size) + "vh; color: " + t.colour + ";" + wrap + "'>" + body + "</div>";
    }
    return rml;
}

std::string hud_rml_parts(const HudView& hud);

std::string hud_rml_parts(const HudView& hud) {
    std::string rml;
    if (!hud.title_notice.empty()) {
        const auto pct = [](float v) { return std::to_string(v * 100.0f) + "%"; };
        rml += "<div id='title-notice' style='left: " + pct(hud.title_x) + "; top: " + pct(hud.title_y) + "; width: " +
               pct(hud.title_w) + ";'>";
        for (const auto& line : hud.title_notice) rml += "<div class='notice'>" + escape(line) + "</div>";
        if (!hud.title_disclaimer.empty()) rml += "<div class='disclaimer'>" + escape(hud.title_disclaimer) + "</div>";
        return rml + "</div>";
    }
    if (hud.versus)
        return versus_column(hud.players[0], "left", "Player 1") + versus_column(hud.players[1], "right", "Player 2");
    if (hud.bottom_row) {
        // Training: one row along the bottom, only what has been introduced,
        // in the order a player meets them.
        const auto glow = [&](const char* part) {
            return std::find(hud.intro.begin(), hud.intro.end(), part) != hud.intro.end() ? " intro" : "";
        };
        std::vector<std::string> parts;
        if (hud.show_next)
            parts.push_back(std::string("<div id='next' class='group") + glow("next") + "'><div class='label'>Next</div>" +
                            pieces_rml(hud.next, hud.next.size()) + "</div>");
        if (hud.show_lives) {
            std::string lives = std::string("<div id='lives' class='group") + glow("lives") + "'><div class='label'>Health</div><div id='hearts'>";
            for (int i = 0; i < 3; ++i) lives += i < hud.hearts ? "<div class='heart'/>" : "<div class='heart lost'/>";
            lives += std::string("</div><div id='drop' class='bar") + (hud.drop_left < 0.35f ? " hurry" : "") +
                     "'><div id='drop-fill' class='fill'/></div></div>";
            parts.push_back(lives);
        }
        if (hud.show_magic) {
            const bool model = hud.magic >= 0 && hud.magic_model;
            parts.push_back(std::string("<div id='magic' class='") + glow("magic") +
                            "'><div class='band'><div class='label'>Magic</div></div><div class='window'><div class='edge'/><div id='item'" +
                            (model ? " class='held'" : "") + ">" +
                            (hud.magic >= 0 && !hud.magic_model ? "<div class='name'>" + std::string(magic_name(hud.magic)) + "</div>" : "") +
                            "</div><div class='edge side-meter'><div id='meter-v' class='bar'><div id='meter-v-fill' class='fill'/></div></div>"
                            "</div><div class='band'><div id='meter' class='bar'><div id='meter-fill' class='fill'/></div></div></div>");
        }
        if (hud.multiplier >= 1)
            parts.push_back(std::string("<div id='xcount' class='group") + glow("xcount") + "'><div class='label'>X count</div><div class='count'>\u00D7" +
                            std::to_string(hud.multiplier) + "</div></div>");
        if (hud.progress >= 0.0f)
            parts.push_back(std::string("<div id='core' class='group") + glow("progress") +
                            "'><div class='label'>To the core</div><div id='progress' class='bar'><div id='progress-fill' class='fill'/></div></div>");
        if (hud.drops_left >= 0)
            parts.push_back(std::string("<div id='drops' class='group") + glow("moves") + "'><div class='label'>Drops left</div><div class='count'>" +
                            std::to_string(hud.drops_left) + "</div></div><div class='rule'><div class='line'/></div><div id='slides' class='group" +
                            glow("moves") + "'><div class='label'>Slides left</div><div class='count'>" +
                            std::to_string(std::max(0, hud.slides_left)) + "</div></div>");
        if (parts.empty()) return rml;
        rml += "<div id='left' class='side row'>";
        for (std::size_t i = 0; i < parts.size(); ++i) rml += (i ? "<div class='rule'><div class='line'/></div>" : "") + parts[i];
        return rml + "</div>";
    }
    rml += "<div id='left' class='side'>";
    if (hud.drops_left >= 0) {
        // Puzzle: the moves left, nothing else (no hearts, magic or queue).
        rml += "<div id='drops' class='group'><div class='label'>Drops left</div><div class='count'>" +
               std::to_string(hud.drops_left) + "</div></div><div class='rule'><div class='line'/></div>"
               "<div id='slides' class='group'><div class='label'>Slides left</div><div class='count'>" +
               std::to_string(std::max(0, hud.slides_left)) + "</div></div></div>";
    } else {
    rml += "<div id='lives' class='group'><div class='label'>Health</div><div id='hearts'>";
    for (int i = 0; i < 3; ++i) rml += i < hud.hearts ? "<div class='heart'/>" : "<div class='heart lost'/>";
    rml += "</div>";
    const bool hurry = hud.drop_left < 0.35f;
    // Bar fills are set on the elements each frame (set_hud_fills), so the
    // markup only changes when something else does.
    rml += std::string("<div id='drop' class='bar") + (hurry ? " hurry" : "") + "'><div id='drop-fill' class='fill'/></div>";
    // On portrait screens the clock sits here, under the hearts, so the top row stays one line.
    if (!hud.clock.empty())
        rml += std::string("<div class='clock-inline") + (hud.clock_low ? " low" : "") + "'>" + digits(escape(hud.clock)) + "</div>";
    rml += "</div>";
    rml += "<div class='rule'><div class='line'/></div>";
    // Score Attack: the clock, below health (the run ends when it runs out).
    if (!hud.clock.empty())
        rml += std::string("<div id='clock-group' class='group'><div class='label'>Time left</div><div id='clock'") +
               (hud.clock_low ? " class='low'" : "") + ">" + digits(escape(hud.clock)) + "</div></div><div id='clock-rule' class='rule'><div class='line'/></div>";
    // While the game draws the magic's model in the box it draws the box's tint
    // too, behind the model (magic_hooks.cpp); otherwise the HUD tints it.
    const bool model = hud.magic >= 0 && hud.magic_model;
    rml += std::string("<div id='magic'><div class='band'><div class='label'>Magic</div></div><div class='window'><div class='edge'/><div id='item'") +
           (model ? " class='held'" : "") + ">";
    // The magic's 3D model is drawn in the box by the game (magic_hooks.cpp);
    // its name is shown only when it is not.
    rml += hud.magic >= 0 && !hud.magic_model ? "<div class='name'>" + std::string(magic_name(hud.magic)) + "</div>" : "";
    // The meter sits under the box; on portrait screens, as a column beside it.
    rml += "</div><div class='edge side-meter'><div id='meter-v' class='bar'><div id='meter-v-fill' class='fill'/></div></div>"
           "</div><div class='band'><div id='meter' class='bar'><div id='meter-fill' class='fill'/></div></div></div>";
    if (hud.show_next) {
    rml += "<div class='rule'><div class='line'/></div>";
    rml += "<div id='next' class='group'><div class='label'>Next</div>";
    rml += pieces_rml(hud.next, hud.next.size());
    rml += "</div>";
    }
    rml += "</div>";
    }
    rml += "<div id='right' class='side'>";
    // Mode and stage, with the stage's progress bar beneath the stage number.
    rml += "<div id='where' class='group'><div class='label'>" + escape(hud.mode) + "</div><div id='stage'>" +
           escape(hud.stage) + "</div>";
    if (!hud.goal.empty()) rml += "<div class='sublabel'>Goal</div><div id='goal'>" + escape(hud.goal) + "</div>";
    if (hud.progress >= 0.0f)
        rml += "<div class='sublabel'>Progress</div><div id='progress' class='bar'><div id='progress-fill' class='fill'/></div>";
    rml += "</div>";
    if (!hud.score.empty())
        rml += "<div class='rule'><div class='line'/></div><div id='score-group' class='group'><div class='label'>Score</div><div id='score'>" +
               digits(escape(hud.score)) + "</div>" + (hud.score_best.empty() ? "" : "<div id='score-best'>" + escape(hud.score_best) + "</div>") +
               "</div>";
    if (!hud.time.empty()) {
        rml += "<div class='rule'><div class='line'/></div><div id='time-group' class='group'><div class='label'>" +
               escape(hud.time_label.empty() ? std::string("Time") : hud.time_label) + "</div><div id='time'>" + digits(escape(hud.time)) + "</div>";
        if (!hud.time_delta.empty())
            rml += std::string("<div id='net' class='") + (hud.time_ahead ? "ahead" : "behind") + "'>" + escape(hud.time_delta) + "</div>";
        if (!hud.best.empty()) rml += "<div id='best'>" + escape(hud.best) + "</div>";
        // Splits: the last few completed levels and the current one.
        rml += "<div id='splits'>";
        const std::size_t first = hud.splits.size() > 6 ? hud.splits.size() - 6 : 0;
        for (std::size_t i = first; i < hud.splits.size(); ++i) {
            const auto& sp = hud.splits[i];
            const bool last_done = !sp.current && i + 2 == hud.splits.size();
            rml += std::string("<div class='split") + (sp.current ? " current" : "") + (last_done ? " last-done" : "") + "'><span class='stage'>" + escape(sp.label) +
                   "</span><span class='delta " + (sp.delta.empty() ? "" : sp.ahead ? "ahead" : "behind") + "'>" + escape(sp.delta) +
                   "</span><span class='t'>" + escape(sp.time) + "</span></div>";
        }
        rml += "</div></div>";
    }
    if (hud.combo > 0) {
        const bool magic = hud.combo >= 20;  // 20 or more gives magic
        rml += std::string("<div class='rule'><div class='line'/></div><div id='combo-group' class='group") + (hud.combo_live ? " live" : "") +
               (magic ? " magic" : "") + "'><div class='label'>" + (hud.combo_live ? "Combo" : "Last combo") + "</div><div id='combo'>" +
               digits(std::to_string(hud.combo)) + "</div>";
        if (magic) rml += "<div id='combo-magic'>+ Magic</div>";
        if (hud.combo_best > 0) rml += "<div id='combo-best'>Best " + std::to_string(hud.combo_best) + "</div>";
        rml += "</div>";
    }
    if (!hud.story_score.empty()) {
        rml += "<div class='rule'><div class='line'/></div><div id='story-score' class='group'><div class='label'>Score</div><div id='score'>" +
               digits(escape(hud.story_score)) + "</div><div id='gains'>";
        for (std::size_t k = 0; k < hud.story_gains.size(); ++k)
            rml += std::string("<div class='gain") + (k == 0 ? " newest" : "") + "'>" + escape(hud.story_gains[k]) + "</div>";
        rml += "</div></div>";
    }
    rml += "</div>";
    return rml;
}

void set_hud_fills(const HudView& hud) {
    const std::pair<const char*, float> fills[] = {
        {"drop-fill", hud.drop_left}, {"meter-fill", hud.meter}, {"progress-fill", hud.progress}};
    for (const auto& [id, value] : fills)
        if (auto* e = hud_document->GetElementById(id)) e->SetProperty("width", percent(value));
    // The portrait screens' column meter fills upwards.
    if (auto* e = hud_document->GetElementById("meter-v-fill")) e->SetProperty("height", percent(hud.meter));
}

std::string px_text(float value);

// Floating numbers: a layer of their own, rebuilt each frame (there are few).
void place_popups(const HudView& hud) {
    auto* layer = hud_document->GetElementById("popups");
    if (layer == nullptr) return;
    static std::string shown;
    std::string rml;
    const auto size = context->GetDimensions();
    const float width = float(size.x), height = float(size.y);
    const float ph = hud.picture_h * height;
    const float cx = (hud.picture_x + hud.picture_w / 2.0f) * width;
    const float cy = (hud.picture_y + hud.picture_h / 2.0f) * height;
    const float dp = height / design_height;
    for (const auto& p : hud.popups) {
        const float t = std::clamp(p.age / 1.4f, 0.0f, 1.0f);
        const float rise = 90.0f * dp * (1.0f - (1.0f - t) * (1.0f - t));
        const float alpha = t < 0.7f ? 1.0f : 1.0f - (t - 0.7f) / 0.3f;
        const float scale = t < 0.12f ? 0.6f + t / 0.12f * 0.4f : 1.0f;
        const float x = cx + p.x * ph, y = cy + p.y * ph - rise;
        rml += "<div class='popup' style='left: " + px_text(x - 200.0f * dp) + "; top: " + px_text(y - 30.0f * dp) +
               "; opacity: " + std::to_string(alpha) + "; transform: scale(" + std::to_string(scale) + ");'><div class='points'>" +
               escape(p.text) + "</div>" + (p.note.empty() ? "" : "<div class='note'>" + escape(p.note) + "</div>") + "</div>";
    }
    for (const auto& m : hud.marks) {
        const float x = cx + m.x * ph, y = cy + m.y * ph;
        rml += "<div class='mark " + escape(m.kind) + "' style='left: " + px_text(x - 40.0f * dp) + "; top: " +
               px_text(y - 30.0f * dp) + ";'>" + escape(m.text) + "</div>";
    }
    if (rml != shown) {
        layer->SetInnerRML(rml);
        shown = rml;
    }
}

std::string px(float value) { return std::to_string(int(std::lround(value))) + "px"; }
std::string px_text(float value) { return px(value); }

// Places the two containers (docs/HUD_DESIGN.md): where they would be on a 4:3
// screen (3:4 on portrait screens, where they are rows above and below), so on
// wider or taller screens they stay near the sphere; pushed out as the sphere
// grows on screen, so they keep clear of it, but never past the screen's edge.
// Sizes come from the last layout.
float hud_left_scale = 1.0f;  // the left container's drawn scale (portrait rows shrink to fit)

void place_hud_sides(const HudView& hud, float width, float height, Rml::Vector2f& left_at) {
    hud_left_scale = 1.0f;
    auto* left = hud_document->GetElementById("left");
    auto* right = hud_document->GetElementById("right");
    const float dp = dp_ratio(height);
    const float margin = 24.0f * dp;
    if (hud.bottom_row && left != nullptr) {
        // Training's row: at the bottom, centred under the sphere's side of the
        // picture (a quarter of the way across), clear of Wheels on the right.
        static Rml::Vector2f last{0, 0};
        auto s = left->GetBox().GetSize(Rml::BoxArea::Border);
        if (s.x > 0 && s.y > 0) last = s; else s = last;
        // The row is drawn 1.3 times larger about its bottom centre (hud.rcss),
        // so it is kept on screen at that size.
        constexpr float scale = 1.3f;
        const float grow = (scale - 1.0f) / 2.0f * s.x;
        const float cx = (hud.picture_x + hud.picture_w * 0.25f) * width;
        const float lx = std::clamp(cx - s.x / 2.0f, margin + grow, std::max(margin + grow, width - margin - s.x - grow));
        const float ly = height - margin - s.y;
        left->SetProperty("left", px(lx));
        left->SetProperty("top", px(ly));
        left_at = {lx, ly};
        return;
    }
    if (left == nullptr || right == nullptr) return;
    const float cx = (hud.picture_x + hud.picture_w / 2.0f) * width;
    const float cy = (hud.picture_y + hud.picture_h / 2.0f) * height;
    // Sizes from the last layout (the border box).
    const auto size_of = [](Rml::Element* e) { return e->GetBox().GetSize(Rml::BoxArea::Border); };
    // Right after the markup is rebuilt the new elements have no size yet:
    // keep the last measured sizes.
    static Rml::Vector2f last_left{0, 0}, last_right{0, 0};
    auto ls = size_of(left), rs = size_of(right);
    if (ls.x > 0 && ls.y > 0) last_left = ls; else ls = last_left;
    if (rs.x > 0 && rs.y > 0) last_right = rs; else rs = last_right;
    const float lw = ls.x, lh = ls.y, rw = rs.x, rh = rs.y;
    static int logged = 0;  // research: the measured sizes
    if (std::getenv("BLOCKPLANET_HUD_LAYOUT_LOG") != nullptr && logged++ % 120 == 0)
        std::fprintf(stdout, "{\"event\":\"hud_layout\",\"lw\":%g,\"lh\":%g,\"rw\":%g,\"rh\":%g,\"w\":%g,\"h\":%g}\n", lw, lh, rw, rh, width, height);
    const float ph = hud.picture_h * height;
    // From the centre, keeping clear of the sphere (Follow the sphere only).
    const float clear = hud.distance_mode == 2 ? hud.sphere_radius * ph + margin : 0.0f;
    if (width >= height) {
        // Half a 4:3 frame's width; Screen edges: the window's.
        const float frame = hud.distance_mode == 1 ? width : ph * 2.0f / 3.0f;
        const float lx = std::max(margin, std::min(cx - frame + margin, cx - clear - lw));
        const float rx = std::min(width - margin - rw, std::max(cx + frame - margin - rw, cx + clear));
        const float ly = std::clamp(cy - lh / 2.0f, margin, std::max(margin, height - margin - lh));
        left->SetProperty("left", px(lx));
        left->SetProperty("top", px(ly));
        left_at = {lx, ly};
        if (std::getenv("BLOCKPLANET_HUD_LAYOUT_LOG") != nullptr && logged % 120 == 1)
            std::fprintf(stdout, "{\"event\":\"hud_place\",\"lx\":%g,\"ly\":%g,\"cy\":%g,\"lh\":%g}\n", lx, ly, cy, lh);
        right->SetProperty("left", px(rx));
        right->SetProperty("top", px(std::clamp(cy - rh / 2.0f, margin, std::max(margin, height - margin - rh))));
    } else {
        // Narrow or tall screens (the owner's choice): both rows at the bottom,
        // one above the other, each on one line, shrunk to fit the width.
        const float avail = width - 2.0f * margin;
        // The rows' whole width (their box stops at the window's edge).
        const float lfull = std::max(lw, left->GetScrollWidth()), rfull = std::max(rw, right->GetScrollWidth());
        const float sl = lfull > 0.0f ? std::min(1.0f, avail / lfull) : 1.0f;
        const float sr = rfull > 0.0f ? std::min(1.0f, avail / rfull) : 1.0f;
        const float gap = 10.0f * dp;
        const float ry = height - margin - rh * sr;
        const float ly = ry - gap - lh * sl;
        const float lx = std::max(margin, (width - lfull * sl) / 2.0f);
        const float rx = std::max(margin, (width - rfull * sr) / 2.0f);
        left->SetProperty("transform", "scale(" + std::to_string(sl) + ")");
        left->SetProperty("transform-origin", "0% 0%");
        right->SetProperty("transform", "scale(" + std::to_string(sr) + ")");
        right->SetProperty("transform-origin", "0% 0%");
        left->SetProperty("left", px(lx));
        left->SetProperty("top", px(ly));
        left_at = {lx, ly};
        hud_left_scale = sl;
        right->SetProperty("left", px(rx));
        right->SetProperty("top", px(ry));
    }
}

// Shows, updates or hides the HUD document. Returns whether it is shown.
std::mutex loading_mutex;
std::string loading_text;
float loading_opacity = 0.0f;
bool loading_wanted = false;  // the load still under way (set_loading)
bool loading_over_grid = false;  // shown over the grid as the loading screen, until it has faded out
Rml::ElementDocument* loading_document = nullptr;

std::string build_label;  // under loading_mutex
Rml::ElementDocument* label_document = nullptr;

bool update_label_document() {
    std::string text;
    {
        std::lock_guard lock(loading_mutex);
        text = build_label;
    }
    if (text.empty()) return false;
    if (label_document == nullptr) {
        label_document = context->LoadDocument(asset("build_label.rml"));
        if (label_document == nullptr) return false;
    }
    // The text changes with the settings (linux_main's update_build_label).
    static std::string shown;
    if (text != shown) {
        shown = text;
        if (auto* e = label_document->GetElementById("label")) e->SetInnerRML(escape(text));
    }
    label_document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    label_document->PullToFront();
    return true;
}

bool update_loading_document() {
    std::string text;
    float opacity;
    {
        std::lock_guard lock(loading_mutex);
        text = loading_text;
        opacity = loading_opacity;
    }
    // While the menu grid flies out into it, the cover waits for the tiles to
    // pass; once let through, it never dips (a grid held over it, as after a
    // restart from the pause, flies out behind it).
    static float allowed = 0.0f;
    allowed = opacity <= 0.0f ? 0.0f : std::max(allowed, grid ? grid->cover_allowance() : 1.0f);
    opacity *= allowed;
    if (opacity <= 0.0f) {
        if (loading_document != nullptr) loading_document->Hide();
        loading_over_grid = false;
        return false;
    }
    if (loading_document == nullptr) {
        loading_document = context->LoadDocument(asset("loading.rml"));
        if (loading_document == nullptr) return false;
    }
    if (auto* e = loading_document->GetElementById("loading-mode")) e->SetInnerRML(escape(text));
    // Over the grid as the loading screen: only the text, no dark cover, until
    // it has faded out (the grid flies out under it, revealing the game).
    if (grid && grid->visible() && !grid->leaving()) loading_over_grid = true;
    loading_document->SetClass("on-grid", loading_over_grid);
    loading_document->SetProperty("opacity", std::to_string(std::min(1.0f, opacity)));
    loading_document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    loading_document->PullToFront();
    return true;
}

bool update_hud_document() {
    std::optional<HudView> hud;
    {
        std::lock_guard lock(hud_mutex);
        hud = hud_wanted;
    }
    // Not under the menu grid (it covers the board while it is up or flying),
    // except the title's notice while the box art shows over the grid.
    const bool box_art = hud.has_value() && !hud->title_notice.empty() && tetrisphere::under_layer::wanted().load();
    const bool show = hud.has_value() && !navigator->is_open() && (box_art || !(grid && grid->visible()));
    if (!show) {
        if (hud_document != nullptr) hud_document->Hide();
        std::lock_guard lock(hud_mutex);
        hud_item.reset();
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
    // The HUD's panels and the game's text in layers of their own, so the
    // panels can fade out under the game's end menus while its text stays.
    const auto rml = hud->text_only ? std::string() : hud_rml_parts(*hud);
    if (rml != hud_shown_rml) {
        if (auto* root = hud_document->GetElementById("hud")) root->SetInnerRML(rml);
        hud_shown_rml = rml;
    }
    if (auto* root = hud_document->GetElementById("hud")) root->SetClass("ended", hud->end_menu);
    if (auto* notice = hud_document->GetElementById("title-notice"))
        notice->SetProperty("opacity", std::to_string(std::clamp(hud->title_opacity, 0.0f, 1.0f)));
    const auto text_rml = game_text_rml(*hud);
    if (text_rml != game_text_shown_rml) {
        if (auto* layer = hud_document->GetElementById("game-text")) layer->SetInnerRML(text_rml);
        game_text_shown_rml = text_rml;
    }
    set_hud_fills(*hud);
    place_popups(*hud);
    // The game's text fitted to the span the game gave it (so the game's own
    // alignment holds): each string stretched or narrowed to that width,
    // from its left edge (measured in the last layout; a transform does not
    // change it).
    {
        Rml::ElementList texts;
        hud_document->GetElementsByClassName(texts, "game-text");
        const float window_w = float(context->GetDimensions().x);
        for (auto* e : texts) {
            const float target = e->GetAttribute<float>("data-w", 0.0f) * window_w;
            const float actual = e->GetBox().GetSize().x;
            if (target <= 0.0f || actual <= 0.0f) continue;
            // Mixed case is narrower than the game's capitals: only a little
            // stretch, and the rest of the span split either side (centred in
            // it, so the game's layout still reads).
            const float scale = std::clamp(target / actual, 0.85f, 1.12f);
            const float shift = std::max(0.0f, (target - actual * scale) / 2.0f);
            e->SetProperty("transform", "translate(" + std::to_string(shift) + "px, 0px) scale(" + std::to_string(scale) + ", 1)");
            e->SetProperty("transform-origin", "0% 50%");
        }
    }
    const auto size = context->GetDimensions();
    Rml::Vector2f left_at{0, 0};
    place_hud_sides(*hud, float(size.x), float(size.y), left_at);
    if (!hud_document->IsVisible()) hud_document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    // Report where the item box is (from the last layout) for the magic's model.
    // The box's offset inside the left container, plus where the container was
    // just placed (absolute offsets lag behind the placement).
    auto* left_side = hud_document->GetElementById("left");
    if (auto* item = hud_document->GetElementById("item"); item != nullptr && left_side != nullptr) {
        // The item's place inside the left container, scaled as the container is drawn.
        Rml::Vector2f inside{0, 0};
        for (auto* e = item; e != nullptr && e != left_side; e = e->GetParentNode()) inside += e->GetRelativeOffset(Rml::BoxArea::Border);
        const Rml::Vector2f offset = left_at + inside * hud_left_scale;
        const auto item_size = item->GetBox().GetSize(Rml::BoxArea::Border) * hud_left_scale;
        if (item_size.x > 0 && item_size.y > 0) {  // not right after a rebuild
            std::lock_guard lock(hud_mutex);
            hud_item = std::array<float, 4>{offset.x, offset.y, item_size.x, item_size.y};
            hud_item_frame = {float(size.x), float(size.y)};
        }
    }
    return true;
}

// The menu grid behind the open screens, in the colour they ask for.
// The menu grid behind the open screens, in the colour they ask for. With
// no screen asking, the game's colour (set_grid_override), else it flies out
// (into the loading cover when one is wanted).
std::mutex grid_override_mutex;
std::string grid_override;
// The grid test screen (grid_lab.rml, for tuning): its colour, and whether
// the grid is shown (its Fly in and Fly out buttons).
std::string lab_theme = "blue";
bool lab_present = true;
std::string lab_show = "both";  // both, tiles or background
void update_grid() {
    const auto screens = navigator->documents();
    const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    std::string colour = grid_colour_of(screens);
    if (navigator->current_screen() == "grid_lab") colour = lab_present ? lab_theme : "";
    else if (colour.empty()) {
        std::lock_guard lock(grid_override_mutex);
        colour = grid_override;
    }
    bool cover = false, loading = false;
    {
        std::lock_guard lock(loading_mutex);
        cover = loading_opacity > 0.0f;
        loading = loading_wanted;
    }
    // The grid as the loading screen (the owner's design, 2026-11-09): while
    // a launch loads, a grid already up stays, in the colour it had, the
    // water moving; it flies out to reveal the game once the load is done.
    static std::string grid_shown_colour;
    if (colour.empty() && loading && grid->visible() && !grid->leaving()) colour = grid_shown_colour;
    if (!colour.empty()) grid_shown_colour = colour;
    const bool lab = navigator->current_screen() == "grid_lab";
    grid->set_backdrop(lab);
    grid->set_layers(!lab || lab_show != "background", !lab || lab_show != "tiles");
    // Flying out while a load is still wanted (the grid was not up as the
    // loading screen): into the cover; once the load is done: revealing the game.
    grid->update(grid_context, colour, now, loading);
    // Research: BLOCKPLANET_GRID_LOG=1 logs the grid's state every frame
    // while it is drawn (and the frame it goes), to tell its own dark frames
    // from the game's.
    static const bool grid_log = std::getenv("BLOCKPLANET_GRID_LOG") != nullptr;
    static bool was_visible = false;
    if (grid_log && (grid->visible() || was_visible))
        std::fprintf(stdout, "{\"event\":\"grid_frame\",\"t\":%.3f,\"asked\":\"%s\",\"visible\":%d,\"leaving\":%d,\"cover\":%d,\"presence\":%.2f,\"far\":%.2f}\n",
                     now, colour.c_str(), int(grid->visible()), int(grid->leaving()), int(cover), grid->presence(), grid->far_alpha());
    was_visible = grid->visible();
    for (auto* document : screens) document->SetClass("on-grid", grid->visible());
}

void hook_init(plume::RenderInterface* render_interface, plume::RenderDevice* device) {
    renderer = std::make_unique<Renderer>(render_interface, device);
}

// Once a frame, before either hook draws: requests, the grid, the HUD and the
// other layers, and RmlUi's update. Returns whether anything is to be drawn.
bool frame_prepared = false;   // this frame's preparation done (by the under hook)
bool grid_drawn_under = false; // the grid drawn under the game's picture this frame
bool frame_shown = false;

bool prepare_frame(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer, bool under) {
    if (!renderer || start_failed) return false;
    if (!started) {
        // Start RmlUi only once a menu (or the grid under the game) is wanted.
        std::lock_guard lock(requests_mutex);
        std::lock_guard hud_lock(hud_mutex);
        std::lock_guard grid_lock(grid_override_mutex);
        if (!under && !hud_wanted && grid_override.empty() &&
            std::none_of(requests.begin(), requests.end(),
                         [](const Request& r) { return std::holds_alternative<OpenRequest>(r) || std::holds_alternative<RestoreRequest>(r); }))
            return false;
    }
    const std::uint32_t width = framebuffer->getWidth();
    const std::uint32_t height = framebuffer->getHeight();
    renderer->begin_frame(list, framebuffer);
    if (!started && !start(width, height)) {
        start_failed = true;
        open_flag.store(false);
        renderer->end_frame();
        return false;
    }
    apply_requests();
    update_grid();
    const bool hud_visible = update_hud_document();
    const bool loading_visible = update_loading_document();
    const bool label_visible = update_label_document();
    // Research (BLOCKPLANET_GRID_LOG=1): every document drawn, with its
    // opacity, each frame while the grid is up and for 1.5 s after it goes.
    {
        static const bool grid_log = std::getenv("BLOCKPLANET_GRID_LOG") != nullptr;
        static double grid_gone_at = -1.0;
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (grid->visible()) grid_gone_at = -1.0;
        else if (grid_gone_at < 0.0) grid_gone_at = t;
        if (grid_log && (grid->visible() || t - grid_gone_at < 1.5)) {
            std::string shown;
            for (int i = 0; i < context->GetNumDocuments(); ++i) {
                auto* document = context->GetDocument(i);
                if (document == nullptr || !document->IsVisible()) continue;
                char entry[160];
                std::snprintf(entry, sizeof(entry), "%s{\"title\":\"%s\",\"opacity\":%.2f}", shown.empty() ? "" : ",",
                              document->GetTitle().c_str(), document->GetProperty<float>("opacity"));
                shown += entry;
            }
            std::fprintf(stdout, "{\"event\":\"ui_frame\",\"t\":%.3f,\"menus\":%d,\"hud\":%d,\"loading\":%d,\"label\":%d,\"grid\":%d,\"documents\":[%s]}\n",
                         t, int(navigator->is_open()), int(hud_visible), int(loading_visible), int(label_visible), int(grid->visible()), shown.c_str());
        }
    }
    frame_shown = navigator->is_open() || hud_visible || loading_visible || label_visible || grid->visible();
    if (frame_shown) {
        for (auto* each : {context, grid_context}) {
            if (each->GetDimensions() != Rml::Vector2i(int(width), int(height))) {
                each->SetDimensions(Rml::Vector2i(int(width), int(height)));
                each->SetDensityIndependentPixelRatio(dp_ratio(float(height)));
            }
        }
        grid_context->Update();
        context->Update();
        apply_card_scroll(float(height));
        // The credits scroll by themselves, about a fifteenth of the window a
        // second, pausing two seconds after Up or Down.
        if (auto* doc = navigator->top_document()) {
            if (Rml::Element* scroll = doc->GetElementById("credits-scroll")) {
                static auto last = std::chrono::steady_clock::now();
                const auto now = std::chrono::steady_clock::now();
                const float dt = std::min(0.1f, std::chrono::duration<float>(now - last).count());
                last = now;
                if (now - navigator->manual_scroll_at() > std::chrono::seconds(2))
                    scroll->SetScrollTop(scroll->GetScrollTop() + scroll->GetClientHeight() / 15.0f * dt);
            }
        }
    }
    return true;
}

// The under layer (called by RT64 before the game's picture while
// tetrisphere::under_layer::wanted() is set): the grid alone.
bool hook_under(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer) {
    // The game's picture (the box art) fades in over the grid each time the
    // layer starts (after half a second without it), over a second, eased.
    {
        static double started_at = 0.0, last_at = -10.0;
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now - last_at > 0.5) started_at = now;
        last_at = now;
        const float t = std::clamp(float((now - started_at) / 1.0), 0.0f, 1.0f);
        tetrisphere::under_layer::picture_fade().store(t * t * (3.0f - 2.0f * t), std::memory_order_relaxed);
    }
    frame_prepared = prepare_frame(list, framebuffer, true);
    if (!frame_prepared) return false;
    grid_drawn_under = frame_shown && grid->visible();
    if (grid_drawn_under) grid_context->Render();
    renderer->end_frame();
    return grid_drawn_under;
}

void hook_draw(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer) {
    const bool prepared = frame_prepared;
    const bool under = grid_drawn_under;
    frame_prepared = false;
    grid_drawn_under = false;
    if (prepared) renderer->begin_frame(list, framebuffer);
    else if (!prepare_frame(list, framebuffer, false)) return;
    if (frame_shown) {
        if (!under) grid_context->Render();  // behind the menus
        context->Render();
    }
    renderer->end_frame();
}

void hook_deinit() {
    hud_document = nullptr;
    grid.reset();
    navigator.reset();
    if (started) {
        if (context != nullptr) Rml::RemoveContext("main");
        if (grid_context != nullptr) Rml::RemoveContext("grid");
        context = nullptr;
        grid_context = nullptr;
        Rml::Shutdown();
    }
    renderer.reset();
}

} // namespace

void set_rom(std::span<const std::uint8_t> rom) { set_grid_rom(rom); }

namespace {

// The grid test screen (assets/ui/screens/grid_lab.rml; the owner's request,
// to tune the grid's motion by eye): its settings change grid_tuning at
// once, its buttons fly the grid in and out. Returns whether `command` was
// one of them. Each change is logged, so the values can be reported.
bool grid_lab_command(const std::string& command) {
    if (command == "grid_fly_in" || command == "grid_fly_out") {
        lab_present = command == "grid_fly_in";
        return true;
    }
    if (command.rfind("setting:grid_", 0) != 0) return false;
    const auto equals = command.find('=');
    if (equals == std::string::npos) return true;
    const std::string name = command.substr(8, equals - 8), value = command.substr(equals + 1);
    if (name == "grid_theme") lab_theme = value;
    else if (name == "grid_show") lab_show = value;
    else grid_lab_set(name, value);
    navigator->set_setting(name, value);
    // The values so far, for the owner to report.
    std::string all;
    for (const auto& [setting, shown] : grid_lab_values()) all += ",\"" + setting + "\":" + shown;
    std::fprintf(stdout, "{\"event\":\"grid_tuning\",\"theme\":\"%s\"%s}\n", lab_theme.c_str(), all.c_str());
    std::fflush(stdout);
    return true;
}

} // namespace

void set_grid_override(const std::string& colour) {
    std::lock_guard lock(grid_override_mutex);
    grid_override = colour;
}

void install(const std::filesystem::path& assets) {
    assets_path = assets;
    ui_asset_root() = assets;
    RT64::SetRenderHooks(hook_init, hook_draw, hook_deinit);
    tetrisphere::under_layer::draw_function().store(hook_under);
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

std::vector<std::pair<std::string, std::size_t>> menu_path() {
    std::lock_guard lock(requests_mutex);
    return shown_path;
}

void request_restore(const std::vector<std::pair<std::string, std::size_t>>& path) {
    std::lock_guard lock(requests_mutex);
    requests.push_back(RestoreRequest{path});
}

void set_build_label(const std::string& text) {
    std::lock_guard lock(loading_mutex);
    build_label = text;
}

void set_loading(const std::string& text, float opacity, bool wanted) {
    std::lock_guard lock(loading_mutex);
    loading_text = text;
    loading_opacity = opacity;
    loading_wanted = wanted;
}

void set_text(const std::string& name, const std::string& text) {
    std::lock_guard lock(requests_mutex);
    requests.push_back(TextRequest{name, text});
}

void set_button_text(const std::string& name, const std::string& text) {
    std::lock_guard lock(requests_mutex);
    requests.push_back(TextRequest{name, text, true});
}

void set_alt_button(const std::string& spec) {
    std::lock_guard lock(requests_mutex);
    requests.push_back(AltButtonRequest{spec});
}

void set_pad(bool pad) {
    std::lock_guard lock(requests_mutex);
    requests.push_back(PadRequest{pad});
}

void send_command(const std::string& command) {
    std::lock_guard lock(requests_mutex);
    commands.push_back(command);
}

std::string screen_title(const std::string& screen) {
    if (screen.empty() || screen.find_first_of("/\\.") != std::string::npos) return {};
    std::ifstream file(assets_path / "screens" / (screen + ".rml"));
    if (!file) return {};
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto open = text.find("<title>");
    const auto close = open == std::string::npos ? open : text.find("</title>", open);
    if (close == std::string::npos) return {};
    std::string title = text.substr(open + 7, close - open - 7);
    // Title Case: each word capitalised, but for short joining words.
    bool start = true, first = true;
    std::string word;
    std::string out;
    const auto flush = [&] {
        static const char* joining_words[] = {"a", "an", "and", "as", "at", "by", "for", "in", "of", "on", "or", "the", "to"};
        bool keep = !first && std::any_of(std::begin(joining_words), std::end(joining_words), [&](const char* w) { return word == w; });
        if (!word.empty() && !keep) word[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
        out += word;
        word.clear();
        first = false;
    };
    for (char ch : title) {
        if (ch == ' ') {
            flush();
            out += ' ';
        } else {
            word += ch;
        }
    }
    flush();
    (void)start;
    return out;
}

std::string exit_to_label(const std::string& screen) {
    const std::string place = screen_title(screen.empty() ? "main" : screen);
    return place.empty() ? "Exit" : "Exit to " + place;
}

std::string current_screen() {
    std::lock_guard lock(requests_mutex);
    return open_flag.load() ? shown_screen : std::string();
}

void set_hud(std::optional<HudView> hud) {
    std::lock_guard lock(hud_mutex);
    hud_wanted = std::move(hud);
}

std::optional<HudBox> hud_item_box() {
    std::lock_guard lock(hud_mutex);
    if (!hud_item) return std::nullopt;
    return HudBox{(*hud_item)[0], (*hud_item)[1], (*hud_item)[2], (*hud_item)[3], hud_item_frame[0], hud_item_frame[1]};
}

bool menus_open() { return open_flag.load(); }

bool card_needs_scroll() { return card_needs_scroll_flag.load(); }

void scroll_card(float lines) {
    std::lock_guard lock(card_scroll_mutex);
    card_scroll_lines += lines;
}

std::optional<std::string> take_command() {
    std::lock_guard lock(requests_mutex);
    if (commands.empty()) return std::nullopt;
    auto command = std::move(commands.front());
    commands.pop_front();
    return command;
}

} // namespace blockplanet::ui
