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

**Music.** The owner doesn't think the original's music changes near game over, and this matches what was seen. The owner suggests that in pinch mode the music could speed up a touch. Research is needed on whether the tempo can change at run time without changing the pitch. There are two routes:
- **The game's own sequence player:** the N64 audio library's sequence player has a run-time tempo (`alSeqpSetTempo` in libaudio), so if the game's music is sequenced, its tempo can be raised a little, with the pitch unchanged. It needs the game's player found, and to check that the tempo isn't re-set each beat.
- **BlockPlanet's audio output** (`queue_audio`): a time-stretch of the mixed output. It is harder to keep clean, and it would also stretch the effects.

The first is the natural one if the music is sequenced (the roadmap's BGM research asks the same).

**On game over** (the owner, 2026-11-10): the original fades the music out and plays its game-over sound, the sound of the black bricks encasing the sphere. So pinch mode's faster music must hand over cleanly to that fade, not fight it: it stops changing the tempo as soon as the game-over sequence starts (hearts at -1, `docs/research/blockplanet-game-state.md`, game over), and leaves the fade and the sound to the game. This is presentation only: `AGENTS.md` forbids changing the original's audio pitch or timers, and a tempo change in a pinch is an added effect, so it needs the owner's explicit choice and an option to turn it off.

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

## Brick skybox: the owner's design idea (2026-11-10)

In the owner's words: "Imagine this for a trippy background.. inside a sphere (just a skybox) and high spins counter to the play sphere. Bricks would need to be different colours to the current stage."

So the camera sits inside a huge sphere made of bricks, seen from the inside like a skybox. It spins fast against the play sphere's rotation, and its bricks use colours the current stage does not, so the board stays readable. **A sketch for the owner to approve; nothing is built.**

### Recommended: (a) BlockPlanet builds the brick sphere and draws it in the background's place

- **Where it draws:** in the background's own slot, the call at `0x80071BA0`. When the brick skybox is chosen, BlockPlanet skips the game's scene draw there and appends its own F3D commands to the game's display list instead (the next free command at `0x800F22B4`). This is how BlockPlanet already adds its title ring (`src/graphics/title_hooks.cpp`) and how it draws the board's faces (`src/graphics/sphere_faces.cpp`). Because it is the background's slot, it lands in the off-screen background layer, and the sphere, pieces and magic are drawn over it as now. No depth problems arise with the board.
- **The mesh:** BlockPlanet's own, generated, so it is ours and can be committed:
  - a sphere of brick-shaped quads seen from inside (faces wound inwards), for example 32 around by 16 high, like the board's grid;
  - each brick slightly inset with a bevel, so it reads as bricks and not a smooth ball, with the grid's lighting cues (a lighter top edge, a darker bottom edge) in its vertex colours;
  - the menu grid's equal-angle patch layout (`src/ui/ui_grid.cpp`) is the starting point, mirrored to face inwards.
- **Brick texture:** the game's own brick texture, already loaded in memory during play (the board's faces use it, through the per-block texture coordinates at `0x8015EDA8`). It is read at run time, never copied into Git. The ROM marble the menu grid uses is an alternative. Either way the colour comes from the vertex colours, so one texture serves any colour set.
- **Its own camera:** its own projection and an identity modelview with our rotation, pushed and popped, then the game's projection put back, as the title ring does.

### Other options

