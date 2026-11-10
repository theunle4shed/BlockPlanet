# Play backgrounds (research, started 2026-11-10)

The owner's question: the animated "Winamp style" backgrounds behind the sphere during play (the swirling purple and blue tunnel in Story 1:1, the red and orange one in Versus and Lines, and so on). How are they made? Do they follow scripts? Can we make our own, or change the existing ones?

Status: **answered (2026-11-10)**, from the generated code and a RAM dump of Story 1:1, read by the parent session. The display list of Story 1:1's background was then logged and compared across frames. Nothing taken from the game (textures, tables, code, asset ids) goes in Git: this page gives addresses and descriptions only.

## Summary

Each play background is a **LightWave scene with one object**, the same kind of asset as the title screen's robots. It is played and looped by the game's general model and scene player (`func_8002D93C`), the one used for the pause's instructor and the title. The motion is **keyframes in the scene data**: positions, rotations and scales over time. No background-specific code drives it. There are **25 backgrounds**, and each level picks one by a per-mode rule. So the backgrounds are data, not scripts written as code. Existing ones can be swapped, slowed, sped up, tinted and recombined with little code. Wholly new ones mean either writing the game's scene and object formats, or BlockPlanet drawing its own in the game's place.

## How one is chosen

- The level set-up `func_800906E8` writes a background index (1 to 25; 0 keeps the previous one) into the level structure at `0x8013D264`, next to the song (`0x8013D266`), by a per-mode rule (jump table `0x800F0F98`). The per-mode tables, from the direct-launch session's catalogue (`docs/research/modes-matrix.md` on `direct-launch`), are:
  - Rescue, Lines and the demo: by chapter, `0x800E94C4`.
  - Time Trial: fixed, `0x800E94A4`.
  - Training: fixed, `0x800E949C`.
  - Puzzle: by group of ten, `0x800E9470`.
  - Hide & Seek: by the record's group byte `0x8013D248`, `0x800E9528`.
  - VS CPU: by round.
  - Mode 12: fixed, `0x800E94C0`.
- Watched in play: Story 1:1 sets `0x8013D264` = 8 at the level's start.
- The level records' bytes 15 and 16 also go to `0x8013D260`, `0x8013D264` and `0x8013D25C` (`docs/research/blockplanet-level-data.md`).

## How it is loaded (`func_8006EE6C`)

- Two parallel tables of 26 halfwords, indexed by the background index: the **scene** ids at `0x800E2262` and the **object** ids at `0x800E2296`. Entry 0 is empty.
- With a non-zero index:
  - The scene is resolved by `func_8002A0C0(scene_id)` and the object by `func_8002A100(object_id)`.
  - The index minus 1 is kept at `0x800E23BC` (7 in Story 1:1).
- With index 0, the previous one is loaded again from `0x800E23BC` + 1.
- The model is built in the pool at `0x800F22C0` (aligned first) by `func_8002CF70(pool, scene, &object, 1, &size)`. This is the same builder BlockPlanet's title hooks use.
- The built background is stored at `0x801033BC`. Its byte `+0x49` is set to 0, and the pool is advanced.
- The load is skipped when `0x8015D980` = 2. Hide & Seek (mode 5, `0x800E44A8`) has extra handling at `0x8006F020`, not yet read.

## How it moves (the per-frame driver)

In the game's main loop `func_80070118`, at `0x80071B70`–`0x80071C38`:

```
frame = (u16)[0x800DFD88] + (s16)[0x80160C64] - bg->0x24
done  = func_8002D93C(bg, frame, ctx + 0x17C0, ctx + 0x1800)   // a second path passes 0, 0 for the matrices
if (done) bg->0x24 = 0                                          // loop
[0x800DFD88] = bg->0x24
```

So the frame it plays comes from a counter plus a per-frame step, and the animation loops when the player reports its end. The drawing goes through the normal model path (`func_8002D93C` from the call at `0x80071BA0`). That is why it is 3D with depth: BlockPlanet's held-magic drawing had to sit nearer the camera than the tunnel walls (`src/graphics/magic_hooks.cpp`).

## Still open

