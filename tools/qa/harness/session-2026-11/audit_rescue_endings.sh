#!/bin/bash
S=${BP_HARNESS:-/tmp/bp-harness}; cd /home/user/blockplanet
# boot
v=aud-boot; cat > $S/$v.txt <<'X'
rec 75
rewin 30
wait 10
key Return
wait 6
key Return
wait 6
poke cmd:open_story
wait 6
key Down
wait 2
key Down
wait 2
key Up
wait 2
key Up
wait 8
X
rm -rf $S/cv-$v; mkdir -p $S/cv-$v/data; : > $S/cv-$v/data/qa.jsonl
TETRISPHERE_QA_CONTROLLERS=$S/cv-$v/data/qa.jsonl BLOCKPLANET_POKE_FILE=$S/cv-$v/poke.txt timeout 100 $S/nav98.sh $S/cv-$v $S/$v.txt > /dev/null 2>&1
echo "$v $(tail -1 $S/cv-$v/stdout.log)"
for ep in 1 2 3 4 5 6 7 8 9 10; do v=aud-ep$ep
cat > $S/$v.txt <<X
rewin 18
key Return
wait 4
key Return
wait 4
poke cmd:open_story
poke cmd:story
poke cmd:chapter:$ep:10
wait 8
poke cmd:pause_resume
wait 3
rec 110
poke cmd:research_win
wait 112
X
rm -rf $S/cv-$v; mkdir -p $S/cv-$v/data; : > $S/cv-$v/data/qa.jsonl
TETRISPHERE_QA_CONTROLLERS=$S/cv-$v/data/qa.jsonl BLOCKPLANET_POKE_FILE=$S/cv-$v/poke.txt timeout 150 $S/nav98.sh $S/cv-$v $S/$v.txt > /dev/null 2>&1
echo "$v $(tail -1 $S/cv-$v/stdout.log) $(ls -la $S/cv-$v/rec.mp4 2>/dev/null | awk '{print $5}')"
done
