#!/usr/bin/env python3
"""Jump-reliability test: enter level 1, then perform N short (2-frame)
B1 taps while walking right, and count how many produce a real jump
(py rises noticeably above its grounded value)."""
import re, types, collections

src = open('tools/smssim.py').read().split(
    "# --------------------------------------------------------------- scenario ---")[0]
g = {'__name__': 'sim', 'sys': types.SimpleNamespace(argv=['x', 'keen.sms'])}
exec(compile(src, 'infra', 'exec'), g)
run_frame, m, pad = g['run_frame'], g['m'], g['pad']

MEMOFF = 40
mem = m.get_state_view()
def rd8(a):  return mem[MEMOFF + a]
def rd16(a):
    v = mem[MEMOFF + a] | (mem[MEMOFF + a + 1] << 8)
    return v - 65536 if v & 0x8000 else v
A_GS, A_LV, A_PX, A_PY = 0xC003, 0xC002, 0xC254, 0xC256

BTN_UP, BTN_DN, BTN_L, BTN_R, BTN_1, BTN_2 = 1, 2, 4, 8, 16, 32
def frames(n, *btns):
    v = 0xFF
    for b in btns: v &= ~b
    pad[0] = v
    for _ in range(n): run_frame()

def bfs_to(goal):
    s = open('gen/bank8.c').read()
    mp = [int(x) for x in re.findall(r'\d+', re.search(r'lvl0_map\[\d+\] = \{(.*?)\};', s, re.S).group(1))]
    fl = [int(x) for x in re.findall(r'\d+', re.search(r'lvl0_mtflags\[\d+\] = \{(.*?)\};', s, re.S).group(1))]
    W, H = 71, 69
    start = (rd16(A_PX) >> 4, rd16(A_PY) >> 4)
    solid = lambda mx, my: fl[mp[my*W+mx]] & 1
    q = collections.deque([start]); prev = {start: None}
    while q:
        c = q.popleft()
        if c == goal: break
        for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
            n = (c[0]+dx, c[1]+dy)
            if 0 <= n[0] < W and 0 <= n[1] < H and n not in prev and not solid(*n):
                prev[n] = c; q.append(n)
    path = []; c = goal
    while c: path.append(c); c = prev[c]
    for (mx, my) in path[::-1][1:]:
        tx, ty = mx*16, my*16
        for _ in range(600):
            px, py = rd16(A_PX), rd16(A_PY)
            if abs(px-tx) <= 1 and abs(py-ty) <= 1: break
            b = []
            if px < tx: b.append(BTN_R)
            elif px > tx: b.append(BTN_L)
            if py < ty: b.append(BTN_DN)
            elif py > ty: b.append(BTN_UP)
            frames(1, *b)

# boot -> title -> overworld -> level 1
frames(150)
frames(4, BTN_1)
frames(120)
assert rd8(A_GS) == 1
bfs_to((304 >> 4, 592 >> 4))
frames(3, BTN_1)
frames(180)
assert rd8(A_GS) == 2 and rd8(A_LV) == 1, 'not in level 1'

# settle on the ground
frames(60)
ground_py = rd16(A_PY)
print('grounded py =', ground_py)

TAPS = 20
jumps = 0
for t in range(TAPS):
    # walk a bit (forces scroll-crossing frames), then 2-frame tap
    frames(18, BTN_R if (t & 1) else BTN_L)
    frames(2, BTN_1, BTN_R if (t & 1) else BTN_L)
    # observe following 45 frames for liftoff
    lifted = False
    min_py = 9999
    for _ in range(45):
        frames(1, BTN_R if (t & 1) else BTN_L)
        py = rd16(A_PY)
        min_py = min(min_py, py)
        if py <= ground_py - 12:
            lifted = True
    # wait until back on ground
    for _ in range(90):
        frames(1)
        if rd16(A_PY) >= ground_py - 1: break
    ground_py = rd16(A_PY)
    jumps += lifted
    print('tap %2d: %s (min_py %d, ground %d)' % (t+1, 'JUMP' if lifted else 'missed', min_py, ground_py))

print('=== %d / %d short taps registered ===' % (jumps, TAPS))
assert rd8(A_GS) == 2, 'player died during test'
