#include "tetrisphere/relaunch.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <filesystem>

namespace tetrisphere {
namespace {

std::vector<std::string> remembered;

#ifdef _WIN32
// Quotes one argument for CommandLineToArgvW rules.
std::wstring quoted(const std::wstring& argument) {
    std::wstring result = L"\"";
    int backslashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') result.append(std::size_t(backslashes) * 2 + 1, L'\\');
        else result.append(std::size_t(backslashes), L'\\');
        backslashes = 0;
        result += c;
    }
    result.append(std::size_t(backslashes) * 2, L'\\');
    return result + L"\"";
}
#endif

} // namespace

void set_launch_arguments(int argc, const char* const argv[]) {
    remembered.clear();
    for (int i = 1; i < argc; ++i) remembered.emplace_back(argv[i]);
}

const std::vector<std::string>& launch_arguments() { return remembered; }

std::vector<std::string> relaunch_arguments(const std::vector<std::string>& original,
                                            const std::optional<std::string>& profile, bool guest) {
    std::vector<std::string> result;
    for (const auto& argument : original) {
        if (argument == "--guest" || argument == "--credits" || argument.rfind("--profile=", 0) == 0) continue;
        result.push_back(argument);
    }
    if (guest) result.insert(result.begin(), "--guest");
    else if (profile) result.insert(result.begin(), "--profile=" + *profile);
    return result;
}

bool relaunch(const std::vector<std::string>& arguments) {
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) return false;
    std::wstring command_line = quoted(path);
    for (const auto& argument : arguments) command_line += L" " + quoted(std::filesystem::u8path(argument).wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(path, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
        return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    std::error_code error;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) return false;
    std::vector<std::string> owned{self.string()};
    owned.insert(owned.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (auto& argument : owned) argv.push_back(argument.data());
    argv.push_back(nullptr);
    execv(owned.front().c_str(), argv.data());
    return false;  // only reached if execv failed
#endif
}

} // namespace tetrisphere
