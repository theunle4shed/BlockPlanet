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

Follow the original game's 1990s visual language and low-resolution menu font. Review native captures of affected screens in 4:3 and widescreen before delivering a UI change. `docs/qa/ui-design-supervision.md` records Blocksphere's design decisions.

## Releases

Use plain version numbers. Release notes focus on features, setup, controls, updates, and bug reports, and credit Blocksphere.
