#include "ui_navigator.h"

#include <RmlUi/Core/Element.h>

#include <algorithm>

namespace blockplanet::ui {
namespace {

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
    stack_.push_back({screen, document, 0});
    fill(stack_.back());
    document->SetClass("entering", true);  // CSS animates screens with this class
    document->Show();
    set_focus(stack_.back(), 0);
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
}

void Navigator::fill(Screen& screen) {
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
            Rml::String text = entry.label;
            if (!entry.note.empty()) text += "<span class=\"note\">" + entry.note + "</span>";
            element->SetInnerRML(text);
            container->AppendChild(std::move(element));
        }
    }
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
        if (!shown.empty()) shown.front()->SetInnerRML(label);
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
    index = (index + options.size() + (direction < 0 ? options.size() - 1 : 1)) % options.size();
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
}

void Navigator::activate(Screen& screen) {
    const auto list = items(screen);
    if (list.empty()) return;
    if (list[screen.focus]->IsClassSet("value")) {
        change_value(screen, 1);
        return;
    }
    const std::string action = list[screen.focus]->GetAttribute<Rml::String>("data-action", "");
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
        if (count != 0) set_focus(screen, (screen.focus + count - 1) % count);
        break;
    case Action::Down:
        if (count != 0) set_focus(screen, (screen.focus + 1) % count);
        break;
    case Action::Left:
        change_value(screen, -1);
        break;
    case Action::Right:
        change_value(screen, 1);
        break;
    case Action::Confirm:
        activate(screen);
        break;
    case Action::Back:
        pop();
        break;
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
