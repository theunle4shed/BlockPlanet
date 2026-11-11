#pragma once

#include <string>

namespace blockplanet::ui {

// Menu navigation, from any input device.
// Alt is the second button (X on most pads, T on keyboard): an item's
// data-alt action, such as Training on Story.
enum class Action { Up, Down, Left, Right, Confirm, Back, Alt };

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
    std::string alt;          // data-alt: what the Alt button does on this item
    std::string icon;         // an icon before the label (a character of the menus' icon font, UTF-8)
    std::string image;        // a picture before the label ("/blockplanet-ui/coins/bolt.png" from the menu folder, or a path from the screens folder)
    std::string image_colour; // its tint (#rrggbb, multiplied), or empty
    std::string classes;      // extra classes for the item (space-separated), for its look
    std::string alt_label;    // data-alt-label: the Alt button's hint on this item
};

} // namespace blockplanet::ui
