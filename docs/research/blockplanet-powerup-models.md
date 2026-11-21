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

**Still to do:** the other five models and the level 2 and 3 forms; the flight and the effect (the flight is its own model object, found after use; the effect's draws to be checked); the outline's thickness; level-specific sounds (the owner: higher or harder at levels 2 and 3); and, with power-up levels, more pieces spread evenly and flying in from the Magic box's side of the screen (the owner).
