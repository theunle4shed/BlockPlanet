# Style captures (approximate)

Renders BlockPlanet's menus (RML/RCSS) and mock-ups of the HUD and cards in
Chromium, for the style review (docs/STYLE.md), without the game. Close to
RmlUi, not identical; no game picture behind them.

    python3 tools/qa/style/render.py OUT [proposed/menu.rcss proposed/hud.rcss]
    node tools/qa/style/shoot.js OUT [name filter]

`proposed/` holds the draft style sheets behind the "after" captures; they
are not used by the game. Captures stay out of Git (AGENTS.md).
