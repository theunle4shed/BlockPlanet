#include "ui_navigator.h"

#include <RmlUi/Core/Element.h>

#include <algorithm>

namespace blockplanet::ui {
namespace {

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

void Navigator::activate(Screen& screen) {
    const auto list = items(screen);
    if (list.empty()) return;
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
    case Action::Right:
        break;  // for value items, later
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