- **(b) The game's board mesh, inverted and scaled up:** the board's mesh (`func_800ADD6C`'s 16 × 16 patch table) covers only the half of the sphere facing the camera and is rebuilt as the board turns. Turned inside out, it would need the whole sphere and its own rotation, so it ends up a new mesh anyway, with the game's builder in the way. Not recommended.
- **(c) The game's background scene, recoloured:** keep one of the 25 scenes and change its lights' colours and rotation. It is cheap, but it is still the tunnel, not a brick sphere. It could serve as the reduced-motion fallback (below).
- **(d) Drawn outside the game, in RT64 or under the frame:** the most freedom (shaders), but our drawing would have to sit behind the game's 3D. That means renderer changes, against a slot the game already gives us. Not needed for this design.

### Spin source

- The play sphere turns as the board moves under a fixed view. Its rotation is the board's view position (32 cells around, plus rows) together with the sub-cell rotation `func_800ADD6C` uses. The addresses are still to name: a `BLOCKPLANET_WATCH` on the player context while holding left and then up will find them.
- The skybox's angular velocity is then −k × the sphere's angular velocity, k being the strength, plus a slow drift of its own so it is never still. The angular velocity is the per-frame change, wrapped at 32 cells.
- The skybox is smoothed with a critically damped spring, so it starts and stops softly and never snaps. "High spins" means k well above 1, for example 2 to 4 times the sphere's speed, for the owner's trippy look.

**Found (2026-11-10).** Halfwords of player 1's context (`0x80110220`), named from RAM dumps and then a per-frame watch in Story 1:1. The run held left for 3.8 s, released, held right for 2 s, released, then held up and down:

| Offset | What it is | Seen |
| --- | --- | --- |
| `+0x2B42` (copy at `+0x2B4A`) | **The horizontal angle**, continuous, about 256 a cell and 8,192 a full turn | Starts at 128. While left is held it rises every frame (159, 186, 209, 247 …), and after release it eases on for about 26 frames, the game's own momentum. Right makes it fall. It did not wrap at 8,192 (it reached 8,300), so it wraps at 65,536 or not at all; the code should take differences modulo 65,536 |
| `+0x2B2A` (copy at `+0x2B32`) | The target cell, 0 to 31 | Steps at once, one cell per move |
| `+0x2B1A` (copy at `+0x2B22`) | The cell shown, following the target | Wraps at 32 (29, 30, 31, 0) |
| `+0x2458` | The sub-cell phase | 0 to 12 while moving, 0 when settled |
| `+0x24C4`, `+0x24D4` | Unclear: coarse steps, perhaps the cursor or a camera value | Not needed |

**So the horizontal input is the per-frame change of `+0x2B42`.** It already includes the game's own easing, so a counter-spin that follows it is smooth by itself. The skybox's yaw velocity is −k × Δ(`+0x2B42`) × 2π / 8,192 radians a frame, plus its own drift.

**The vertical axis is the halfword right after each one** (found 2026-11-10, from RAM dumps idle, holding up and after release):

| Offset | What it is | Seen |
| --- | --- | --- |
| `+0x2B46` (copy at `+0x2B4E`) | **The vertical angle**, the twin of `+0x2B42` | Idle 128, 128; up held 406, 1,606, 2,941; after release 3,953 |
| `+0x2B2E` (copy at `+0x2B36`) | The vertical target cell | 0, 0, 2, 8, 13, 15 |
| `+0x2B1E` (copy at `+0x2B26`) | The vertical cell shown | 0, 0, 1, 5, 11, 15 |
| `+0x24C6`, `+0x24D6` | The twin of `+0x24C4` | Not needed |

- So the sphere's turn is an (x, y) pair of angles at `+0x2B42` and `+0x2B46`, about 256 per cell, both with the game's easing.
- The skybox's angular velocity is the per-frame change of both, each taken modulo 65,536.
- **The vertical turn wraps, as the horizontal does** (2026-11-10, up held 6 s): `+0x2B46` climbed smoothly from 128 to 13,169 (about 1.6 turns) with no stop; the target cell counted on to 51 and the cell shown wrapped at 32. There are no poles: both angles are treated the same way.
- Other values also change while the sphere turns: floats at `0x8010C874`, `0x8010C8A0` and `0x8010C8CC` (camera or lighting?), and `0x801270F0` onwards, which is the board's mesh table being rebuilt. They are not needed here.

### A contrasting colour set

**Read in play (2026-11-10, a Story 1:1 RAM dump).** The six words at `0x800E4508` are not colours. They point at six **brick-type records** 0x768 bytes apart (`0x80127CF0` onwards). Each record's second halfword is its brick type, 0x20 to 0x25 (the colours 32–37), followed by small counts and a long run of 0/1 halfwords, which look like shape grids. `0x8013D186` is the stage's colour count (2 in Story 1:1), and `0x8013D188` onwards holds pointers to the records it uses (types 0x21 and 0x20 there). So the types a stage uses are known, and the colour of each type comes from its texture. The game's six brick colours are blue, yellow, red, green, purple and grey. **The types' colours, seen in play** (2026-11-10, the stage's types logged against the board's colours):

| Stage | Types | Board |
| --- | --- | --- |
| Story 1:1 | 0x21, 0x20 | blue, yellow |
| Story 2:1 | 0x21, 0x23 | blue, magenta |
| Story 4:1 | 0x21, 0x23 | blue, magenta |
| Story 3:1 | 0x22, 0x23 | green, magenta |
| Story 5:1 | 0x22, 0x23, 0x24 | green, magenta, pale lavender |
| Story 6:1 | 0x20, 0x24, 0x25 | yellow, pale lavender, red |
| Story 7:1 | 0x21, 0x22, 0x24 | blue, green, pale lavender |
| Story 8:1 | 0x21, 0x20, 0x23 | blue, yellow, magenta |
| Lines 1:1 | 0x21, 0x20 | blue, yellow |
| Puzzle 12 | 0x21, 0x20, 0x22 | yellow, green (and blue) |

So **0x20 yellow, 0x21 blue, 0x22 green, 0x23 magenta, 0x24 pale lavender, 0x25 red** (settled by 5:1 and 7:1, which have lavender without red).

**In the experiment:** the Colours setting "Not the stage's" reads the stage's types (`stage_brick_types`) and uses the colours of the other types, darkened by the brightness setting. It falls back to a fixed set if a stage used all six. The type-to-colour table (`type_colour`) follows the mapping above. Each change of the stage's types is logged as `brick_sky_stage_types`. **This is the default.**


- The stage's brick colours are known at level set-up. Byte 14 of the level record, the colour set, picks entries of the six-entry colour table at `0x800E4508` into `0x8013D188` onwards. The number of colours is at `0x8013D186` (`docs/research/blockplanet-level-data.md`).
- The skybox takes **the colours the stage does not use**: a stage uses two to four of the six, so two to four remain. The game's own brick colours are read at run time.
- They are darkened and slightly desaturated, for example to 40 to 60 % brightness, so the background never competes with the board.
- If a stage were ever to use all six, the skybox would fall back to a hue rotated half way round the colour wheel from the stage's average.
- A colour-blind check is to do: the stage's colours and the skybox's should differ in brightness, not only in hue.

### Performance

- The mesh has 32 × 16 = 512 bricks: about 1,000 triangles and 2,048 vertices. That is 128 `G_VTX` loads of 16, a few thousand commands. The game's own background is 260 commands, and the board draws several thousand triangles a frame, so this is the same order of work. It is trivial for RT64 on the test machine, but it should be measured before and after, as `AGENTS.md` asks.
- The vertices only need building once. Each frame changes only the rotation matrix, plus the vertex colours if they pulse. So the per-frame CPU cost is one matrix and a copy of the command list.
- Only the inner half facing the camera needs drawing, so about half the bricks can be culled.

### Motion comfort

A fast counter-spin around the viewer is the most likely thing in this design to make people feel sick. The plan:
- **A strength setting** (Options): off, gentle, normal, wild. "Wild" is the owner's high spin. The default should be gentle until it has been tried by several people.
- **A reduced-motion option:** the skybox does not counter-spin. It drifts slowly at a fixed speed, or holds still. It also follows the system's reduced-motion setting where one is available.
- **Caps:**
  - a top angular speed, so even wild stays below roughly half a turn a second;
  - no sudden starts: the spring above;
  - no flashing above 3 Hz, if pulses are added for pinch mode.
- **Readability:** keep the skybox dim and low in contrast, keep the play sphere's silhouette clear, and keep the stage's colours out of it.

### The experiment (built 2026-11-10, the owner's go: "just an experiment for now")

- **Code:** `src/graphics/brick_sky.cpp` (`include/tetrisphere/brick_sky.h`), tested in `tests/brick_sky_test.cpp`. It is off unless turned on by `BLOCKPLANET_BRICK_SKY=1` or Options > **Sky test** > Brick sky (open from the pause's Options, then Resume to see it). `BLOCKPLANET_BRICK_SKY_SET="sky_spin=wild,sky_comfort=none"` sets the others for recordings. `BLOCKPLANET_BRICK_SKY_LOG=1` logs the sky's turn twice a second.
- **The sky:** a cube-sphere of 6 × 8 × 8 = 384 bricks at radius 1,000, laid out at equal angles as the menu grid is, seen from the centre.
  - Each brick has 8 vertices: dark mortar corners and an inset face, lighter at its top edge.
  - It is drawn at the background's slot (`blockplanet_draw_filter` on the background scene) as one `G_DL` into BlockPlanet's own list at `0x807E0000`–`0x807F8000`, where the title's own use is rebuilt each title frame. About 4,000 commands.
  - It has its own 4:3 perspective, opaque with no depth, and the game's projection is loaded again after.
  - For now the colours are BlockPlanet's own presets; reading the stage's colour set comes next.
- **One more background, not a replacement** (the owner, 2026-11-10: "This new background concept is just another to the pool it shouldn't replace any"). The setting **Play backgrounds** has three choices:
  - **Original**, the default: the game's own 25 backgrounds as it picks them. Nothing of the brick sky runs, and the game's draw goes ahead as normal.
  - **Mixed**: the brick sky joins the pool and takes one stage in four in place of that stage's own background. The rule is fixed and repeatable: a hash of the mode (`0x800E44A8`), the level's background index (`0x8013D264`) and the level counter (`0x800E4498`), which stay the same through a level. So the same stages always get it, and a whole chapter never does. Each decision is logged as `brick_sky_mixed`.
  - **Brick sky only**, for testing.
  - `BLOCKPLANET_BRICK_SKY=1` means only; `=mixed` means mixed.
  - For the owner to decide: whether one in four is right, and whether the setting moves to Options > Graphics once the experiment ends.
- **Checked on the real game** (2026-11-10, the parent session):
  - **Original** draws the game's own backgrounds, with no brick-sky lines at all in the log.
  - **Mixed** at the first level of Story chapters 1 to 8 matched its log every time: the brick sky in 1:1 and the game's own background in 2:1 to 8:1. That is one in eight over that small sample.
  - **Sphere** with the stage's colours, in Story 1:1 and 6:1: the sky has none of the board's colours.
  - **Frame rate:** about 33–36 fps, the same as Cube.
- **The stage's colours are followed as they change:** they are read at every draw, so a new level or chapter, with a new colour set at `0x8013D188`, changes the sky's colours at once.
- **The Sphere layout** (the default since the owner's note, 2026-11-10: "it's cube shaped.. I was kinda hoping it could be constructed like the outside of our sphere"):
  - The sky is built like the play sphere's outside, seen from inside: 32 cells around at 11.25° (the board's cells), in 14 rows between 78.75° south and north, with the poles capped by a dark disc.
  - A seeded fill lays bricks of one to four cells over the cells, in the game's piece shapes (I, O, T, L, S, three in a line, a corner, two) with their turns, so the bricks have different shapes and sizes. The columns wrap; the rows don't.
  - Each brick is one colour, with a bevel only along its own edges, and lighter towards its top.
  - That is 512 cells of 8 vertices, with BlockPlanet's memory for the sky extended to `0x807FF000`.
  - **The Cube layout** (6 × 8 × 8 square bricks) stays as an option.
  - Still to do: the play sphere's own brick texture (the bricks are vertex-coloured for now), and the game's real brick shapes, which the type records may hold as 0/1 grids.
  - `BLOCKPLANET_HIDE_SPHERE=1` hides the play sphere for stills of the sky alone.
- **The test screen** (not saved; each change is logged as `brick_sky_settings`):
  - **Spin:** Still, Drift, Gentle 0.5, Normal 1.5, Wild 3 × the sphere's turn.
  - **Direction:** against or with the sphere.
  - **Field of view:** 40–120°, default 70°; wider looks further away.
  - **Comfort layer:** none, a dark still frame at the edges, a faint still horizon line, or both.
  - **Colours** and **Brightness**.
- **Comfort rules in the code:**
  - the sky's turn eases towards its target (a quarter of a second) and never snaps;
  - it is capped at half a turn a second;
  - it drifts slowly unless set to Still;
  - nothing flashes.
- **First run on the real game** (2026-11-10, the parent session, Story 1:1, the defaults):
  - The bricks show behind the sphere, and the sphere, pieces and HUD draw on top with no flicker. The board's lighting is unchanged.
  - Holding left and then up, the sky's yaw and pitch speeds went to about −0.5 rad/s and eased, the same sign mapping on both axes. Whether "against" looks right is for the owner to judge by eye.
  - **Frame rate** on that machine's slow GPU, the same script: about 33 fps with the sky (display lists 16–24 ms) against about 22 fps with the game's own background (23–45 ms). The sky is cheaper than the game's lit, mipmapped tunnel.
  - Pause and Resume work; Quit and the next level are not yet tested.
  - Fixed after the run:
    - the comfort frame was drawn in the 4:3 frame's units, so at 16:9 and 21:9 it showed as seams at the 4:3 edges; it now reaches the picture's own edges;
    - the default colours are now amber, since violet and blue were close to Story 1:1's blue bricks.

### Effort and risk

- **Effort:** medium:
  - the mesh and its commands (adapted from the title ring's and the menu grid's code);
  - the hook that skips the game's draw at `0x80071BA0`;
  - the spin source (a watch, then a few reads);
  - the colour choice;
  - the options.
- **Risk:** low to medium. The draw slot is known and the drawing technique is already in use. The open points are the spin-source addresses and how the off-screen background layer treats a full sphere's worth of draws.
- It could share code with pinch mode (pulses, tints) and with counter-motion (the same rotation input).

A note on ownership: backgrounds BlockPlanet draws itself (the last row) would be entirely BlockPlanet's own art and code, so they could be committed. Changes to the original ones must keep using the player's ROM at run time.
