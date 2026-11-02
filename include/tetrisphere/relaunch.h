#pragma once

// Restarting BlockPlanet, for switching profiles: the game reads its
// cartridge save while booting, so a different profile needs a fresh start.

#include <optional>
#include <string>
#include <vector>

namespace tetrisphere {

// Remembers the command line (call once from main).
void set_launch_arguments(int argc, const char* const argv[]);

// The original arguments without --profile and --guest, plus the new choice.
std::vector<std::string> relaunch_arguments(const std::vector<std::string>& original,
                                            const std::optional<std::string>& profile, bool guest);
const std::vector<std::string>& launch_arguments();

// Starts the game again with `arguments`. On Linux this replaces the current
// process; on Windows it starts a new one and the caller should exit.
// Returns false if it could not.
bool relaunch(const std::vector<std::string>& arguments);

} // namespace tetrisphere
