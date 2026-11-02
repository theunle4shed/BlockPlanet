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
- Added [docs/research/blockplanet-frame-pacing.md](docs/research/blockplanet-frame-pacing.md): the game can draw 60 FPS on PC, a slow renderer lowers the frame rate in whole-VI steps without slowing game speed, and the game caps logic catch-up at 5 steps per frame in one mode.

## 2026-11-01: Framebuffer readback option and RT64 tile-copy leak fix

- Added `--framebuffer-readback=on|off` (default on). Off stops RT64 from rendering every frame a second time and waiting for the GPU to copy it into game memory. Known issue when off: the controller buttons in Training are missing.
- Fixed an RT64 leak in `findTileCopyId` that only appears with readback off: the renderer-thread tile-copy map grew without limit and was scanned on every framebuffer tile load (patch in `tools/build/patch_rt64_probe.py`).
- Recorded the measurements in [docs/research/blockplanet-frame-pacing.md](docs/research/blockplanet-frame-pacing.md).

## 2026-11-01: Built-in trophies

- Added a data-driven trophy engine (`include/tetrisphere/trophies.h`, `src/platform/trophies.cpp`) and the game's trophy service (`trophy_service.h/.cpp`). Definitions are in `assets/trophies/trophies.json` (copied beside the executable and into packages) and progress is saved to `data/trophies.json`. See [docs/TROPHIES.md](docs/TROPHIES.md).
- The game loads trophies at startup and saves progress on unlocks, every 30 seconds and on exit. Game events are not reported yet, so nothing unlocks during play until the game hooks are added.

## 2026-11-01: First game hooks for trophies

- Added `GameEventWatcher` (`include/tetrisphere/game_event_watcher.h`, `src/platform/game_event_watcher.cpp`), which reads known game memory each pass of the main loop and reports `mode_started`, `piece_placed`, `life_lost`, `game_over` and `vs_round` to the trophy engine. Set `TETRISPHERE_LOG_GAME_EVENTS=1` to print them.
- Recorded the addresses and how they were found in [docs/research/blockplanet-game-state.md](docs/research/blockplanet-game-state.md), including the game's mode numbers. Mode 4, which has the catch-up cap, is Puzzle.

## 2026-11-02: Casual play, start chapter and level events

- Added `--casual` (no game over: hearts refill and the Time Trial clock stops at 30 seconds) and `--start-chapter=N` (start Rescue at chapter N). Both pause trophies. Implemented in `include/tetrisphere/game_options.h` and `src/platform/game_options.cpp`.
- The game event watcher now reports `level_completed`, `chapter_completed` and `puzzle_solved`, using the chapter (`0x800E44A0`) and level (`+0x23BA`) found this session. A real `level_completed` was seen when clearing level 1:1.
- Testing later stages is described in [docs/PERFORMANCE_TESTING.md](docs/PERFORMANCE_TESTING.md).

## 2026-11-02: Level data research

- Added `tools/analysis/level_table.py`, which prints the level parameter table from the player's own ROM. The output is derived from game data and stays out of Git.
- Documented the level data format and how difficulty scales in [docs/research/blockplanet-level-data.md](docs/research/blockplanet-level-data.md). Rescue has 15 chapters of 10 levels; starting chapters above 16 reads into other data.
