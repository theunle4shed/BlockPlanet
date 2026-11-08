#pragma once

// BlockPlanet's rewrites of the game's text (the owner's review), looked up by
// a fingerprint of the original, so the game's text is not kept in the source.

#include <cstdint>
#include <optional>
#include <string>

namespace tetrisphere {

// FNV-1a (64-bit) of the text's bytes, as the game holds them (capitals and codes).
std::uint64_t text_fingerprint(const std::string& text);

// The rewrite for one page of the game's text (Training's pages, a card's
// whole text, a hint line), if the owner's review gave one.
std::optional<std::string> text_rewrite(const std::string& original);

}  // namespace tetrisphere
