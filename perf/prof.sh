#!/bin/bash
# perf/prof.sh [levels...]: busy (non-idle) CPU cycles per game-loop iteration
export PATH=$HOME/.devkitsms/bin:$PATH
cd "$(dirname "$0")/.."
lv=${@:-1 3 4 7 8 12 13 14 16}
W=${PERF_DIR:-/tmp/ck1perf}; mkdir -p $W     # scripts + profile dumps land here
for l in $lv; do
  python3 perf/gen.py $l | sed 's/^poke perf_loops 0 w2/profstart\npoke perf_loops 0 w2/' > $W/p$l.txt
  echo -e "profstop\nprofdump $W/pd$l.txt" >> $W/p$l.txt
  out=$(smstest keen.sms $W/p$l.txt 2>&1)
  lp=$(echo "$out" | grep "perf_loops =" | sed 's/.*= \([0-9]*\).*/\1/')
  vb=$(echo "$out" | grep "perf_vbl =" | sed 's/.*= \([0-9]*\).*/\1/')
  python3 - "$l" "$lp" "$vb" $W/pd$l.txt <<'P'
import sys
l,lp,vb,f=sys.argv[1],int(sys.argv[2]),int(sys.argv[3]),sys.argv[4]
noi={}
for line in open('keen.noi'):
    p=line.split()
    if len(p)==3 and p[0]=='DEF': noi[p[1]]=int(p[2],16)
w=noi['_SMS_waitForVBlank']; we=min(v for v in noi.values() if v>w)
tot=wait=0
for line in open(f):
    a,c=line.split(); a=int(a,16); c=int(c); tot+=c
    if w<=a<we: wait+=c
print("L%-2s loops %4d/%4d frames  busy/loop %6d" % (l,lp,vb,(tot-wait)//max(lp,1)))
P
done
