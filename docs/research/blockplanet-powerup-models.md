# Power-up models: BlockPlanet's own 3D (research, 2026-11-21)

The owner's decision: keep the power-ups 3D, redrawn in the flat icons' style, with level 2 and 3 forms (the flat icons stay in the Magic box; the models show in flight and in the effect).

## How the game draws them

`BLOCKPLANET_MAGIC_DL_LOG=1` (with `BLOCKPLANET_MAGIC_MODELS=1`) logs, once per kind, the commands the model helper (`0x8002D93C`) writes for the held power-up, from the display list pointer (`0x800F22B4`) at its start to its end. The helper writes **straight into the frame's display list (F3D)**, no called lists: pipe syncs and modes, the projection (`G_MTX` load), the model matrices, a white light (`G_MV_L0`: colour `FFFFFF`, direction `5A 33 C5`) and a grey ambient (`G_MV_L1`: `3F3F3F`), lighting on (`G_LIGHTING`), then for each part a texture load (the colours are in the textures, lit white), a `G_VTX` from the built scene in RAM and `G_TRI1`s (F3D: vertex index × 10). Some parts load a new matrix of their own.

| Kind | Commands | Matrices | Vertices | Triangles |
| --- | --- | --- | --- | --- |
| 0 Firecracker | 164 | 5 | 86 | 64 |
| 1 Dynamite | 311 | 3 | 252 | 208 |
| 2 Electro Magnet | 400+ | 5 | 327+ | 243+ |
| 3 Atom | 400+ | 15 | 542+ | 182+ |
| 4 Bomb | 265 | 3 | 167 | 128 |
| 5 Ray Gun | 400+ | 3 | 564+ | 260+ |

(400+: the log stops at 400 commands.) Units are large: the Firecracker runs along z from about +1,600 (the fuse's end) to -5,800 (the cone's tip) and about 1,000 across; the Magnet's base ring is at z 148 to 726, its claws reach z -3,700.

## Replacing them

`src/graphics/powerup_models.cpp`: at the helper's start (the draw filter, `blockplanet_magic_flight`) the power-up's draw is marked; at its end (`blockplanet_model_draw_end`) everything from its first `G_VTX` on is cut and BlockPlanet's model written in its place, in the first part's space, so the game's matrices (placing, turning, flight) and its light's direction are kept. Each part is one flat colour through the lights (the light the part's colour, the ambient 45 % of it), texture off, `G_CC_SHADE`. A dark outline comes first: each part a little larger, front faces culled (its inside), lit dark. Vertices and lights live at `0x807FA000`–`0x807FFF00` (free upper memory; the title's own drawing uses `0x807E0000`–`0x807F7FFF`, the lessons `0x807F8000` on).

**Proof (2026-11-21):** the Firecracker as a purple striped tube, an orange cone and a light fuse, drawn in the Magic box (`BLOCKPLANET_MODELS3D=1 BLOCKPLANET_MAGIC_MODELS=1`), lit by the game's light and turning with its animation. The outline barely shows yet.

**All six, three levels each (2026-11-21):** built from simple shapes (tubes and cones, boxes, balls, rings, chains of tubes for curved claws and fuses) in each model's own space and units, from the bounds logged for each (Dynamite along y about ±1,500, its fuses above; the Magnet's claws along -z from a ring at about +700; the Atom about its middle, electrons about 1,400 out; the Bomb along z, nose to -z; the Ray Gun along z). Levels: Firecracker bigger, then three rockets; Dynamite three, five, then seven sticks with as many fuses as the level; Magnet a horseshoe, a bigger one, then four claws (the game's own form), all on the green ring; Atom more electrons, then a fourth orbit; Bomb a bigger flame, then two missiles; Ray Gun a longer, wider beam, then a glowing one. Each triangle is turned to face away from its shape's centre (the outline shell's inside faces, front faces culled, showed instead of the colour when a shape's own order faced in). Each model is built once (a marker word, then its commands, lights and vertices) at `0x80700000`–`0x8073C000` and called from the frame's list with one `G_DL`, so the frame's list grows by one command; the first area tried (`0x807E0000`) is written every frame in play and crashed the graphics thread when used. Sizes: 120 triangles (the Firecracker) to 752 (the level 3 Atom), 14 to 87 KB each. Shown in the Magic box with `BLOCKPLANET_MODELS3D=1 BLOCKPLANET_MAGIC_MODELS=1`; guest byte `0x807FFF00` (1 to 3) sets the level for research.

**Still to do:** the flight and the effect (the flight is its own model object, found after use; the effect's draws to be checked); the outline's thickness; level-specific sounds (the owner: higher or harder at levels 2 and 3); and, with power-up levels, more pieces spread evenly and flying in from the Magic box's side of the screen (the owner).

## In flight and in the effect (2026-11-21)

`BLOCKPLANET_MAGIC_FLIGHT_LOG=1` logs, after a power-up is used, each model drawn through its matrix. The **flying model draws its vertices from the same place as the held model** (the Firecracker's `0x801A61C0`, the Magnet's `0x801A3B50`), for about 3 seconds (the Firecracker) to 4 (the Magnet, as it pulls); the **core's character** is drawn through the same matrix throughout, from its own vertices (`0x801E5820`), and must be left alone; other objects drawn later (from other vertices) are the effect's own pieces. So: the held draw is marked by the hook just before it (`blockplanet_held_magic_draw`), its first vertex source recorded; after use, for 10 seconds, any draw through the matrix whose first vertices come from there is replaced. (Marking every draw through the matrix while held first recorded the core character's source and replaced the character.) The models are drawn from both sides (no culling): the effect can draw the model mirrored, which turned the front faces round and showed the dark outline shell; the outline is off by default (`BLOCKPLANET_MODEL_OUTLINE`). The ambient share raised from 45 % to 65 % (dim in the effect). `BLOCKPLANET_MODELS3D_LOG=1` logs each replacement. Checked: the Magnet flies out and pulls bricks as BlockPlanet's horseshoe, its tips towards the sphere as the game's claws, the core's character untouched.

## Launch review (2026-11-22, the owner's request)

Each use recorded at 30 frames a second in Casual 10:5, ours beside the original (`BLOCKPLANET_MAGIC_FLIGHT_LOG=1` lists each model drawn through the flight matrix after use: object, first vertex source, vertex count, time).

| Power-up | The game's draws after use | Ours now |
| --- | --- | --- |
| Firecracker (Missile 1) | One model (its source while held), then the flame trail and smoke rings in the same draw | Our missile, nose first; the game's trail and rings kept. It vanishes at about 3.1 s where the original's rocket visibly dives into the sphere (to look at) |
| Bomb (Missile 2, 3) | The held model to 2.3 s; then a second, large model (about 2,500 vertices, its own object) that dives into the sphere and shrinks; blast effects (76 and 84 vertices) | Our missile in both phases; the second phase's nozzle faces the camera as the original's |
| Dynamite | The bundle to 1.9 s; then **five sticks**, each its own object of 32 vertices (1.7 to 3.7 s); then five blasts (76 and 84 vertices) | Our bundle (five sticks at every level), then five of our sticks |
| Magnet | One model to about 4.2 s, its vertex count rising with its arcs; its last moments not through the flight matrix | Ours throughout, lit (the game sets two lights here) |
| Ray Gun | One model to about 7 s | Ours throughout, aimed at the cursor as the original |
| Atom | One model to about 3 s | Ours throughout; the original glows white in flight, ours does not (the glow may be part of the body draw) |