- ~~Mesh motion or texture motion?~~ **Mesh motion only** (2026-11-10, `BLOCKPLANET_BG_DL_LOG=120`, Story 1:1, draws 120 and 150, read by the parent session).
  - Both draws are 260 F3D commands. The only lines that differ are two `G_MTX` modelview matrices: a load (`p=02`) and a multiply (`p=00`). The projection (`p=03`) is identical.
  - Everything else is byte for byte the same:
    - 13 `G_VTX` loads (12 of 16 vertices and one of 9);
    - one RGBA16 texture, a 32 × 32 mipmap chain (six tile levels, 32 down to 1), with no palette;
    - the colour combiner, prim and env colours, and the geometry mode.
  - So nothing scrolls or colour-cycles. The tunnel is one small textured mesh that the keyframes turn, move and scale.
  - **The colour shifts seen in play** (red to purple) come from lighting: the geometry mode has `G_LIGHTING` on, with the lights set by `G_MOVEWORD` and `G_MOVEMEM`. As the mesh turns, the lights shade it differently.
  - No colour-image switch appears in these commands. The switch to the off-screen layer at `0x000400` is made in the code around the call, not inside it.
- ~~Is it music-driven?~~ **No** (2026-11-10). The step `0x80160C64` is set by the frame loop `func_800CC6E0` (at `0x800CCF28`–`0x800CCF90`), just before each call to `func_80070118`, from `0x800E4484`, the count of video retraces since the last game frame (reset each loop, 1 at start; capped at 5 in Puzzle, mode 4). So the background plays its keyframes at a fixed speed in real time, with no link to the music. A music-driven background would be BlockPlanet's own addition: for example, the frame passed at `0x80071BA0` scaled by the audio level or tempo.
- The texture sizes and formats in the 25 objects; Hide & Seek's extra handling.

## Feasibility (2026-11-10)

