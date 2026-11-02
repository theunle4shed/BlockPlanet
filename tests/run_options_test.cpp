#include "tetrisphere/run_options.h"

#include <cassert>
#include <string>

using tetrisphere::parse_run_options;

int main() {
    {
        const char* argv[] = {"tetrisphere-m1", "--help"};
        const auto options = parse_run_options(2, argv);
        assert(options.valid && options.help && options.rom_path.empty());
    }
    {
        const char* argv[] = {"tetrisphere-m1", "--fullscreen", "--msaa2x",
                              "--msaa=8x", "--windowed", "--presentation-filter=pixel",
                              "--texture-filter=three-point", "Rom con ñ.zip"};
        const auto options = parse_run_options(8, argv);
        assert(options.valid && !options.help);
        assert(options.fullscreen && !*options.fullscreen);
        assert(options.msaa_samples && *options.msaa_samples == 8);
        assert(options.presentation_filter &&
               *options.presentation_filter == tetrisphere::PresentationFilter::Pixel);
        assert(options.three_point_filter && *options.three_point_filter);
        assert(options.rom_path == "Rom con ñ.zip");
    }
    {
        const char* argv[] = {"tetrisphere-m1", "--msaa=3", "rom.z64"};
        const auto options = parse_run_options(3, argv);
        assert(!options.valid && options.error.find("MSAA") != std::string::npos);
    }
    {
        const char* argv[] = {"tetrisphere-m1", "--presentation-filter", "rom.z64"};
        const auto options = parse_run_options(3, argv);
        assert(!options.valid && options.error.find("presentation") != std::string::npos);
    }
    {
        const char* argv[] = {"tetrisphere-m1", "rom.z64", "extra.z64"};
        const auto options = parse_run_options(3, argv);
        assert(!options.valid && options.error.find("ROM") != std::string::npos);
    }
    {
        const char* argv[] = {"tetrisphere-m1", "--present-mode=skip-buffering", "--perf-log"};
        const auto options = parse_run_options(3, argv);
        assert(options.valid && options.perf_log);
        assert(options.present_mode &&
               *options.present_mode == tetrisphere::PresentMode::SkipBuffering);
    }
    {
        const char* argv[] = {"tetrisphere-m1", "--present-mode", "early"};
        const auto options = parse_run_options(3, argv);
        assert(options.valid && !options.perf_log);
        assert(options.present_mode && *options.present_mode == tetrisphere::PresentMode::Early);
    }
    {
        const char* argv[] = {"tetrisphere-m1", "rom.z64"};
        const auto options = parse_run_options(2, argv);
        assert(options.valid && !options.present_mode && !options.perf_log);
    }
    {
        const char* argv[] = {"tetrisphere-m1", "--framebuffer-readback=off"};
        const auto options = parse_run_options(2, argv);
        assert(options.valid && options.framebuffer_readback && !*options.framebuffer_readback);
    }
    {
        const char* argv[] = {"tetrisphere-m1", "--framebuffer-readback", "maybe"};
        const auto options = parse_run_options(3, argv);
        assert(!options.valid && options.error.find("readback") != std::string::npos);
    }
    {
        const char* argv[] = {"tetrisphere-m1", "--casual", "--start-chapter=12"};
        const auto options = parse_run_options(3, argv);
        assert(options.valid && options.casual && options.start_chapter && *options.start_chapter == 12);
    }
    {
        const char* argv[] = {"tetrisphere-m1", "--guest"};
        assert(parse_run_options(2, argv).guest);
        const char* argv2[] = {"tetrisphere-m1", "--guest", "--profile=Sam"};
        assert(!parse_run_options(3, argv2).valid);
        const char* argv3[] = {"tetrisphere-m1", "--profile=Sam"};
        assert(parse_run_options(2, argv3).profile == std::string("Sam"));
    }
    {
        const char* argv[] = {"tetrisphere-m1", "--sphere=extended"};
        const auto options = parse_run_options(2, argv);
        assert(options.valid && options.sphere_extended && *options.sphere_extended);
        const char* argv2[] = {"tetrisphere-m1", "--sphere=original"};
        const auto original = parse_run_options(2, argv2);
        assert(original.valid && original.sphere_extended && !*original.sphere_extended);
        const char* argv3[] = {"tetrisphere-m1", "--sphere=huge"};
        assert(!parse_run_options(2, argv3).valid);
    }
    {
        const char* argv[] = {"tetrisphere-m1", "--start-chapter", "0"};
        assert(!parse_run_options(3, argv).valid);
        const char* argv2[] = {"tetrisphere-m1", "--start-chapter=x"};
        assert(!parse_run_options(2, argv2).valid);
    }
    {
        const char* argv[] = {"tetrisphere-m1", "--present-mode=fast"};
        const auto options = parse_run_options(2, argv);
        assert(!options.valid && options.error.find("present mode") != std::string::npos);
    }
    return 0;
}
