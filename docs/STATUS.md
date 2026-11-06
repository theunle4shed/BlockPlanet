# Status and handoff (BlockPlanet)

Where the project stands, for picking up in a new session. Updated 2026-11-06. Read this first, then `AGENTS.md`.

## Working setup

- Branch `claude/teyrisphere-performance-hqetcp` (repository `theunle4shed/BlockPlanet`, formerly `blockplanet`).
- Private, not in Git: the ROM (`.local/m0/tetrisphere-us.z64`), the generated code (`.local/m1/linux-generated`) and the build (`.local/m1/build-gcc`). Regenerate after changing hooks in `config/recomp/tetrisphere.us.rev0.toml` (the output directory must not exist yet):

  ```
  rm -rf .local/m1/linux-generated .local/m1/linux-generated-manifest.json
  python3 -m tools.recomp.generate generate --rom .local/m0/tetrisphere-us.z64 \
    --output .local/m1/linux-generated --manifest .local/m1/linux-generated-manifest.json \
    --recompiler .local/m1/toolchain/n64recomp-build/N64Recomp
  ```

  If it reports a receipt mismatch: `python3 -m tools.recomp.generate create-receipt`. A hook needs `before_vram` unless it is the function's only entry hook. Build with `ninja` in `.local/m1/build-gcc`; `ctest` runs 70 tests.
- Testing on Linux: `tools/qa/harness/` (README there): Xvfb, a virtual controller, screenshots, recordings, memory pokes and F2 memory dumps.
- Owner's rules (AGENTS.md): Windows first; private data out of Git; record changes in `CHANGES-SINCE-BLOCKSPHERE.md`; modern UI checked at several screen shapes; no model names in commits; the owner designs the art.

## Done recently

- **Story HUD** (`docs/HUD_DESIGN.md`): one tinted container per side, section titles, placed as on 4:3 (Graphics > HUD panels: near, edges, follow). Left: health and drop timer, magic (the game's own 3D model turning in the item box, its flight starting from the box; the box's tint drawn by the game behind it), next pieces (bar, square, S, T). Right: mode, stage and progress bar; LiveSplit-style episode splits against per-stage bests saved in the profile. Portrait: rows above and below.
- **Title screen:** the game's own 3D letters held on screen; the painted picture hidden until replaced (`src/graphics/title_hooks.cpp`).
- Profiles, Chapter 0 (Training), pause menu, audio and options, restart level/chapter (earlier in the session; see `CHANGES-SINCE-BLOCKSPHERE.md`).

## Open threads (next steps)

1. **HUD for the other modes** (Time Trial, Puzzle, Hide & Seek with no progress bar, Lines, VS): designs agreed in `docs/HUD_DESIGN.md`; the Story HUD is the pattern.
2. **Title rebuild** (`docs/research/blockplanet-game-state.md`, "The title screen's artwork"): the ring and characters to replace (from game models: the gold robot is in the intro, the rockets are the Rocket magic, the crab is still to find); copyright lines to read from the title picture in the player's ROM at install and show as text (never written into the repository). Face-on letters: experiment off (letters drift; their true centre is still to find). A fake drop shadow for letter-on-letter shading is an idea.
3. **The game's floating number** (parked): a grey 3D number after big combos ("6000", "48000"), not the points total; to hide in Story and possibly reuse in score modes. Real-drop combo tests need the matching blocks under the drop, not beside it.
4. **Research loose ends:** what each magic does and its name; magic crystals; fuse vs gravity combos; CONNECT and CRYSTAL TOWER goals; the two unseen piece shapes (IDs 3, 4).
5. **Saved for later** (`docs/ROADMAP.md`): seeded stages for fair stage time trials, new block types, new modes (Escape, Multiphasic, Score Attack, Lines variants, VS and co-op), the scanner magic, arena walls, attract-mode demos, credits scripting, music (BGM) editing.
6. **The owner to test on Windows**: much has changed since the last Windows test.

## Decisions to keep

- Sphere framing as the original; Story's Try Again as the original.
- Story shows no score pop-ups; Hide & Seek shows no progress bar (hints would spoil the search).
- Any finished Story stage counts towards that stage's best, however the run started.
- No HD texture packs; sharpness through redesign and the game's own models. Nothing copied from the game is committed; anything derived from it is generated from the player's ROM.
