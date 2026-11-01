# BlockPlanet contributor instructions

BlockPlanet continues Blocksphere by djultra64. These instructions apply to people and AI tools working in this repository.

## Private data

Keep ROMs, generated game code, extracted original assets, captures, audio, saves, and credentials out of Git and shared packages. Generated code comes from the ROM and stays private, like the ROM itself.

## Attribution

- Credit Blocksphere and djultra64 for all work up to fork commit `53203b6`. Never rewrite or re-attribute that history.
- Record BlockPlanet changes in `CHANGES-SINCE-BLOCKSPHERE.md` with the date.
- Preserve authentic third-party copyright and license notices, `LICENSE`, `THIRD_PARTY_NOTICES.md`, and `licenses/`.
- Keep the README's AI disclosure, Blocksphere credit, challenges, and full creator and dependency credits accurate. New documentation is in English.

## Performance work

Measure before and after. A change that claims a speed-up should say how it was measured (scene, settings, hardware) and what the numbers were. Do not change the original game's simulation speed, timers, or audio pitch to gain frame rate.

## UI changes

New menus are modern, simple and TV-like: large readable text, clear focus states, few items per screen, quick transitions, and controller-first navigation. They do not imitate the original 1990s menus, and there is no "classic" mode. Keep the existing in-game HUD until its replacement art is ready (see `docs/HUD_INVENTORY.md`). Review captures of affected screens at several aspect ratios before delivering a UI change. `docs/qa/ui-design-supervision.md` records Blocksphere's earlier design decisions for reference.

## Releases

Use plain version numbers. Release notes focus on features, setup, controls, updates, and bug reports, and credit Blocksphere.
