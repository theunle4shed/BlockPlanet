# Changes since Blocksphere

BlockPlanet is a modified version of [Blocksphere](https://github.com/djultra64/Blocksphere) by djultra64. It forks from Blocksphere commit `53203b6` (after the 1.1.0 release). This file lists BlockPlanet's changes so that modified files can be told apart from djultra64's original work, as GPL-3.0 section 5(a) requires. Git history has the full detail.

## 2026-11-01: Fork and rename

- Renamed the project to BlockPlanet in the README, with credit to Blocksphere and djultra64.
- Replaced `AGENTS.md` with BlockPlanet's contributor instructions.
- Removed Blocksphere's banner image, which is not covered by the code license, and the `.recomp.json` catalog entry, which describes Blocksphere.
- Added a BlockPlanet copyright line to `licenses/README.md`. djultra64's copyright and all upstream notices are unchanged.

Internal names (the `tetrisphere` executable, CMake targets, and data paths) are unchanged for now.

## 2026-11-01: Frame-timing report and first performance fixes

- Added a frame-timing report, switched on with `--perf-log` or `TETRISPHERE_PERF_METRICS=1`. Every 5 seconds it prints a `perf_window` line to the console and the diagnostics file, with the game's own frame rate, the VI rate, renderer time per frame, screen-update time, and audio wait time. See [docs/PERFORMANCE_TESTING.md](docs/PERFORMANCE_TESTING.md). New files: `include/tetrisphere/perf_metrics.h`, `tests/perf_metrics_test.cpp`.
- Added `--present-mode=console|skip-buffering|early`. Console stays the default. In Console mode RT64 never interpolates frames, and its present thread takes the same lock as rendering.
- The audio back-pressure wait in `osAiSetNextBuffer` now stops at a real time limit. It used to count 1 ms sleeps, and a Windows sleep can last about 15.6 ms, so the game thread could block for far longer than intended. The reported `ai_fifo_waits_ms` is now measured time.
- On Windows, the game now requests 1 ms timer resolution at startup instead of relying on SDL to do it.

## 2026-11-01: Build the game without private memory captures

- `CMakeLists.txt` now builds `tetrisphere-m1` when only `TETRISPHERE_RSP_RECOMP_SOURCE` is set. The RSP source is generated from the ROM. The three private RDRAM captures (`TETRISPHERE_GRAPHICS_SNAPSHOT`, `TETRISPHERE_GRAPHICS_COMMANDS`, `TETRISPHERE_AUDIO_SNAPSHOT`) are now needed only for the optional `microcode_live` diagnostic test. The audio microcode input for RSPRecomp is the 0xDF0 bytes at ROM offset `0xB9B80`.
- CMake now stops with a clear error if the generated game code would be compiled with clang. N64Recomp's clang form of `RECOMP_FUNC` (`extern inline`) lets clang drop every generated function when compiled as C++, so the link failed with hundreds of undefined `func_XXXXXXXX` symbols. Use GCC on Linux and MSVC on Windows.
