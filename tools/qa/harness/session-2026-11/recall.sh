#!/bin/bash
S=${BP_HARNESS:-/tmp/bp-harness}; cd /home/user/blockplanet
suffix=$1; shift
declare -A LV=([0]=1 [1]=1 [2]=1 [3]=1 [4]=2 [5]=3)
for spec in "$@"; do k=${spec%:*}; m=${spec#*:}; lv=${LV[$k]}; [ -n "$LVL" ] && lv=$LVL
v=an-$k-$m$suffix
head -10 $S/mm.txt | sed 's/chapter:1:1/chapter:10:5/' > $S/$v.txt; printf 'wait 3\npoke 0x807FFF00:1=%s\npoke cmd:research_magic:%s\nwait 0.3\nrec 16\nwait 17\n' $lv $k >> $S/$v.txt
rm -rf $S/cv-$v; mkdir -p $S/cv-$v/data; : > $S/cv-$v/data/qa.jsonl
e="BLOCKPLANET_POWERUP_TEST=1"; [ $m = ours ] && e="$e BLOCKPLANET_MODELS3D=1"
env $e TETRISPHERE_QA_CONTROLLERS=$S/cv-$v/data/qa.jsonl BLOCKPLANET_POKE_FILE=$S/cv-$v/poke.txt timeout 70 $S/nav98.sh $S/cv-$v $S/$v.txt > /dev/null 2>&1
echo "$v $(ls -la $S/cv-$v/rec.mp4 2>/dev/null | awk '{print $5}') $(tail -1 $S/cv-$v/stdout.log)"; done
