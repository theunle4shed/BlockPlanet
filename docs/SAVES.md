# Saves and player data (BlockPlanet)

## Where data lives

BlockPlanet keeps settings, saves and caches in one folder per PC, so every profile is available whichever account starts the game:

| System | Folder |
| --- | --- |
| Windows | `C:\ProgramData\BlockPlanet` (`%PROGRAMDATA%`), shared by every Windows account on the PC. BlockPlanet creates it writable for all users; if it cannot be written, it falls back to the account's own `%APPDATA%\BlockPlanet`. |
| Linux | `$XDG_DATA_HOME/BlockPlanet`, usually `~/.local/share/BlockPlanet` (a folder shared by all accounts is not normally writable on Linux) |

- **Portable mode:** put a file named `portable.txt` beside the game executable, and everything is kept in the `data` folder there instead, as Blocksphere did. Use this for a copy on a USB stick.
- **Moving from an earlier build:** Blocksphere and earlier BlockPlanet builds kept data in `data` beside the executable. On the first start with no profile list, BlockPlanet copies what it finds there once (settings, controller mappings, the imported ROM, the cartridge save and trophies) into the new folder and the first profile. The originals are left in place.
- `TETRISPHERE_DATA_DIR` overrides the folder, for isolated test sessions.

## Layout

```
BlockPlanet/
  settings.dat                 graphics, and the default controls for new
                               profiles and guests (shared by all profiles)
  gamecontrollerdb.txt         optional extra controller mappings
  tetrisphere-us-rev0/rom.z64  the imported ROM
  diagnostics/                 one log per session
  profiles.json                the profiles and which one is active
  deleted/<id>-<time>/         deleted profiles, restorable for 30 days
  profiles/<id>/
    cartridge.eep              the game's own save (see below)
    save.json                  progress and preferences (below)
    trophies.json
    backups/cartridge.1.eep    the save as it was at the start of each of the
    backups/cartridge.2.eep    last three sessions (1 is the newest)
    backups/cartridge.3.eep
```

## Profiles

Each profile has its own cartridge save, trophies and (planned) save file with progress and preferences. The first profile is "Player 1".

- **Character:** each profile can choose a character to represent it, with a colour variant (0 to 7). The characters will be the ones rescued in the game, possibly unlocking as they are rescued, with more added later. The profile list stores the choice (`character`, `palette`); the characters are Rocket, Gear, Gyro, Stomp, Wheels and Turbine (ids `rocket` … `turbine`).
- **Guest:** `--guest` plays without a profile. The cartridge save starts fresh and stays in memory, nothing is written, and no trophies are earned.
- **Deleting** moves the profile's folder to `deleted/` and removes it from the list; it can be restored for 30 days, then it is removed at the next start. Deleting the last profile leaves a fresh "Player 1".
- **Launch and local multiplayer (with the new menus):** at launch (done; local multiplayer still to come), choose an existing profile, create one, play as a guest, or delete one; the last profile used is pre-selected. In local multiplayer each player chooses their own profile or plays as a guest, and two players cannot use the same profile. Player 1's profile owns the game's own cartridge save; other players' results go to their own save files. If player 1 is a guest, the cartridge save is not written that session.

