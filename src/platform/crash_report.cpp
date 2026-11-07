#include "tetrisphere/crash_report.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace tetrisphere {
namespace {

// Everything the handler writes is prepared beforehand: a crash handler may
// not allocate or take locks.
char report_path[1024] = {};
char report_header[512] = {};
char context_a[512] = {}, context_b[512] = {};
volatile int context_current = 0;  // which buffer holds the latest context

std::size_t length(const char* s) { return std::strlen(s); }

#ifdef _WIN32
void write_all(HANDLE file, const char* text) {
    DWORD written = 0;
    WriteFile(file, text, DWORD(length(text)), &written, nullptr);
}

void write_report(const char* what, const void* address) {
    HANDLE file = CreateFileA(report_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    char line[256];
    write_all(file, report_header);
    std::snprintf(line, sizeof line, "What happened: %s at %p\r\n", what, address);
    write_all(file, line);
    std::snprintf(line, sizeof line, "BlockPlanet loaded at: %p\r\n", static_cast<void*>(GetModuleHandleA(nullptr)));
    write_all(file, line);
    write_all(file, "Doing: ");
    write_all(file, context_current == 0 ? context_a : context_b);
    write_all(file, "\r\nStack (addresses):\r\n");
    void* frames[48];
    const USHORT count = CaptureStackBackTrace(0, 48, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        HMODULE module = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(frames[i]), &module))
            GetModuleFileNameA(module, name, MAX_PATH);
        const char* base = std::strrchr(name, '\\');
        std::snprintf(line, sizeof line, "  %p  %s+0x%llx\r\n", frames[i], base ? base + 1 : name,
                      static_cast<unsigned long long>(static_cast<const char*>(frames[i]) - reinterpret_cast<const char*>(module)));
        write_all(file, line);
    }
    CloseHandle(file);
}

LONG WINAPI on_exception(EXCEPTION_POINTERS* info) {
    char what[64];
    std::snprintf(what, sizeof what, "exception 0x%08lX", info ? info->ExceptionRecord->ExceptionCode : 0ul);
    write_report(what, info ? info->ExceptionRecord->ExceptionAddress : nullptr);
    return EXCEPTION_CONTINUE_SEARCH;
}
#else
void write_all(int fd, const char* text) {
    std::size_t left = length(text);
    while (left > 0) {
        const ssize_t n = ::write(fd, text, left);
        if (n <= 0) return;
        text += n;
        left -= std::size_t(n);
    }
}

void write_report(const char* what, const void* address) {
    const int fd = ::open(report_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    char line[256];
    write_all(fd, report_header);
    std::snprintf(line, sizeof line, "What happened: %s at %p\n", what, address);
    write_all(fd, line);
    write_all(fd, "Doing: ");
    write_all(fd, context_current == 0 ? context_a : context_b);
    write_all(fd, "\nStack:\n");
    void* frames[48];
    const int count = backtrace(frames, 48);
    backtrace_symbols_fd(frames, count, fd);
    ::close(fd);
}

void on_signal(int number, siginfo_t* info, void*) {
    const char* name = number == SIGSEGV ? "SIGSEGV (bad memory access)" : number == SIGBUS ? "SIGBUS" : number == SIGILL ? "SIGILL"
                     : number == SIGFPE ? "SIGFPE" : number == SIGABRT ? "SIGABRT (aborted)" : "signal";
    write_report(name, info ? info->si_addr : nullptr);
    // Then the usual end: the default action for this signal.
    std::signal(number, SIG_DFL);
    std::raise(number);
}
#endif

void on_terminate() {
    write_report("std::terminate (an unhandled C++ exception)", nullptr);
    std::abort();
}

}  // namespace

void install_crash_handler(const std::filesystem::path& directory, const std::string& run_id, const std::string& build_id) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto path = (directory / ("crash-" + std::to_string(ms) + "-" + run_id + ".txt")).u8string();
    std::snprintf(report_path, sizeof report_path, "%s", reinterpret_cast<const char*>(path.c_str()));
    std::snprintf(report_header, sizeof report_header,
                  "BlockPlanet crash report\n"
                  "Please send this file with a short note of what you were doing.\n"
                  "Build: %s\nRun: %s\n\n",
                  build_id.c_str(), run_id.c_str());
    std::snprintf(context_a, sizeof context_a, "starting up");
#ifdef _WIN32
    SetUnhandledExceptionFilter(on_exception);
#else
    struct sigaction action {};
    action.sa_sigaction = on_signal;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (int number : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(number, &action, nullptr);
#endif
    std::set_terminate(on_terminate);
}

void set_crash_context(const std::string& context) {
    // Written into the buffer not in use, then switched to: a crash mid-copy
    // still finds a whole line.
    char* target = context_current == 0 ? context_b : context_a;
    std::snprintf(target, sizeof context_a, "%s", context.c_str());
    context_current = context_current == 0 ? 1 : 0;
}

std::vector<std::filesystem::path> take_new_crash_reports(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> fresh;
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) return fresh;
    const auto shown_file = directory / "crashes-shown.txt";
    std::set<std::string> shown;
    {
        std::ifstream in(shown_file);
        for (std::string line; std::getline(in, line);)
            if (!line.empty()) shown.insert(line);
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("crash-", 0) == 0 && entry.path().extension() == ".txt" && !shown.count(name)) fresh.push_back(entry.path());
    }
    std::sort(fresh.begin(), fresh.end());
    if (!fresh.empty()) {
        std::ofstream out(shown_file, std::ios::app);
        for (const auto& path : fresh) out << path.filename().string() << "\n";
    }
    return fresh;
}

}  // namespace tetrisphere
