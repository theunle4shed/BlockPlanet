# Floating text in play: the catalogue (2026-11-15)

Everything that floats over the sphere or flashes up in Rescue (and the modes built on it), what draws it, and what BlockPlanet does with it today. **Nothing here is removed or changed until the owner has decided** (the owner, 2026-11-15: "before removing them, let's document them and decide"). The owner's earlier direction (docs/ATTACK_MODES_REVIEW.md): in Rescue, combo words rather than numbers over the sphere.

## The game's own

| # | What | When | Look | Drawn by | Today |
|---|---|---|---|---|---|
| G1 | **Small floating points** ("300", "400", "500") | After a drop that scores, or a small clear | White digits rising from the drop | The game's floating score (routine not yet found) | Shown in every mode |
| G2 | **Score bubbles** (1K, 2K, 3K, 5K…) | During a combo, over the bricks as they break; "1K" also for turning filler into a brick | A coloured bubble with the points: 1K blue, 2K purple, 3K pink, 5K red | Not yet found (one picture per value, or a bubble with digits?) | Shown in every mode |
| G3 | **The large grey 3D number** ("19000", "30100") | A few seconds after a big combo or a stage's last clear | Large grey 3D digits across the sphere | A 3D model first drawn at the clear (candidates: `0x801BE090`, `0x801AC880`; to confirm) | Shown in every mode |
| G4 | **"Nice Job"** | A stage's end | Large silver 3D letters | The game's stage-end scene | Shown |
| G5 | **The light burst and flying bricks** | As the core opens | White rays, white bricks flying out | The game's stage-end scene | Shown |
| G6 | **The game's combo counter** (teal number, top right) and its spark ring | During combos | Teal digits and a spark ring | Text draw at about (268, 32); the ring is not found yet | Number hidden in Rescue (BlockPlanet's HUD shows the combo); the ring still shows |

## BlockPlanet's own

| # | What | When | Today |
|---|---|---|---|
| B1 | **Combo counter in the HUD**: "Combo" counting up, then "Last combo", gold with "+ Magic" at 20 or more, and the stage's best | During and after combos | Shown in Rescue and Casual |
| B2 | **Score list** under Score | Each time points are added | Shown in Rescue and Casual; a combo is one entry with its block count ("+300 ×3"), a drop on its own |
| B3 | **"+N s"** over the sphere | A combo in Score Attack adds time | Shown (new, 2026-11-15) |
| B4 | **Points pop-ups** over the sphere | A combo ends | Built but off (`BLOCKPLANET_POPUPS=1` for testing) |
| B5 | **Combo words** over the sphere ("Combo ×12", "Gravity combo", "Fuse combo", "+ Magic") | Notable moves | Not built yet (the owner's direction) |

## For the owner to decide

For each of G1, G2, G3 and G6's ring: keep in Rescue, hide in Rescue, or replace with BlockPlanet's own (B5)? And whether the score modes (Score Attack) keep the game's numbers, since score matters there.

## The owner's decision (2026-11-15)

For Rescue: **keep the game's own floating numbers** (G1 to G3) for now, and make sure **the HUD's score list reports the right thing and makes sense**, naming the kind of combo when there is one. Built:

- A combo is one entry: its total and block count ("+30,100 ×21"), growing while it runs; points outside a combo (a drop) show on their own.
- Under a combo's entry, in smaller blue text, its kind: **Gravity combo** (the game's gravity count, player context `+0x2AE6`, rose during it: a piece fell into a match mid-combo, `func_8009423C` → `func_800C7FC4`), **Fuse combo** (`+0x2AE8` rose: a piece was dropped into a combo still running, in `func_800CD3B8` at `0x800CE9A8`), and **+ Magic** (the held magic changed during it). Both counts also add 3 to the magic meter. `BLOCKPLANET_GAIN_SAMPLE=1` fills the list with the longest line, to check the layout.

B5 (combo words over the sphere) is not built: the HUD carries them instead.

