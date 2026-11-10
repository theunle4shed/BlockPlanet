# Play backgrounds (research, started 2026-11-10)

The owner's question: the animated "Winamp style" backgrounds behind the sphere during play (the swirling purple and blue tunnel in Story 1:1, the red and orange one in Versus and Lines, and so on). How are they made? Do they follow scripts? Can we make our own, or change the existing ones?

Status: **mostly answered (2026-11-10)**, from the generated code and a RAM dump of Story 1:1, read by the parent session. One point is still open: whether the visible swirl is motion of the mesh or of its textures (below). Nothing taken from the game (textures, tables, code, asset ids) goes in Git: this page gives addresses and descriptions only.

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

- **Mesh motion or texture motion?** The swirl and colour shifts seen in play may be keyframed object motion (the tunnel turning and scaling) or the object's own material or texture data. To tell them apart: take two RT64 inspector frames about 1 s apart in Story 1:1. Compare the first draws, before the sphere: vertex positions, tile offsets (`uls`/`ult`), and whether a TLUT is loaded and changes between the frames.
- **Is it music-driven?** `docs/REDESIGN.md` calls the backgrounds music-driven. In the driver above, the frame advances by the counter and step only, with no sign of the music. To check, read where `0x80160C64` is written.
- The texture sizes and formats in the 25 objects; Hide & Seek's extra handling.

## Feasibility (2026-11-10)

| Option | What it takes | Effort | Risk |
| --- | --- | --- | --- |
| Any of the 25 backgrounds for any level | Write `0x8013D264` before the level set-up (or the per-mode tables) | Small | Low: the game's own path |
| Slower or faster | Scale the background's frame step only: a hook on the `func_8002D93C` call at `0x80071BA0` with a changed frame (a1). It must stay tied to the game's frame count, not the display rate | Small | Low |
| Colour or tint | Per-part vertex or material colour, as BlockPlanet's title hooks already do for the Turbine and Wheels models | Small to medium | Low to medium |
| New combinations | Any scene with any object, or other scenes and objects from the archive, through the same resolvers and builder (`func_8002A0C0`, `func_8002A100`, `func_8002CF70`) | Medium | Medium: the pool's size limits what fits |
| Textures replaced | Replace the object's textures at load (RT64 texture replacement, or our own image in place of the archive entry), keeping the motion | Medium | Medium |
| New backgrounds in the game's formats | Write new scene keyframes or object meshes in the LightWave-derived formats (partly known, `docs/research/blockplanet-assets.md`) | Large | Medium to high: formats only partly known |
| BlockPlanet draws its own | Skip the call at `0x80071BA0` and draw our own animated background in its place (a shader or mesh behind the sphere, like the menu grid but under the game's 3D) | Medium to large | Medium: our draw must sit at the background's depth for the magic and the sphere |

A note on ownership: backgrounds BlockPlanet draws itself (the last row) would be entirely BlockPlanet's own art and code, so they could be committed. Changes to the original ones must keep using the player's ROM at run time.
