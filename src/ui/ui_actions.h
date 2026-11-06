#pragma once

#include <string>

namespace blockplanet::ui {

// Menu navigation, from any input device.
enum class Action { Up, Down, Left, Right, Confirm, Back };

// An item of a list the game fills in (data-list in a screen).
struct ListItem {
    std::string id;
    std::string label;
    std::string note;    // small text after the label
    std::string action;  // data-action
    bool enabled = true;
    int span = 1;         // columns taken in a grid (data-columns)
    bool focus = false;   // focused when the screen opens
    std::string description;  // shown in the screen's #description while focused
};

} // namespace blockplanet::ui
