# QA harness (Linux)

Scripts used to drive the game for screenshots, recordings, memory dumps and
research (docs/research). They contain no game data; the ROM and the build
are private (`.local/`).

- `run.sh` starts the build in Xvfb; `nav.sh` drives it from a script
  (commands listed at its top); `story_start.txt` starts Story 1:1.
- `rdram.py` reads F2 memory dumps.
- Example: `d=/tmp/out; mkdir -p $d/data; touch $d/data/qa.jsonl;
  TETRISPHERE_QA_CONTROLLERS=$d/data/qa.jsonl BLOCKPLANET_POKE_FILE=$d/poke.txt
  tools/qa/harness/nav.sh $d tools/qa/harness/story_start.txt --casual`
- Handy pokes: every layer-1 cell (`0x801173F0`..`0x80117BEE` in Rescue 1:1)
  set to 288 wins the level in one combo; `0x80110922:1=N` gives magic N (0-5).
