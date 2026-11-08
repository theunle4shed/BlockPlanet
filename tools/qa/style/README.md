# Style captures (approximate)

Renders BlockPlanet's menus (RML/RCSS) and mock-ups of the HUD and cards in
Chromium, without the game, for checking the style (docs/STYLE.md) at several
window sizes. Close to RmlUi, not identical; a plain gradient stands in for
the game's picture. The HUD mock-ups in `hud/` follow the markup that
`src/ui/ui_system.cpp` writes, with sample values.

    python3 tools/qa/style/render.py OUT [other-menu.rcss other-hud.rcss]
    node tools/qa/style/shoot.js OUT [name filter]

`BLOCKPLANET_TEXT_SCALE=1.4` renders at the Larger text size (0.85 Small,
1.2 Large). Captures stay out of Git (AGENTS.md).
