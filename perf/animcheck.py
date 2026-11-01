#!/usr/bin/env python3
"""perf/animcheck.py state.bin...: every live (animated) subtile in VRAM must
hold the anim_art frame its an[].cur says (skipped while a batch is queued)."""
import re, sys, glob
addr = {}
for line in open('build/main.rst'):
    m = re.match(r'\s+([0-9A-F]{6})\s+\d+\s+_(\w+)::?\s*$', line)
    if m: addr[m.group(2)] = int(m.group(1), 16)
arr = {}
for fn in glob.glob('gen/bank*.c'):
    for m in re.finditer(r'const unsigned char (lvl\d+_anim|anim_art)\[\d+\] = \{([^}]*)\}', open(fn).read()):
        arr[m.group(1)] = bytes(int(x) for x in m.group(2).replace('\n', '').split(',') if x.strip())
bad = 0
for p in sys.argv[1:]:
    d = open(p, 'rb').read(); vram, ram = d[:0x4000], d[0x4000 + 48:]
    r8 = lambda a: ram[(a - 0xC000) & 0x1FFF]
    lv, n, abn = r8(addr['cur_level']), r8(addr['an_n']), r8(addr['ab_n'])
    lst = arr['lvl%d_anim' % lv]
    ok = 0; nb = 0
    for i in range(n):
        tile = lst[i*4] | (lst[i*4+1] << 8); aid = lst[i*4+2]
        cur = r8(addr['an'] + i*6 + 5)
        want = arr['anim_art'][aid*128 + cur*32: aid*128 + cur*32 + 32]
        prev = arr['anim_art'][aid*128 + ((cur-1)&3)*32: aid*128 + ((cur-1)&3)*32 + 32]
        if vram[tile*32: tile*32+32] == want: ok += 1
        elif vram[tile*32: tile*32+32] == prev and abn == 0: ok += 1   # dumped mid-scan, upload pending
        else: nb += 1
    print('%s: level %d anim %d ok %d bad %d (queued %d)' % (p, lv, n, ok, nb, abn))
    if abn == 0: bad += nb
sys.exit(1 if bad else 0)