**The profile screens** (Main > Profiles, [UI.md](UI.md#screens-connected-to-the-game)) choose, add, delete and play as a guest. With more than one profile they open at launch. The game reads its cartridge save while booting, so switching profile saves everything and restarts the game with `--profile=NAME` or `--guest` (the other launch options are kept). New profile and Rename open a name screen; Character chooses one of the six robots (Rocket, Gear, Gyro, Stomp, Wheels, Turbine, named in the ROM) and a colour variant.

Also available:

| Where | Command |
| --- | --- |
| Game | `--profile=NAME` plays as that profile (created if new) and makes it active; `--guest` plays as a guest |
| `tetrisphere-config` | `profiles` (list as JSON, with deleted ones), `profile-add NAME`, `profile-use NAME`, `profile-delete NAME`, `profile-restore NAME`, `profile-character NAME CHARACTER [COLOUR]` |

Names are 1 to 24 printable characters and are unique regardless of case.

## The profile save file (`save.json`)

Written by BlockPlanet: atomically, with the previous version kept as `save.json.bak` (used if the main file is damaged).

- **Preferences:** the profile's controls (controller family, button layout, magic button), audio (music and effects levels, Auto or Random music) and HUD panels (`hud.distance`: near, edges or follow; Graphics > HUD panels). Progress also holds the episode records (`progress.episode_records`: per mode and chapter, the best time of each level and of the whole episode, for the HUD's splits). A profile without controls of its own starts from the PC's defaults in `settings.dat`, which are then copied in. Changing controls in the game saves them to the profile, not to `settings.dat`; `tetrisphere-config bind` and `family` change the PC's defaults. A guest uses the defaults, and changes last only for the session.
- **Progress:** levels completed per mode and chapter, chapters completed per mode, puzzles solved.
- **Statistics:** games started and over, pieces placed, lives lost, levels, chapters and puzzles completed, Versus rounds won and lost, and seconds in play (not in menus).

Progress is saved at once when a level, chapter or puzzle is completed, otherwise every 30 seconds and when the game closes. It comes from the game events BlockPlanet detects (`include/tetrisphere/game_event_watcher.h`), so it can only cover what those events report. Casual mode records progress; `--start-chapter` (a testing option) does not.

## The game's own save

Tetrisphere saved to a 4-Kbit EEPROM in the cartridge: 512 bytes in 64 blocks of 8. The recompiled game still reads and writes it; BlockPlanet keeps the 512 bytes in `cartridge.eep` with a header and checksum, writing it as soon as the game saves, with a `.bak` of the previous write.

How the game saves: a save thread (`func_80029938`) reads all 64 blocks into a RAM copy at startup (pointer at `0x800F204C`) and later writes back only the blocks marked changed (flags at `0x800F2250`, one per block).

What is in it, from comparing saves after different test sessions (not yet decoded field by field):

| Bytes | Contents (tentative) |
| --- | --- |
| `0x000`–`0x07F` | Packed data, probably high-score names |
| `0x080`–`0x0DB` | Big-endian numbers that match the default score tables (2,000,000, 1,000,000, 500,000 …) and times |
| `0x0CC`–`0x15F` | Repeated 20-byte records starting `0xFF` when empty; a casual Rescue session changed one |
| `0x1A4`–`0x1FB` | Flags and options; block 61 (`0x1E8`) changes after playing each mode, blocks 52–53 after controller tests |
| `0x1FC`–`0x1FF` | A checksum, changed by every save |

## Planned

Agreed layout (2026-11-02):

- **`save.json` (done, first version):** later additions are unlocks, tutorials, casual mode as a preference, HUD and accessibility options, and the Cooperative colour and shape.
- **More statistics** (each needs a new game event, see [TROPHIES.md](TROPHIES.md#events)):

  | Statistic | Needs |
  | --- | --- |
  | Blocks destroyed (lifetime) | `blocks_destroyed`: finding where the game counts cleared blocks |
  | Largest combo | `combo`: the combo counter |
  | Pieces sent to other players in VS | `junk_sent`: the junk the game sends |
  | Largest combo contributed in Cooperative | `coop_combo` (Cooperative mode) |
  | Local and online battles won and lost | `battle_result` (local VS with people now; online later) |
  | Magic (power weapons) used | `magic_used` |
  | Blocks destroyed by magic | `blocks_destroyed` with `by_magic` |
- **`settings.dat` stays shared by the PC:** resolution, window, aspect, filters, sphere detail, audio output.
- **`records.json`, shared by the PC:** best times, high scores and Time Trial runs per mode, each entry tagged with the profile that set it, viewable for everyone or one profile. Guests do not set records. Daily challenges and online leaderboards can build on it.
- Decoding the cartridge save, so its scores and names can appear in the new menus and be copied into `records.json`.
- Characters unlocking as they are rescued (needs the chapter each robot is rescued in), the character shown in the game, and art for the colour variants.