What the display-list log means for each option:
- **Tints** can come from three places: the light colours (the background's `G_MOVEMEM` light blocks, which also keep its shading alive), the prim colour, or the whole off-screen layer.
- **Speed** comes from the frame advance.
- **Counter-motion** comes from an extra rotation multiplied into the modelview (`p=02`), the one thing that moves.
- **Texture replacement** is one 32 × 32 RGBA16 texture per background, an easy target for RT64 texture replacement.


| Option | What it takes | Effort | Risk |
| --- | --- | --- | --- |
| Any of the 25 backgrounds for any level | Write `0x8013D264` before the level set-up (or the per-mode tables) | Small | Low: the game's own path |
| Slower or faster | Scale the background's frame step only: a hook on the `func_8002D93C` call at `0x80071BA0` with a changed frame (a1). It must stay tied to the game's frame count, not the display rate | Small | Low |
| Colour or tint | Per-part vertex or material colour, as BlockPlanet's title hooks already do for the Turbine and Wheels models | Small to medium | Low to medium |
| New combinations | Any scene with any object, or other scenes and objects from the archive, through the same resolvers and builder (`func_8002A0C0`, `func_8002A100`, `func_8002CF70`) | Medium | Medium: the pool's size limits what fits |
| Textures replaced | Replace the object's textures at load (RT64 texture replacement, or our own image in place of the archive entry), keeping the motion | Medium | Medium |
| New backgrounds in the game's formats | Write new scene keyframes or object meshes in the LightWave-derived formats (partly known, `docs/research/blockplanet-assets.md`) | Large | Medium to high: formats only partly known |
| BlockPlanet draws its own | Skip the call at `0x80071BA0` and draw our own animated background in its place (a shader or mesh behind the sphere, like the menu grid but under the game's 3D) | Medium to large | Medium: our draw must sit at the background's depth for the magic and the sphere |

Rendering note: earlier research (`docs/ROADMAP.md`, Phase 3) found that play renders the background into an off-screen layer at RDRAM `0x000400`, with two projections, and the main frame then draws it as forty 64 × 30 tiles. So the background reaches the screen as a picture, not straight from the scene. Two consequences:
- A tint or overlay could also be applied to that layer as a whole (one colour multiply over the copied tiles), not only per part.
- A background BlockPlanet draws itself could go into that layer, so the sphere and the magic stay in front of it.

The `BLOCKPLANET_BG_DL_LOG` output should show the colour-image switch to `0x000400` around the scene's draw.

## Pinch mode: the owner's idea (2026-11-10)

The background reacts when the player is close to losing. Research and notes only until the owner decides.

**Triggers.** Each is a value BlockPlanet can read each frame, as it already does for its HUD:

| Trigger | Where | Notes |
| --- | --- | --- |
| Last heart | Player context `0x80110220` + `0x2D7E` (-1 at game over) | Known and already read by BlockPlanet; Story and other modes with hearts |
| Drop timer or shot time limit nearly out | Candidates `+0x3254` and `+0x3250` (direct-launch catalogue) | To confirm in play, and to scale to each mode's speed |
| Board close to its limit | Not found yet | The board's height or fill against the game's limit; needs research |
| The game's own danger signal | **None seen** (2026-11-10) | Story 1:1 with the hearts set to 1 by a poke (`0x80112F9E` = 1). For about 10 s the background, the other visuals and the game's own drawing were unchanged, and no new events were logged. Two limits: audio could not be heard, so a music change is not ruled out (asked of the owner); and the poke skips the game's own heart-loss path, so a real loss may still differ. As far as seen, pinch mode would be wholly BlockPlanet's addition, triggered by the hearts value |

**Reactions.** Each follows from the feasibility table:
- **Speed:** a faster background (for example 1.5 to 2 times), or a stutter or pulse, by scaling the frame passed at `0x80071BA0`. Small effort. It must ease in and out, not jump.
- **Tint or pulse:**
  - a colour shift towards red, or a beat-like pulse;
  - per part, as the title hooks colour models, or over the whole background layer (above);
  - how well it works depends on the `BLOCKPLANET_BG_DL_LOG` result: whether the colours come from vertex colours, textures or a palette.
  - Small to medium.
- **Another background:** switch to a "danger" one of the 25 by loading it as `func_8006EE6C` does. A rebuild mid-play is riskier: it uses the pool at `0x800F22C0`. A cross-fade would need both in memory. Medium.
- **A BlockPlanet overlay:** a vignette, edge glow or pulse drawn by BlockPlanet over the background and under the sphere. It is ours entirely and doesn't depend on the game's assets. Medium.

All of these are presentation only. None changes the game's rules or timers (`AGENTS.md`: the original's simulation speed, timers and audio pitch stay as they are).

## Counter-motion: the owner's second idea (2026-11-10)

**Does the background react to play now?** No, at least in Story 1:1. Measured by the parent session on the real game, recorded at 30 fps: 3 s idle, then 3 s holding left so the sphere spins, then 3 s idle. The figure is the mean absolute change per frame, measured on the bands above and below the sphere for the background, and on a patch at the centre for the sphere:

| Phase | Background | Sphere |
| --- | --- | --- |
| Idle | 2.40 | 0.11 |
| Spinning | 0.98 | 4.52 |
| Idle again | 1.34 | 0.75 |

The background changes no more while the sphere spins than when idle. It follows only its own keyframes, and the matrices passed at `0x80071BA0` are presumably a fixed camera.

**The idea:** "cool counter motion backgrounds". The background turns against the sphere's rotation, or with it, so spinning the sphere feels like moving through a space. Research and notes only until the owner decides.

- **Input: the sphere's rotation.** The game rotates the sphere by moving the board under a fixed view. The mesh is rebuilt by `func_800ADD6C` from the sub-cell rotation while the sphere turns (`docs/research/blockplanet-game-state.md`, how the patch is built). So the rotation is the board's view position (whole cells, 32 around) plus that sub-cell part. Their exact addresses in the player context are still to name. A watch while holding left and then up would find them. The angular speed is the per-frame change.
- **Applying it:**
  - **Turn the background:** multiply the matrices passed at `0x80071BA0` (a2 and a3, `ctx + 0x17C0` and `ctx + 0x1800`) by a rotation that follows the sphere's yaw and pitch, scaled (for example −0.3 for counter-motion, +0.3 for drag) and eased. This needs a hook on that call, like the speed hook. It needs the matrices' layout confirmed (`BLOCKPLANET_BG_DL_LOG` logs them). Small to medium.
  - **Speed up its own animation while spinning:** add the angular speed to the frame advance. Small.
  - **Offset the background layer:** shift or rotate the copied background layer (the forty tiles, above). This is a 2D parallax rather than a 3D turn. It's cheap, but its edges must stay covered. Medium.
  - **BlockPlanet's own background:** with our own drawing, counter-motion is simply part of its design. Medium to large, as above.
- **Care points:**
  - The rotation should ease and wrap smoothly (the board wraps at 32 cells).
  - It must stay comfortable to watch, so it needs an option to turn it off or down.
  - Presentation only: the game's rotation and timing are untouched.

A note on ownership: backgrounds BlockPlanet draws itself (the last row) would be entirely BlockPlanet's own art and code, so they could be committed. Changes to the original ones must keep using the player's ROM at run time.
