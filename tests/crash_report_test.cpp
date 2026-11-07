#include "tetrisphere/crash_report.h"

#include <cassert>
#include <filesystem>
#include <fstream>

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    const fs::path dir = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "crash-report-test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    using tetrisphere::take_new_crash_reports;
    assert(take_new_crash_reports(dir).empty());
    std::ofstream(dir / "crash-200-b.txt") << "two";
    std::ofstream(dir / "crash-100-a.txt") << "one";
    std::ofstream(dir / "100-a.jsonl") << "a session log, not a crash";
    auto fresh = take_new_crash_reports(dir);
    assert(fresh.size() == 2 && fresh[0].filename() == "crash-100-a.txt" && fresh[1].filename() == "crash-200-b.txt");
    assert(take_new_crash_reports(dir).empty());  // each shown once
    std::ofstream(dir / "crash-300-c.txt") << "three";
    fresh = take_new_crash_reports(dir);
    assert(fresh.size() == 1 && fresh[0].filename() == "crash-300-c.txt");
    // The handler installs and the context can be set (no crash here).
    tetrisphere::install_crash_handler(dir, "test-run", "test-build");
    tetrisphere::set_crash_context("in a test");
    return 0;
}
