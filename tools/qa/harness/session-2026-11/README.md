# Harness scripts from the session of 2026-11-01 to 2026-11-24

Copies of the scripts used in that session (docs/HANDOVER-2026-11-24.md). They contain no game data. They expect `BP_HARNESS` (default `/tmp/bp-harness`) to be the folder holding them, with their outputs written beside them (`cv-NAME/`): copy this folder there first.

- `run98.sh` starts the Linux build in Xvfb on display :98; `nav98.sh OUT SCRIPT` drives it (its commands are listed at its top: wait, key, pad, poke, rec, wshot...). `nav_gdb.sh` / `run_gdb.sh` the same under gdb.
- `bar.txt` (the Missile level 3 test), `mm.txt` (a Casual 1:1 start, the first ten lines), `fc3.txt`, `life1.txt`: example scripts.
- `zoom.py NAME T0 T1 N OUT [crop]` makes a sheet of N frames from `cv-NAME/rec.mp4`; `sheet.py`, `one.py` similar.
- `measure.sh FAMILY LEVEL TAG [ENV...]` fires a power-up on a fixed sphere (Casual 10:5, seeds 4242 and 777) and prints the bricks removed per layer.
- `recall.sh`: records a power-up's use in 10:5. `strikes2.py`, `halves.py`: count the cells each barrage strike lowered (from `research_columns` dumps).
- `audit_rescue_endings.sh`: records the boot and each Rescue episode's last stage won (the minimal viable build's audit).
