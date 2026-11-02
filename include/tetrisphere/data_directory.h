#pragma once

#include <SDL.h>
#include <cstdlib>
#include <filesystem>

namespace tetrisphere {

// The folder holding the executable.
inline std::filesystem::path executable_directory() {
    char* base = SDL_GetBasePath();
    if (base == nullptr) return {};
    auto directory = std::filesystem::u8path(base);
    SDL_free(base);
    directory = directory.lexically_normal();
    if (directory.filename().empty()) directory = directory.parent_path();
    return directory;
}

// Where Blocksphere and earlier BlockPlanet builds kept their data: `data`
// beside the executable. Also the portable data folder.
inline std::filesystem::path legacy_data_directory() {
    const auto directory = executable_directory();
    return directory.empty() ? directory : directory / "data";
}

// Portable mode: a file named portable.txt beside the executable keeps all
// data in the `data` folder there, as Blocksphere did (for USB sticks).
inline bool portable_mode() {
    const auto directory = executable_directory();
    std::error_code error;
    return !directory.empty() && std::filesystem::exists(directory / "portable.txt", error);
}

// Where BlockPlanet keeps settings, saves and caches:
// 1. TETRISPHERE_DATA_DIR, if set (isolated QA sessions).
// 2. Portable mode: `data` beside the executable.
// 3. Otherwise the standard per-user folder: %APPDATA%\BlockPlanet on
//    Windows, $XDG_DATA_HOME/BlockPlanet (usually ~/.local/share/BlockPlanet)
//    on Linux.
inline std::filesystem::path application_data_directory() {
    if (const char* override = std::getenv("TETRISPHERE_DATA_DIR");
        override != nullptr && *override != '\0') {
        return std::filesystem::u8path(override);
    }
    if (portable_mode()) return legacy_data_directory();
    char* preferences = SDL_GetPrefPath("", "BlockPlanet");
    if (preferences == nullptr) return {};
    auto directory = std::filesystem::u8path(preferences);
    SDL_free(preferences);
    directory = directory.lexically_normal();
    if (directory.filename().empty()) directory = directory.parent_path();
    return directory;
}

} // namespace tetrisphere
