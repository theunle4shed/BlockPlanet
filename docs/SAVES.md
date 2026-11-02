# Saves and player data (BlockPlanet)

## Where data lives

BlockPlanet keeps settings, saves and caches in the standard per-user folder:

| System | Folder |
| --- | --- |
| Windows | `%APPDATA%\BlockPlanet` |
| Linux | `$XDG_DATA_HOME/BlockPlanet`, usually `~/.local/share/BlockPlanet` |

- **Portable mode:** put a file named `portable.txt` beside the game executable, and everything is kept in the `data` folder there instead, as Blocksphere did. Use this for a copy on a USB stick.
- **Moving from an earlier build:** Blocksphere and earlier BlockPlanet builds kept data in `data` beside the executable. On the first start with no profile list, BlockPlanet copies what it finds there once (settings, controller mappings, the imported ROM, the cartridge save and trophies) into the new folder and the first profile. The originals are left in place.
- `TETRISPHERE_DATA_DIR` overrides the folder, for isolated test sessions.

## Layout

```
BlockPlanet/
  settings.dat                 graphics and controls (shared by all profiles)
  gamecontrollerdb.txt         optional extra controller mappings
  tetrisphere-us-rev0/rom.z64  the imported ROM
  diagnostics/                 one log per session
  profiles.json                the profiles and which one is active
  profiles/<id>/
    cartridge.eep              the game's own save (see below)
    trophies.json
    backups/cartridge.1.eep    the save as it was at the start of each of the
    backups/cartridge.2.eep    last three sessions (1 is the newest)
    backups/cartridge.3.eep
```

## Profiles

Each profile has its own cartridge save and trophies. The first profile is "Player 1". `--profile=NAME` plays as that profile, creating it if it is new, and makes it the active one for later starts. Names are 1 to 24 printable characters and match regardless of case. A profile menu will come with the new menus.

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

- A BlockPlanet progress file per profile (`progress.json`): chapters completed (for the chapter select), statistics and records, written from the game events BlockPlanet already detects. It can grow without the 512-byte limit.
- Decoding the cartridge save, so its scores and names can appear in the new menus.
- A profile menu in the new menus.
