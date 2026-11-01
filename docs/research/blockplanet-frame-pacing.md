# Frame pacing findings (BlockPlanet)

Measured on 2026-11-01 with a native Linux build of BlockPlanet from the verified NTPE rev0 ROM, using the `perf_window` report. There was no GPU, so RT64's display-list processing was skipped or delayed with temporary local instrumentation that was not committed. These runs show how the game itself paces frames, not how fast RT64 is on any particular PC.

## The game is not capped at 20 or 30 FPS

With display-list processing skipped, the game drew about **60 frames per second** on the title screen, in menus and in gameplay, with the VI rate at 60 Hz and one logic step per drawn frame. The original N64 drew about 20–34 frames per second (see [m0-reference-session.md](m0-reference-session.md)) because of console hardware limits, not a frame cap in the game.

## A slow renderer lowers the frame rate, not the game speed

ultramodern sends `dp_complete` only after RT64's `processDisplayLists` returns, so the game cannot start its next frame until the renderer thread has finished the current one. With an artificial 40 ms per graphics task, the game drew 20 frames per second and applied 3 logic steps per drawn frame (pending steps at `0x80160C64`), so gameplay speed stayed at 60 steps per second.

Frame rates are quantized to whole VIs: a graphics task that takes slightly over 16.7 ms drops the game from 60 to 30 FPS, and over 33.3 ms to 20 FPS. Keeping `display_list` time below 16.7 ms is therefore what keeps the game at 60.

## Catch-up cap

Before each draw, the dispatcher at `0x800CCE7C` copies the pending logic steps (`0x800E4484`) to `0x80160C64`. When the mode value at `0x800E44A8` equals 4, it caps them at 5. Below about 12 drawn frames per second in that mode, game time would genuinely slow down. Observed mode values: 2 on the title and menus, 3 in the first gameplay mode reached from the main menu. Which mode uses 4 has not been identified yet.

## Next questions

- On real hardware, how close is `display_list` time to 16.7 ms, and how much of it is `display_list_rt64`?
- Does `--present-mode=skip-buffering` reduce it? In Console mode, RT64's present thread takes the workload lock.
- Is there first-time shader compilation stutter in RT64 that a pre-built shader cache would remove?
