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

## 2026-11-02: Sphere renderer, stage 1 (mesh table, cell setup, quadrant passes and faces)

- Reimplemented the sphere mesh table builder (`func_800ADD6C`) as `tetrisphere::sphere::build_mesh` (`include/tetrisphere/sphere_mesh.h`, `src/graphics/sphere_mesh.cpp`), with the grid size as a parameter for later stages.
- Hooked it into the generated code (`config/recomp/tetrisphere.us.rev0.toml`, `src/graphics/sphere_hooks.cpp`). `BLOCKPLANET_SPHERE` selects `original` (default, unchanged), `verify` (run both and compare every table) or `replace` (use BlockPlanet's table).
- Verified: zero differing entries over more than 1,000 table rebuilds in chapter 1 and 500 in chapter 15 with rotation in all directions; replace mode plays normally.
- Reimplemented the cell setup pass (`func_8009D90C`), which prepares each grid cell for drawing, as `tetrisphere::sphere::prepare_cell` (`include/tetrisphere/sphere_cell.h`, `src/graphics/sphere_cell.cpp`), with a unit test (`tests/sphere_cell_test.cpp`). It uses the same `BLOCKPLANET_SPHERE` modes.
- Verified: zero differing values over more than 131,000 calls each in chapter 1 and chapter 15; replace mode draws the sphere correctly in chapter 15 with a hole dug to the core.
- Added a general verify harness for ported passes: BlockPlanet's version writes to an overlay of guest memory and every value is compared with what the original leaves, logging the first mismatches in detail.
- Reimplemented the four sphere quadrant passes (`func_8009E630`, `func_8009EB28`, `func_8009F024`, `func_8009F520`) as one routine, `tetrisphere::sphere::draw_quadrant` (`include/tetrisphere/sphere_quadrants.h`, `src/graphics/sphere_quadrants.cpp`). The face drawing is still the game's own code.
- Added a snapshot check for passes that call other game functions: it replays BlockPlanet's version from the original's starting state and compares all of guest memory. Verified: zero differing bytes in at least 64 checks per quarter in chapters 1 and 15; replace mode draws correctly in both.
- Added `include/tetrisphere/guest_memory.h`, shared guest memory helpers.
- Reimplemented the six sphere face functions as `tetrisphere::sphere::draw_face` (`include/tetrisphere/sphere_faces.h`, `src/graphics/sphere_faces.cpp`), with a unit test. Verified: zero differences in chapters 1 and 15; replace mode now draws the whole sphere with BlockPlanet code.
- Fixed the snapshot check so it never writes live guest memory (it runs BlockPlanet's version on a private copy) and stays quick (one copy, page-wise comparison, one quarter checked every few frames). The first version restored snapshots over live memory and stalled a frame for about 140 ms; either could lose runtime messages and hang the game.
- Added `BLOCKPLANET_SPHERE_VERIFY` to check only some groups of passes, and `BLOCKPLANET_SPHERE_SELFTEST` to measure the snapshot check's own noise.
- Documented how the game culls faces: a precomputed face mask per block, with each quarter drawing only the outer face and the two sides that look towards the view centre.
- Corrected the research notes: each mesh table entry holds a base point and a fixed layer step (not a core point), and `0x8015EDB2`/`0x8015EDAE` are the cell's height and kind (not a column span).

## 2026-11-02: Sphere renderer, stage 2 (buffer budget)

- Added `BLOCKPLANET_SPHERE_STATS=1`, which logs how much display list and vertex space the sphere and the whole frame use (`docs/PERFORMANCE_TESTING.md`).
- Measured the budget in chapter 15: the sphere uses up to 18 KB of the game's 56 KB vertex pool and 9.5 KB of its 112 KB display list. A wider patch would only just fit the game's pool, but the upper 4 MB of memory is unused by the game, so BlockPlanet's larger patches will keep their vertices there. Details in `docs/research/blockplanet-game-state.md`.
- Worked out the rule behind the game's base grid (a uniform angular grid seen from the sphere's lowest point) and added `include/tetrisphere/sphere_grid.h`, which gives grid points beyond the game's table, with a unit test. The formula is within 49 units of the table (checked in the live game).
- The mesh builder accepts 32-bit grid values for extended grids (`MeshInputs::wide`); the game's own mesh is still reproduced exactly.
- Documented how the game computes its lighting normals (`func_800A2374`).
- Added the extended sphere patch (`BLOCKPLANET_SPHERE_RING`, with `BLOCKPLANET_SPHERE=replace`): extra cells on every side, built from BlockPlanet's own mesh and normals (`include/tetrisphere/sphere_extended.h`), with vertices kept above 4 MB. Checked byte-for-byte against the game's passes with no extra cells (`BLOCKPLANET_SPHERE_EXTENDED_TEST`), and drawn correctly with two extra cells in chapter 15. A playtest of the edge artifacts is still needed.
