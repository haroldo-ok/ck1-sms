#!/usr/bin/env python3
"""perf/ntcheck.py state.bin [...]: verify the scrolled name table against the
level data. For every visible cell (screen cols 1..31, rows 0..23 relative to
the camera) the NT word must equal mtdef[metatile*4 + quadrant], where the
metatile comes from the map or the in-RAM override list (taken items, doors).
state.bin is an smstest `dumpstate` (VRAM 16K, CRAM 32, regs 16, RAM 8K)."""
import re, sys, glob

addr = {}
for line in open('build/main.rst'):
    m = re.match(r'\s+([0-9A-F]{6})\s+\d+\s+_(\w+)::?\s*$', line)
    if m: addr[m.group(2)] = int(m.group(1), 16)

arrays = {}
for fn in glob.glob('gen/bank*.c'):
    txt = open(fn).read()
    for m in re.finditer(r'const unsigned (char|int) (lvl\d+_(?:map|mtdef))\[\d+\] = \{([^}]*)\}', txt):
        arrays[m.group(2)] = [int(x) for x in m.group(3).replace('\n', '').split(',') if x.strip()]

def check(path):
    d = open(path, 'rb').read()
    vram, ram = d[:0x4000], d[0x4000 + 32 + 16:]
    r8 = lambda a: ram[(a - 0xC000) & 0x1FFF]
    r16 = lambda a: r8(a) | (r8(a + 1) << 8)
    lv = r8(addr['cur_level'])
    W, H = r16(addr['mapW']), r16(addr['mapH'])
    c8, rr8 = r16(addr['cam_c8']), r16(addr['cam_r8'])
    n_ovr = r8(addr['n_ovr'])
    ovr = {}
    for i in range(n_ovr - 1, -1, -1):
        ovr[(r8(addr['ovr_mx'] + i), r8(addr['ovr_my'] + i))] = r8(addr['ovr_mt'] + i)
    mp, mt = arrays['lvl%d_map' % lv], arrays['lvl%d_mtdef' % lv]
    bad = 0
    for r in range(0, 24):
        wr = rr8 + r
        if wr >= 2 * H: continue
        for c in range(1, 32):
            wc = c8 + c
            if wc >= 2 * W: continue
            m = ovr.get((wc >> 1, wr >> 1), mp[(wr >> 1) * W + (wc >> 1)])
            exp = mt[m * 4 + (wr & 1) * 2 + (wc & 1)]
            a = 0x3800 + (((wr % 28) * 32 + (wc & 31)) << 1)
            got = vram[a] | (vram[a + 1] << 8)
            if got != exp:
                if bad < 5: print('  %s: cell wc=%d wr=%d got %04X want %04X' % (path, wc, wr, got, exp))
                bad += 1
    print('%s: level %d cam %d,%d ovr %d -> %s' % (path, lv, c8, rr8, n_ovr, 'OK' if not bad else '%d BAD' % bad))
    return bad

sys.exit(1 if sum(check(p) for p in sys.argv[1:]) else 0)
