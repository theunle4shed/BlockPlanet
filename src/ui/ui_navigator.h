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

#include "ui_actions.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include <deque>
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

    Rml::Context* context_;
    std::filesystem::path screens_;
    std::vector<Screen> stack_;
    std::deque<std::string> commands_;
};

} // namespace blockplanet::ui
