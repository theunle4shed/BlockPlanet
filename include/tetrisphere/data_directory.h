#pragma once

#include <filesystem>

namespace tetrisphere {

// The folder holding the executable.
std::filesystem::path executable_directory();

// Where Blocksphere and earlier BlockPlanet builds kept their data: `data`
// beside the executable. Also the portable data folder.
std::filesystem::path legacy_data_directory();

// Portable mode: a file named portable.txt beside the executable keeps all
// data in the `data` folder there, as Blocksphere did (for USB sticks).
bool portable_mode();

// Where BlockPlanet keeps settings, saves and caches:
// 1. TETRISPHERE_DATA_DIR, if set (isolated QA sessions).
// 2. Portable mode: `data` beside the executable.
// 3. Windows: C:\ProgramData\BlockPlanet, shared by every account on the PC
//    (created writable for all users), or the account's own
//    %APPDATA%\BlockPlanet if that cannot be written.
// 4. Linux: $XDG_DATA_HOME/BlockPlanet (usually ~/.local/share/BlockPlanet);
//    a folder shared by all accounts is not normally writable there.
std::filesystem::path application_data_directory();

} // namespace tetrisphere
