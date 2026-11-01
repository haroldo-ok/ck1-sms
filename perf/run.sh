#!/bin/bash
# perf/run.sh [rom]: per level, game-loop iterations per in-level frames
# (600 frames of scripted running and jumping; 600 loops = full speed)
export PATH=$HOME/.devkitsms/bin:$PATH
cd "$(dirname "$0")/.."
tl=0; tv=0
for l in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do
  T=$(mktemp)
  python3 perf/gen.py $l $([ $l = 5 ] && echo L) > $T   # L5: start leftward, away from its exit
  out=$(smstest ${1:-keen.sms} $T 2>&1); rm -f $T
  lp=$(echo "$out" | grep "perf_loops =" | sed 's/.*= \([0-9]*\).*/\1/')
  vb=$(echo "$out" | grep "perf_vbl =" | sed 's/.*= \([0-9]*\).*/\1/')
  tl=$((tl+lp)); tv=$((tv+vb))
  printf "L%-2s loops %4s frames %4s  %3s%%\n" $l $lp $vb $((100*lp/(vb>0?vb:1)))
done
echo "TOTAL loops $tl frames $tv  $((1000*tl/tv)) per mille"
