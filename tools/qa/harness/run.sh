#!/bin/bash
# Runs the Linux build in a virtual display for a set time.
# Usage: run.sh <seconds> <outdir> [game args...]
# Needs Xvfb, a built .local/m1/build-gcc/tetrisphere-m1 and the ROM at
# .local/m0/tetrisphere-us.z64 (both private, not in Git).
secs=$1; out=$2; shift 2
repo=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$out/data"
export TETRISPHERE_DATA_DIR="$out/data"
export VK_ICD_FILENAMES=${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}
export SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-dummy}
export DISPLAY=:99
pgrep -x Xvfb >/dev/null || (Xvfb :99 -screen 0 1280x960x24 >/dev/null 2>&1 &); sleep 1
cd "$repo"
timeout -s INT "$secs" .local/m1/build-gcc/tetrisphere-m1 --windowed --perf-log "$@" .local/m0/tetrisphere-us.z64 \
  > "$out/stdout.log" 2> "$out/stderr.log"
echo "exit $?" >> "$out/stdout.log"
