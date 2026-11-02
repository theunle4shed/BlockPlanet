#pragma once

// Screens and focus for BlockPlanet's menus (docs/UI.md). Works on an RmlUi
// context only, so it can be tested without RT64.
//
// A screen is an RML document, ui/screens/<name>.rml. Elements with the
// class "item" can take focus; the focused one gets the class "focused".
// An item's data-action says what Confirm does:
//   open:NAME     show screen NAME on top
//   back          go back one screen
//   close         close the menus
//   command:NAME  report NAME to the game (take_command)
//
// Setting rows: class "item value", data-setting="NAME" and
// data-options="value:Label,value:Label". Left and Right (or Confirm) cycle
// the value, show its label in a child with class "value", and report
// "setting:NAME=VALUE" as a command. Current values come from set_setting.
//
// Lists: an element with data-list="NAME" is filled with the items given to
// set_list when its screen opens.

#include "ui_actions.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include <deque>
#include <map>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace blockplanet::ui {

class Navigator {
public:
    Navigator(Rml::Context* context, std::filesystem::path screens);

    // Opens the menus at `screen` (closing any open screens first).
    bool open(const std::string& screen);
    void close();
    bool is_open() const { return !stack_.empty(); }

    void handle(Action action);

    // Current setting values and list contents, used when screens open
    // (and applied at once to the screen showing).
    void set_setting(const std::string& name, const std::string& value);
    void set_list(const std::string& name, std::vector<ListItem> items);

    // Commands chosen by the player (command:NAME), oldest first.
    std::optional<std::string> take_command();

    // For tests and logging.
    std::string current_screen() const;
    std::string focused_item() const;  // the focused item's id

private:
    struct Screen {
        std::string name;
        Rml::ElementDocument* document;
        std::size_t focus;
    };

    bool push(const std::string& screen);
    void pop();
    std::vector<Rml::Element*> items(const Screen& screen) const;
    void set_focus(Screen& screen, std::size_t index);
    void activate(Screen& screen);
    void change_value(Screen& screen, int direction);
    void fill(Screen& screen);

    Rml::Context* context_;
    std::filesystem::path screens_;
    std::vector<Screen> stack_;
    std::deque<std::string> commands_;
    std::map<std::string, std::string> settings_;
    std::map<std::string, std::vector<ListItem>> lists_;
};

} // namespace blockplanet::ui
