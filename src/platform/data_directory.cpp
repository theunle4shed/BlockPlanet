#include "tetrisphere/data_directory.h"

#include <SDL.h>

#include <cstdlib>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <aclapi.h>
#include <shlobj.h>
#endif

namespace tetrisphere {
namespace {

std::filesystem::path normalized(std::filesystem::path directory) {
    directory = directory.lexically_normal();
    if (directory.filename().empty()) directory = directory.parent_path();
    return directory;
}

// True if a file can be created and removed in `directory`.
bool writable(const std::filesystem::path& directory) {
    const auto probe = directory / ".blockplanet-write-test";
    {
        std::ofstream out(probe, std::ios::binary | std::ios::trunc);
        if (!out) return false;
    }
    std::error_code code;
    std::filesystem::remove(probe, code);
    return true;
}

// The account's own folder (SDL creates it).
std::filesystem::path per_user_directory() {
    char* preferences = SDL_GetPrefPath("", "BlockPlanet");
    if (preferences == nullptr) return {};
    auto directory = std::filesystem::u8path(preferences);
    SDL_free(preferences);
    return normalized(directory);
}

#ifdef _WIN32
// Gives the built-in Users group modify rights on `directory`, inherited by
// everything created inside it. Without this, files one account creates in
// ProgramData are read-only for the others.
bool grant_all_users(const std::filesystem::path& directory) {
    BYTE sid_buffer[SECURITY_MAX_SID_SIZE];
    DWORD sid_size = sizeof(sid_buffer);
    if (!CreateWellKnownSid(WinBuiltinUsersSid, nullptr, sid_buffer, &sid_size)) return false;
    PACL old_acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetNamedSecurityInfoW(directory.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                              &old_acl, nullptr, &descriptor) != ERROR_SUCCESS)
        return false;
    EXPLICIT_ACCESS_W access{};
    access.grfAccessPermissions = GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | DELETE;
    access.grfAccessMode = GRANT_ACCESS;
    access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid_buffer);
    PACL new_acl = nullptr;
    const bool ok = SetEntriesInAclW(1, &access, old_acl, &new_acl) == ERROR_SUCCESS &&
                    SetNamedSecurityInfoW(const_cast<LPWSTR>(directory.c_str()), SE_FILE_OBJECT,
                                          DACL_SECURITY_INFORMATION, nullptr, nullptr, new_acl,
                                          nullptr) == ERROR_SUCCESS;
    if (new_acl != nullptr) LocalFree(new_acl);
    if (descriptor != nullptr) LocalFree(descriptor);
    return ok;
}

// C:\ProgramData\BlockPlanet, or empty if it cannot be used.
std::filesystem::path shared_directory() {
    PWSTR base = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &base))) return {};
    const auto directory = std::filesystem::path(base) / L"BlockPlanet";
    CoTaskMemFree(base);
    std::error_code code;
    if (!std::filesystem::exists(directory, code)) {
        if (!std::filesystem::create_directories(directory, code)) return {};
        grant_all_users(directory);
    }
    return writable(directory) ? directory : std::filesystem::path{};
}
#endif

} // namespace

std::filesystem::path executable_directory() {
    char* base = SDL_GetBasePath();
    if (base == nullptr) return {};
    auto directory = std::filesystem::u8path(base);
    SDL_free(base);
    return normalized(directory);
}

std::filesystem::path legacy_data_directory() {
    const auto directory = executable_directory();
    return directory.empty() ? directory : directory / "data";
}

bool portable_mode() {
    const auto directory = executable_directory();
    std::error_code error;
    return !directory.empty() && std::filesystem::exists(directory / "portable.txt", error);
}

std::filesystem::path application_data_directory() {
    if (const char* override = std::getenv("TETRISPHERE_DATA_DIR"); override != nullptr && *override != '\0') {
        return std::filesystem::u8path(override);
    }
    if (portable_mode()) return legacy_data_directory();
#ifdef _WIN32
    if (auto shared = shared_directory(); !shared.empty()) return shared;
#endif
    return per_user_directory();
}

} // namespace tetrisphere
