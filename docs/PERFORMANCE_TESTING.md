# Performance testing

BlockPlanet can report where each frame's time goes. Use it to compare settings and builds on your own hardware.

## Turn the report on

Start the game with `--perf-log`:

```
tetrisphere.exe --perf-log
```

On Windows, run that from a Command Prompt or PowerShell window in the game folder so you can see the output. You can also set the environment variable `TETRISPHERE_PERF_METRICS=1`. Reports are also written to the diagnostics file in `data/diagnostics/`, so you can read them afterwards even without a console.

## Reading a report

Every 5 seconds one line like this appears:

```json
{"event":"perf_window","window_ms":5002,"present_mode":"console","game_fps":20.0,"vi_hz":60.0,
 "display_list":{"count":100,"avg_ms":3.10,"max_ms":9.80,"total_ms":310.0,"over_16ms":0},
 "display_list_rt64":{...},"screen_update":{...},"audio_wait":{...},"audio_rejections":0}
```

| Field | Meaning | What to look for |
| --- | --- | --- |
| `game_fps` | Frames the game itself drew | The game's own frame rate. If it is 20 or 30 even in quiet scenes, choppiness comes from the game's frame rate, not from your PC |
| `vi_hz` | Emulated video interrupts per second | Should stay close to 60. Lower means the whole game is running slow |
| `display_list` | Renderer time per game frame, including BlockPlanet's fixes | `max_ms` or `over_16ms` rising in busy scenes points to the renderer |
| `display_list_rt64` | RT64's share of `display_list` | If close to `display_list`, the time is spent inside RT64 |
| `screen_update` | Time to hand a finished frame to the screen, once per VI | High values mean presenting is blocking the renderer thread |
| `audio_wait` | Game-thread time blocked waiting for audio space | Large or frequent waits during slowdown point to audio pacing |
| `audio_rejections` | Audio buffers dropped after a wait timed out | Should be 0; non-zero values can mean crackle |

## The 16.7 ms rule

The game can draw 60 frames per second, but it waits for the renderer to finish each frame before starting the next. If `display_list` takes even slightly more than 16.7 ms, the frame rate drops to 30, and past 33.3 ms to 20. Game speed is kept by applying several logic steps per frame, so this looks choppy rather than slow. See [frame pacing findings](research/blockplanet-frame-pacing.md).

## Testing later stages

Later chapters have larger spheres with more layers. Start at any chapter with `--start-chapter=N`, and add `--casual` so a game cannot end while you measure. Both pause trophies. For example:

```
tetrisphere.exe --perf-log --casual --start-chapter=15
```

On the software renderer used for development, chapter 20 cost about a third more renderer time per frame than chapter 1, with higher peaks.

## Sphere buffer use

`BLOCKPLANET_SPHERE_STATS=1` adds a `sphere_usage` line every 300 frames: the most display list and vertex space the sphere used in one frame, the same for the whole frame, and the buffer address ranges seen (`sphere_usage_buffers` lists the distinct buffer starts). Use it when testing larger sphere patches; the game's vertex pool holds 56 KB per frame.

## Trying the extended sphere

Set Options > Graphics > SPHERE to EXTENDED (or start with `--sphere=extended`) to draw two extra cells on every side of the sphere patch (19 × 19 instead of 15 × 15), so blocks near the edge of the view are not missing. Compare the frame time with `--perf-log` against the same stage with ORIGINAL. For testing other sizes, `BLOCKPLANET_SPHERE=replace BLOCKPLANET_SPHERE_RING=n` (1 to 4) overrides the setting. The log shows a `sphere_mode` line whenever the drawing mode changes.

## A useful test run

1. Pick one scene that feels slow, for example a busy Rescue level or VS mode, and one quiet scene, such as the main menu.
2. Play each for about 30 seconds with `--perf-log`.
3. Repeat with `--present-mode=skip-buffering`, then with `--present-mode=early`.
   Also try `--framebuffer-readback=off`. It removes a GPU wait from every frame, but some graphics that the game renders once and reuses go missing, for example the buttons on the controller picture in Training.
4. Note any visual problems with each mode, especially in pause screens, screen transitions and widescreen.
5. Share the `perf_window` lines with your hardware, OS, resolution, aspect, refresh setting and present mode.

`early` presents frames as soon as they are drawn. It has the lowest delay, but it can show unfinished frames in games that draw a frame in several passes, so check it carefully.
