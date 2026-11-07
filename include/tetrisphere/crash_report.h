#pragma once

// Crash reports (the owner's choice: a crash log the player can send). When
// BlockPlanet crashes, a text report is written beside the session logs
// (<data folder>/diagnostics/crash-<time>-<run>.txt): what went wrong, the
// stack (addresses, with names where the system gives them), the build, and
// what the game was doing (set_crash_context). At the next start BlockPlanet
// tells the player where it is, once.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace tetrisphere {

// Installs the handlers (Windows: an unhandled-exception filter; elsewhere:
// signal handlers for SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT), and
// std::terminate. `directory` must exist.
void install_crash_handler(const std::filesystem::path& directory, const std::string& run_id, const std::string& build_id);

// What the game is doing, for the next report (copied; cheap; call each frame).
void set_crash_context(const std::string& context);

// Crash reports in `directory` not yet shown to the player, oldest first; marks
// them shown (crashes-shown.txt in the same folder).
std::vector<std::filesystem::path> take_new_crash_reports(const std::filesystem::path& directory);

} // namespace tetrisphere
