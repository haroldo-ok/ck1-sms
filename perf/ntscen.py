#!/usr/bin/env python3
"""perf/ntscen.py > script: roam levels by teleporting Keen across the map
(collecting items on the way so the override list fills), dumping state
after the camera has scrolled; check the dumps with perf/ntcheck.py."""
import re, glob
items = {}
for fn in glob.glob('gen/bank*.c'):
    for m in re.finditer(r'const unsigned char lvl(\d+)_items\[\d+\] = \{([^}]*)\}', open(fn).read()):
        v = [int(x) for x in m.group(2).replace('\n', '').split(',') if x.strip()]
        items[int(m.group(1))] = [(v[i], v[i+1]) for i in range(0, len(v) - 3, 4)]
L = ["symbols keen.noi", "run 420", "tap 1", "run 200"]
k = 0
for lvl in (1, 7, 13, 12, 3, 14):
    L += ["poke lives 99 w1", "poke cur_level %d w1" % lvl, "poke game_state 2 w1",
          "poke perf_on 2 w1", "run 60"]
    its = items.get(lvl, [])
    step = max(1, len(its) // 10)
    cx, cy = None, None
    for (mx, my) in its[::step][:10]:
        tx, ty = mx * 16 + 2, my * 16 - 8
        if cx is None:       # first stop: spawn there is fine (fresh level)
            cx, cy = tx, ty
        # glide there at <= 3px a frame (6 a game loop at worst), so the camera scrolls strip by strip
        while (cx, cy) != (tx, ty):
            cx += max(-3, min(3, tx - cx)); cy += max(-3, min(3, ty - cy))
            L += ["poke px %d s16" % cx, "poke py %d s16" % cy, "poke vy 0 s16", "run 1"]
        L += ["run 90", "dumpstate /tmp/nt_%03d.bin" % k]
        k += 1
    L += ["poke game_state 1 w1", "run 120"]
print('\n'.join(L))
