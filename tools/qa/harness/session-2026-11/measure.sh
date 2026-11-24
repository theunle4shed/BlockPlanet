#!/bin/bash
# measure.sh FAMILY(1-4 keypad) LEVEL TAG [extra env...]
S=${BP_HARNESS:-/tmp/bp-harness}; cd /home/user/blockplanet
k=$1; lv=$2; tag=$3; shift 3
v=ms-$tag-$k-$lv
sed -n '1,11p' $S/bar.txt > $S/$v.txt
for i in $(seq 1 $lv); do printf 'key KP_%s\nwait 0.5\n' $k >> $S/$v.txt; done
printf 'wait 1\npoke cmd:research_snap\npoke cmd:research_board\nwait 0.5\npoke cmd:research_magic_retry\nwait 18\npoke cmd:research_diff\nwait 0.5\npoke cmd:research_board\nwait 1\n' >> $S/$v.txt
rm -rf $S/cv-$v; mkdir -p $S/cv-$v/data; : > $S/cv-$v/data/qa.jsonl
env "$@" BLOCKPLANET_SPHERE_RESEARCH=1 BLOCKPLANET_SPHERE_SEED=4242 BLOCKPLANET_PIECE_SEED=777 BLOCKPLANET_POWERUP_TEST=1 BLOCKPLANET_MODELS3D=1 TETRISPHERE_QA_CONTROLLERS=$S/cv-$v/data/qa.jsonl BLOCKPLANET_POKE_FILE=$S/cv-$v/poke.txt timeout 100 $S/nav98.sh $S/cv-$v $S/$v.txt > /dev/null 2>&1
python3 - $S/cv-$v/stdout.log $k $lv <<'PY'
import json,sys,re
L=open(sys.argv[1]).read().splitlines()
h=[re.search(r'"hash":"?([0-9a-fA-Fx]+)',l) for l in L if 'level_build_end' in l]
hashes=[m.group(1) for m in h if m]
d=[json.loads(l) for l in L if '"research_diff"' in l]
b=[json.loads(l) for l in L if '"research_board"' in l and '"layers"' in l]
cells=None
if d:
    e=d[-1]; key=[k for k in e if isinstance(e[k],list) and e[k] and isinstance(e[k][0],list)]
    cells=e[key[0]] if key else []
lay=[]
if len(b)>=2: lay=[x-y for x,y in zip(b[0]['layers'],b[-1]['layers'])]
cols=len({(c[0],c[1]) for c in cells}) if cells else 0
fam=['Missile','Dynamite','Magnet','Atom'][int(sys.argv[2])-1]
print(f"{fam} L{sys.argv[3]}: bricks {len(cells) if cells is not None else '?'} columns {cols} per-layer {lay} sphere {hashes[:1]} exit {L[-1] if L else ''}")
PY
