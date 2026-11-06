#!/bin/bash
# Drives the game from a script while it runs (run.sh), for screenshots,
# recordings and research. Usage: nav.sh <outdir> <script> [game args...]
# Script lines:
#   wait S            sleep S seconds
#   rewin S           wait S, then pick up the game window again (after boot)
#   key NAME          press a key (xdotool names: Return, Escape, F1, F2 ...)
#   type TEXT         type text
#   pad BUTTON        press a virtual controller button (south, east, west,
#                     north, start, back, up, down, left, right, ...);
#                     padd / padu: hold / release
#   size W H          resize the window
#   shot NAME         screenshot of the whole display -> NAME.png
#   wshot NAME        screenshot of the game window -> NAME.png
#   rec S             record the display for S seconds -> rec.mp4 (ffmpeg)
#   poke LINE         write one line to the poke file (BLOCKPLANET_POKE_FILE):
#                     "0xADDR:SIZE=VALUE" stores into guest memory,
#                     "cmd:NAME" runs a menu command (e.g. cmd:chapter:1)
#   pokefile PATH     copy a whole poke file
# Run with: TETRISPHERE_QA_CONTROLLERS=<out>/data/qa.jsonl BLOCKPLANET_POKE_FILE=<out>/poke.txt
# (and an empty qa.jsonl created first). F2 saves guest memory when
# BLOCKPLANET_DUMP_DIR is set. Research switches: docs/research/*.md.
here=$(cd "$(dirname "$0")" && pwd)
out=$1; script=$2; shift 2; seq=0
"$here/run.sh" 400 "$out" "$@" &
sleep 4; export DISPLAY=:99
win=$(xdotool search --name Tetrisphere | head -1)
press() { seq=$((seq+1)); echo "{\"seq\":$seq,\"port\":0,\"button\":\"$1\",\"pressed\":$2}" >> "$out/data/qa.jsonl"; }
while read -r cmd arg; do
  case $cmd in
    wait) sleep "$arg";;
    rewin) sleep "$arg"; win=$(xdotool search --name Tetrisphere | tail -1);;
    key) xdotool windowfocus --sync "$win" 2>/dev/null; xdotool keydown --window "$win" "$arg"; sleep 0.15; xdotool keyup --window "$win" "$arg";;
    type) xdotool windowfocus --sync "$win" 2>/dev/null; xdotool type --window "$win" --delay 120 "$arg";;
    size) xdotool windowsize "$win" $arg; sleep 1.5;;
    pad) press "$arg" true; sleep 0.15; press "$arg" false;;
    padd) press "$arg" true;;
    padu) press "$arg" false;;
    poke) echo "$arg" > "$out/poke.txt.tmp" && mv "$out/poke.txt.tmp" "$out/poke.txt"; sleep 0.3;;
    pokefile) cp "$arg" "$out/poke.txt.tmp" && mv "$out/poke.txt.tmp" "$out/poke.txt"; sleep 0.3;;
    rec) (ffmpeg -loglevel error -y -f x11grab -framerate 30 -video_size 1280x960 -i :99 -t "$arg" "$out/rec.mp4" &);;
    shot) import -window root "$out/$arg.png";;
    wshot) import -window "$win" "$out/$arg.png";;
  esac
  echo "$(date +%s%3N) $cmd $arg" >> "$out/nav.log"
done < "$script"
pkill -INT -x tetrisphere-m1; wait
