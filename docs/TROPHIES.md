# Trophies

BlockPlanet has built-in trophies. They work without any launcher or store. A launcher integration such as Quiver, or Steam, can be added later as one more connector without changing the trophies or their triggers.

## How it fits together

```
game hooks ──GameEvent──▶ TrophyEngine ──▶ stats and unlocks ──▶ sinks
 ("combo",                 (trophies.json)    (trophies.json        ├─ log: console and diagnostics
  size 12, …)                                  in data/)            ├─ on-screen notification (with the new UI)
                                                                    └─ launcher connector (Quiver, Steam, …)
```

- **Game hooks** only report what happened: a combo, a finished level, a magic use. They never decide about trophies.
- **`TrophyEngine`** (`include/tetrisphere/trophies.h`) applies the definitions: it updates stats, unlocks trophies, and tells every sink once per unlock.
- **Definitions** are data in `assets/trophies/trophies.json`, copied beside the executable. Adding or changing a trophy needs no code, only an event that the game already reports.
- **Progress** is saved to `data/trophies.json` with a `.bak` backup, separate from the N64 EEPROM save. It is saved on every unlock, every 30 seconds while stats change, and when the game closes.
- **Online guests** earn nothing. While a guest plays a stage the host unlocked, `set_awards_enabled(false)` stops both stats and unlocks.

## Definition format

```json
{
  "schema_version": 1,
  "stats": [
    { "id": "pieces_placed", "event": "piece_placed" },
    { "id": "biggest_combo", "event": "combo", "add_field": "size", "combine": "max" }
  ],
  "trophies": [
    {
      "id": "combo_10", "name": "Big Chain", "tier": "silver",
      "description": "Score a combo of 10 or more pieces.",
      "trigger": { "event": "combo", "where": { "size": { "at_least": 10 } } }
    }
  ]
}
```

**Stats** count or track events. Each matching event adds `add` (default 1) or the value of `add_field`. `"combine": "max"` keeps the largest value instead of a sum. `where` filters which events count.

**Trophies** have an `id` (lowercase letters, digits and `_`), `name`, `description`, `tier` (`bronze`, `silver`, `gold` or `platinum`), optional `hidden`, and one trigger:

| Trigger | Unlocks when |
| --- | --- |
| `{ "event": "combo", "where": { … } }` | That event happens and every condition matches. |
| `{ "stat": "pieces_placed", "at_least": 1000 }` | The stat reaches the target. |
| `{ "all_of": ["a", "b"] }` | All the listed trophies are unlocked. An empty list means every other trophy, which is how a platinum works. |

**Conditions** in `where` compare event fields: `"mode": "rescue"` (equals), `{ "not": "vs" }`, `{ "at_least": 10 }` and `{ "at_most": 1 }`. Every condition must match.

The engine rejects a definitions file with unknown stats, unknown trophies in `all_of`, duplicate ids, unknown tiers or operators, or non-integer comparisons. In that case the game runs without trophies and reports why.

## Events

The trophy list depends on these events. **None are reported by the game yet:** each needs a hook into the game code, found through the same kind of research as the HUD inventory. Field names are part of the format, so keep them stable once released.

| Event | Fields | Reported when |
| --- | --- | --- |
| `mode_started` | `mode` | A game of any mode begins. |
| `piece_placed` | `mode` | A piece lands. |
| `combo` | `mode`, `size`, `score`, `multiplier` | A combo scores. |
| `magic_used` | `mode`, `type` | Magic is launched. |
| `life_lost` | `mode` | A heart is lost. |
| `level_completed` | `mode`, `chapter`, `level`, `score`, `hearts_lost` | A level is cleared. |
| `chapter_completed` | `mode`, `chapter`, `final` (1 for the last chapter) | A chapter is cleared. |
| `puzzle_solved` | `puzzle`, `drops`, `slides`, `seconds` | A puzzle is solved. |
| `vs_round` | `result` (`win` or `loss`), `opponent` | A VS round ends. |
| `game_over` | `mode`, `score` | A game ends. |

Mode names: `rescue`, `hide_and_seek`, `puzzle`, `time_trial`, `vs`, `vs_cpu`, `practice`, `training`, `lines`.

## Connecting a launcher

Implement `TrophySink::trophy_unlocked` and register the sink with `add_trophy_sink()` (`include/tetrisphere/trophy_service.h`). The sink receives each unlock once, with its id, name, description and tier. A launcher that needs a full sync at startup can read `trophy_engine().progress()`.

Quiver's trophy or achievement API is not publicly documented as of 2026-11-01. See [ROADMAP.md](ROADMAP.md).

## Tests

`tests/trophies_test.cpp` covers event and stat triggers, conditions, `max` stats, chains of `all_of` trophies including an empty list, no double unlocks, sinks called once, guests earning nothing, rejected definitions, saving with a backup and recovery from a corrupt file, and that the shipped `assets/trophies/trophies.json` loads.
