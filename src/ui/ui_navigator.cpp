#include "ui_navigator.h"
#include "tetrisphere/version.h"

#include <RmlUi/Core/Element.h>

#include <algorithm>

namespace blockplanet::ui {
namespace {

// Text shown as is (profile names may contain < or &).
Rml::String escaped(const std::string& text) {
    Rml::String result;
    for (char c : text) {
        if (c == '&') result += "&amp;";
        else if (c == '<') result += "&lt;";
        else if (c == '>') result += "&gt;";
        else result += c;
    }
    return result;
}

// "value:Label,value:Label" as pairs.
std::vector<std::pair<std::string, std::string>> parse_options(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> result;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find(',', start);
        const auto entry = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const auto colon = entry.find(':');
        if (!entry.empty())
            result.emplace_back(entry.substr(0, colon), colon == std::string::npos ? entry : entry.substr(colon + 1));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

void collect_lists(Rml::Element* element, std::vector<Rml::Element*>& out) {
    for (int i = 0; i < element->GetNumChildren(); ++i) {
        Rml::Element* child = element->GetChild(i);
        if (child->HasAttribute("data-list")) out.push_back(child);
        collect_lists(child, out);
    }
}

void collect_items(Rml::Element* element, std::vector<Rml::Element*>& out) {
    for (int i = 0; i < element->GetNumChildren(); ++i) {
        Rml::Element* child = element->GetChild(i);
        if (child->IsClassSet("item") && !child->IsClassSet("disabled")) out.push_back(child);
        collect_items(child, out);
    }
}

} // namespace

Navigator::Navigator(Rml::Context* context, std::filesystem::path screens)
    : context_(context), screens_(std::move(screens)) {}

bool Navigator::open(const std::string& screen) {
    close();
    return push(screen);
}

void Navigator::close() {
    while (!stack_.empty()) pop();
}

bool Navigator::push(const std::string& screen) {
    Rml::ElementDocument* document = context_->LoadDocument((screens_ / (screen + ".rml")).string());
    if (document == nullptr) return false;
    if (!stack_.empty()) stack_.back().document->Hide();
    document->SetClass("pad", pad_);
    stack_.push_back({screen, document, 0});
    fill(stack_.back());
    document->SetClass("entering", true);  // CSS animates screens with this class
    document->Show();
    // The first item, or a list item marked to be focused.
    std::size_t first = 0;
    const auto list = items(stack_.back());
    for (std::size_t i = 0; i < list.size(); ++i)
        if (list[i]->HasAttribute("data-focus")) {
            first = i;
            break;
        }
    set_focus(stack_.back(), first);
    return true;
}

void Navigator::pop() {
    if (stack_.empty()) return;
    stack_.back().document->Close();
    stack_.pop_back();
    if (!stack_.empty()) {
        auto& previous = stack_.back();
        previous.document->SetClass("entering", false);
        previous.document->SetClass("returning", true);
        previous.document->Show();
        set_focus(previous, previous.focus);
    }
}

std::vector<Rml::Element*> Navigator::items(const Screen& screen) const {
    std::vector<Rml::Element*> result;
    collect_items(screen.document, result);
    return result;
}

void Navigator::set_focus(Screen& screen, std::size_t index) {
    const auto list = items(screen);
    for (auto* item : list) item->SetClass("focused", false);
    if (list.empty()) return;
    screen.focus = std::min(index, list.size() - 1);
    list[screen.focus]->SetClass("focused", true);
    list[screen.focus]->ScrollIntoView(false);
    // What the focused item is: its data-description, in the screen's
    // #description (menus explain each mode and sub-mode).
    if (Rml::Element* description = screen.document->GetElementById("description")) {
        const auto text = list[screen.focus]->GetAttribute<Rml::String>("data-description", "");
        const auto alt = list[screen.focus]->GetAttribute<Rml::String>("data-alt-label", "");
        std::string rml = escaped(text);
        if (!alt.empty())
            rml += "<div class='alt-hint'>" + alt_button_ + "<span> " + escaped(alt) + "</span></div>";
        description->SetInnerRML(rml);
        description->SetClass("empty", text.empty() && alt.empty());
    }
}

void Navigator::fill(Screen& screen) {
    // BlockPlanet's name and version, on screens that show it (the main menu).
    if (Rml::Element* version = screen.document->GetElementById("version"))
        version->SetInnerRML(std::string("BlockPlanet v") + blockplanet::kVersion);
    // Lists first, so their items get values and focus like any other.
    std::vector<Rml::Element*> lists;
    collect_lists(screen.document, lists);
    for (Rml::Element* container : lists) {
        while (container->GetNumChildren() > 0) container->RemoveChild(container->GetFirstChild());
        const auto found = lists_.find(container->GetAttribute<Rml::String>("data-list", ""));
        if (found == lists_.end()) continue;
        for (const auto& entry : found->second) {
            auto element = container->GetOwnerDocument()->CreateElement("div");
            element->SetId(entry.id);
            element->SetClass("item", true);
            element->SetClass("disabled", !entry.enabled);
            element->SetAttribute("data-action", entry.action);
            if (entry.focus) element->SetAttribute("data-focus", true);
            if (!entry.description.empty()) element->SetAttribute("data-description", entry.description);
            if (entry.span > 1) {
                element->SetAttribute("data-span", entry.span);
                element->SetClass("span" + std::to_string(entry.span), true);
            }
            Rml::String text = escaped(entry.label);
            if (!entry.note.empty()) text += "<span class=\"note\">" + escaped(entry.note) + "</span>";
            element->SetInnerRML(text);
            container->AppendChild(std::move(element));
        }
    }
    fill_text(screen);
    for (Rml::Element* item : items(screen)) {
        if (!item->IsClassSet("value")) continue;
        const auto name = item->GetAttribute<Rml::String>("data-setting", "");
        const auto options = parse_options(item->GetAttribute<Rml::String>("data-options", ""));
        const auto current = settings_.find(name);
        std::string label;
        for (const auto& [value, text] : options) {
            if (current != settings_.end() && current->second == value) label = text;
        }
        Rml::ElementList shown;
        item->GetElementsByClassName(shown, "value");
        if (!shown.empty()) shown.front()->SetInnerRML(escaped(label));
        // Sliders: a bar filled to the value's position among the options.
        Rml::ElementList fills;
        item->GetElementsByClassName(fills, "fill");
        if (!fills.empty() && options.size() > 1) {
            std::size_t index = 0;
            for (std::size_t i = 0; i < options.size(); ++i)
                if (current != settings_.end() && current->second == options[i].first) index = i;
            fills.front()->SetProperty("width", std::to_string(100.0 * index / (options.size() - 1)) + "%");
        }
    }
}

void Navigator::change_value(Screen& screen, int direction) {
    const auto list = items(screen);
    if (list.empty() || !list[screen.focus]->IsClassSet("value")) return;
    Rml::Element* item = list[screen.focus];
    const auto name = item->GetAttribute<Rml::String>("data-setting", "");
    const auto options = parse_options(item->GetAttribute<Rml::String>("data-options", ""));
    if (name.empty() || options.empty()) return;
    std::size_t index = 0;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (settings_[name] == options[i].first) index = i;
    }
    if (item->IsClassSet("slider")) {  // sliders stop at their ends
        if (direction < 0 && index == 0) return;
        if (direction > 0 && index + 1 == options.size()) return;
        index = direction < 0 ? index - 1 : index + 1;
    } else {
        index = (index + options.size() + (direction < 0 ? options.size() - 1 : 1)) % options.size();
    }
    settings_[name] = options[index].first;
    commands_.push_back("setting:" + name + "=" + options[index].first);
    fill(screen);
    set_focus(screen, screen.focus);
}

void Navigator::set_setting(const std::string& name, const std::string& value) {
    settings_[name] = value;
    if (!stack_.empty()) {
        fill(stack_.back());
        set_focus(stack_.back(), stack_.back().focus);
    }
}

void Navigator::set_list(const std::string& name, std::vector<ListItem> items) {
    lists_[name] = std::move(items);
    // Refill open screens showing this list (they may be under others).
    for (auto& screen : stack_) {
        std::vector<Rml::Element*> lists;
        collect_lists(screen.document, lists);
        bool shows = false;
        for (Rml::Element* container : lists)
            shows = shows || container->GetAttribute<Rml::String>("data-list", "") == name;
        if (!shows) continue;
        fill(screen);
        const auto count = this->items(screen).size();
        set_focus(screen, count == 0 ? 0 : std::min(screen.focus, count - 1));
    }
}

void Navigator::set_alt_button(const std::string& rml) {
    alt_button_ = rml;
    if (!stack_.empty()) set_focus(stack_.back(), stack_.back().focus);  // refresh the hint
}

void Navigator::activate(Screen& screen, const char* attribute) {
    const auto list = items(screen);
    if (list.empty()) return;
    const bool alt = std::string(attribute) != "data-action";
    if (!alt && list[screen.focus]->IsClassSet("value")) {
        change_value(screen, 1);
        return;
    }
    const std::string action = list[screen.focus]->GetAttribute<Rml::String>(attribute, "");
    if (action.rfind("open:", 0) == 0) {
        push(action.substr(5));
    } else if (action == "back") {
        pop();
    } else if (action == "close") {
        close();
    } else if (action.rfind("command:", 0) == 0) {
        commands_.push_back(action.substr(8));
    }
}

void Navigator::handle(Action action) {
    if (stack_.empty()) return;
    auto& screen = stack_.back();
    const auto count = items(screen).size();
    switch (action) {
    case Action::Up:
    case Action::Down:
        // A scrolling text (the credits): Up and Down move it.
        if (Rml::Element* scroll = screen.document->GetElementById("credits-scroll")) {
            const float step = scroll->GetClientHeight() * 0.25f;
            scroll->SetScrollTop(scroll->GetScrollTop() + (action == Action::Down ? step : -step));
            manual_scroll_at_ = std::chrono::steady_clock::now();
            break;
        }
        if (count != 0) move(screen, action);
        break;
    case Action::Left:
    case Action::Right:
        if (count != 0) move(screen, action);
        break;
    case Action::Confirm:
        activate(screen);
        break;
    case Action::Back:
        // On the first screen, the document's data-back (a command) when it
        // has one: the start-up screens go back through each other.
        if (stack_.size() == 1 && screen.document->HasAttribute("data-back")) {
            const auto back = screen.document->GetAttribute<Rml::String>("data-back", "");
            if (back.rfind("command:", 0) == 0) commands_.push_back(back.substr(8));
            break;
        }
        pop();
        break;
    case Action::Alt:
        activate(screen, "data-alt");
        break;
    }
}

bool Navigator::grid(const Screen& screen, Grid& out) const {
    const auto list = items(screen);
    if (list.empty()) return false;
    Rml::Element* container = list[screen.focus]->GetParentNode();
    if (container == nullptr || !container->HasAttribute("data-columns")) return false;
    out.columns = std::max(1, container->GetAttribute<int>("data-columns", 1));
    out.wrap = container->HasAttribute("data-wrap");
    out.first = screen.focus;
    while (out.first > 0 && list[out.first - 1]->GetParentNode() == container) --out.first;
    out.size = 0;
    while (out.first + out.size < list.size() && list[out.first + out.size]->GetParentNode() == container) ++out.size;
    int row = 0, column = 0;
    for (std::size_t i = 0; i < out.size; ++i) {
        const int span = std::clamp(list[out.first + i]->GetAttribute<int>("data-span", 1), 1, out.columns);
        if (column + span > out.columns) {
            ++row;
            column = 0;
        }
        out.row.push_back(row);
        out.start.push_back(column);
        out.span.push_back(span);
        column += span;
    }
    out.rows = row + 1;
    return true;
}

void Navigator::move(Screen& screen, Action action) {
    const std::size_t count = items(screen).size();
    Grid g;
    const bool in_grid = grid(screen, g);
    const bool horizontal = action == Action::Left || action == Action::Right;
    if (horizontal && !in_grid) {
        change_value(screen, action == Action::Left ? -1 : 1);
        return;
    }
    const bool up = action == Action::Up || action == Action::Left;
    if (!in_grid) {
        screen.column = -1;
        set_focus(screen, up ? (screen.focus + count - 1) % count : (screen.focus + 1) % count);
        return;
    }
    const std::size_t here = screen.focus - g.first;
    const int row = g.row[here];
    if (horizontal) {
        // Along the row, wrapping at its ends.
        std::size_t first = here, last = here;
        while (first > 0 && g.row[first - 1] == row) --first;
        while (last + 1 < g.size && g.row[last + 1] == row) ++last;
        const std::size_t next = action == Action::Left ? (here == first ? last : here - 1)
                                                        : (here == last ? first : here + 1);
        screen.column = -1;
        set_focus(screen, g.first + next);
        return;
    }
    if (screen.column < g.start[here] || screen.column >= g.start[here] + g.span[here]) screen.column = g.start[here];
    int target = up ? row - 1 : row + 1;
    if (target < 0 || target >= g.rows) {
        if (!g.wrap) {
            // Leave the grid: to the item before it, or after it.
            screen.column = -1;
            set_focus(screen, up ? (g.first + count - 1) % count : (g.first + g.size) % count);
            return;
        }
        target = (target + g.rows) % g.rows;
    }
    // The item in the target row covering the column, or the row's last
    // item if the row is shorter.
    std::size_t best = g.size;
    for (std::size_t i = 0; i < g.size; ++i) {
        if (g.row[i] != target) continue;
        if (g.start[i] <= screen.column) best = i;
    }
    if (best == g.size)
        for (std::size_t i = 0; i < g.size; ++i)
            if (g.row[i] == target) { best = i; break; }
    const int column = screen.column;
    set_focus(screen, g.first + best);
    screen.column = column;
}

void Navigator::set_pad(bool pad) {
    pad_ = pad;
    for (auto& screen : stack_) screen.document->SetClass("pad", pad);
}

void Navigator::set_text(const std::string& name, const std::string& text) {
    texts_[name] = text;
    for (auto& screen : stack_) fill_text(screen);
}

void Navigator::set_rml(const std::string& name, const std::string& rml) {
    rmls_[name] = rml;
    for (auto& screen : stack_) fill_text(screen);
}

void Navigator::fill_text(Screen& screen) {
    {
        Rml::ElementList with_rml;
        screen.document->QuerySelectorAll(with_rml, "[data-buttons]");
        for (Rml::Element* element : with_rml) {
            const auto found = rmls_.find(element->GetAttribute<Rml::String>("data-buttons", ""));
            element->SetInnerRML(found == rmls_.end() ? Rml::String() : found->second);
        }
    }
    Rml::ElementList all;
    screen.document->QuerySelectorAll(all, "[data-text]");
    for (Rml::Element* element : all) {
        const auto found = texts_.find(element->GetAttribute<Rml::String>("data-text", ""));
        const Rml::String text = found == texts_.end() ? Rml::String() : found->second;
        if (element->HasAttribute("data-credits")) {
            // The credits: "#Role" a heading, ">text" a closing line, "" a gap.
            Rml::String rml;
            std::size_t at = 0;
            while (at < text.size()) {
                const std::size_t end = std::min(text.find('\n', at), text.size());
                const Rml::String line = text.substr(at, end - at);
                if (line.empty()) rml += "<div class='credit-gap'/>";
                else if (line[0] == '#') rml += "<div class='credit-role'>" + escaped(line.substr(1)) + "</div>";
                else if (line[0] == '>') rml += "<div class='credit-closing'>" + escaped(line.substr(1)) + "</div>";
                else rml += "<div class='credit-name'>" + escaped(line) + "</div>";
                at = end + 1;
            }
            element->SetInnerRML(rml);
            continue;
        }
        if (element->HasAttribute("data-paragraphs")) {
            // One paragraph per line.
            Rml::String rml;
            std::size_t at = 0;
            while (at <= text.size()) {
                const std::size_t end = std::min(text.find('\n', at), text.size());
                if (end > at) rml += "<p class='para'>" + escaped(text.substr(at, end - at)) + "</p>";
                at = end + 1;
            }
            element->SetInnerRML(rml);
        } else {
            element->SetInnerRML(escaped(text));
        }
    }
}

std::optional<std::string> Navigator::take_command() {
    if (commands_.empty()) return std::nullopt;
    auto command = std::move(commands_.front());
    commands_.pop_front();
    return command;
}

std::string Navigator::current_screen() const { return stack_.empty() ? std::string{} : stack_.back().name; }

std::string Navigator::focused_item() const {
    if (stack_.empty()) return {};
    const auto list = items(stack_.back());
    return list.empty() ? std::string{} : list[stack_.back().focus]->GetId();
}

} // namespace blockplanet::ui
