#!/usr/bin/env python3
"""Overworld scroll validation: BFS-walk long horizontal+vertical paths on
the Mars map, settling periodically, then ASSERT (not skip):
  1. scroll registers == camera derived from px/py
  2. every visible NT cell == mtdef[map] for that camera
Catches frozen/incorrect scroll register maintenance and strip bugs."""
import re, types, collections

src = open('tools/smssim.py').read().split(
    "# --------------------------------------------------------------- scenario ---")[0]
g = {'__name__': 'sim', 'sys': types.SimpleNamespace(argv=['x', 'keen.sms'])}
exec(compile(src, 'infra', 'exec'), g)
m, pad, vram = g['m'], g['pad'], g['vram']
vreg = g['vreg']
run_frame = g['run_frame']
MEMOFF = 40
mem = m.get_state_view()
def rd8(a):  return mem[MEMOFF+a]
def rd16(a):
    v = mem[MEMOFF+a] | (mem[MEMOFF+a+1] << 8)
    return v - 65536 if v & 0x8000 else v
A_GS, A_PX, A_PY = 0xC003, 0xC254, 0xC256

BTN = {'U':1,'D':2,'L':4,'R':8,'1':16,'2':32}
def frames(n, *bt):
    v = 0xFF
    for b in bt: v &= ~BTN[b]
    pad[0] = v
    for _ in range(n): run_frame()

def arrays(path, *names):
    s = open(path).read()
    out = []
    for n in names:
        mm = re.search(n + r'\[\d+\] = \{(.*?)\};', s, re.S)
        out.append([int(x) for x in re.findall(r'\d+', mm.group(1))])
    return out

ow_map, ow_fl = arrays('gen/bank8.c', 'lvl0_map', 'lvl0_mtflags')
ow_mtdef, = arrays('gen/bank7.c', 'lvl0_mtdef') if 'lvl0_mtdef' in open('gen/bank7.c').read() \
            else arrays('gen/bank8.c', 'lvl0_mtdef')
W, H = 71, 69
PW, PH = W*16, H*16

checked = [0]
def check(tag):
    px, py = rd16(A_PX), rd16(A_PY)
    cam_x = max(0, min(px + 8 - 124, PW - 256))
    cam_y = max(0, min(py + 8 - 92,  PH - 192))
    # 1) scroll registers must match (settled => no excuse)
    assert vreg[8] == ((0 - cam_x) & 0xFF), \
        ('scrollX wrong', tag, vreg[8], cam_x)
    assert vreg[9] == (cam_y % 224), \
        ('scrollY wrong', tag, vreg[9], cam_y, cam_y % 224)
    # 2) visible NT cells
    c0, c1 = (cam_x + 8) >> 3, (cam_x + 255) >> 3
    r0, r1 = cam_y >> 3, (cam_y + 191) >> 3
    for wr in range(r0, r1 + 1):
        base = 0x3800 + (wr % 28) * 64
        mrow = (wr >> 1) * W
        for wc in range(c0, c1 + 1):
            e = base + (wc & 31) * 2
            got = (vram[e] | (vram[e+1] << 8)) & 0x1FF
            mt = ow_map[mrow + (wc >> 1)]
            want = ow_mtdef[mt*4 + (wr & 1)*2 + (wc & 1)]
            assert got == want, ('NT mismatch', tag, wc, wr, got, want, cam_x, cam_y)
    checked[0] += 1

def settle():
    last = (rd16(A_PX), rd16(A_PY)); still = 0
    for _ in range(60):
        frames(1)
        cur = (rd16(A_PX), rd16(A_PY))
        still = still + 1 if cur == last else 0
        last = cur
        if still >= 3: return True
    return False

def bfs(goal):
    start = (rd16(A_PX) >> 4, rd16(A_PY) >> 4)
    solid = lambda mx, my: ow_fl[ow_map[my*W+mx]] & 1
    q = collections.deque([start]); prev = {start: None}
    while q:
        c = q.popleft()
        if c == goal: break
        for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
            n = (c[0]+dx, c[1]+dy)
            if 0 <= n[0] < W and 0 <= n[1] < H and n not in prev and not solid(*n):
                prev[n] = c; q.append(n)
    assert goal in prev, 'no path to %s' % (goal,)
    path = []; c = goal
    while c: path.append(c); c = prev[c]
    return path[::-1]

def walk_to(goal, tag):
    steps = 0
    for (mx, my) in bfs(goal)[1:]:
        tx, ty = mx*16, my*16
        for _ in range(600):
            px, py = rd16(A_PX), rd16(A_PY)
            if abs(px-tx) <= 1 and abs(py-ty) <= 1: break
            b = []
            if px < tx: b.append('R')
            elif px > tx: b.append('L')
            if py < ty: b.append('D')
            elif py > ty: b.append('U')
            frames(1, *b)
            steps += 1
            if steps % 20 == 0 and settle():
                check(tag)
    if settle(): check(tag)

# boot -> overworld
frames(150); frames(4, '1'); frames(120)
assert rd8(A_GS) == 1, 'not on overworld'
assert settle(); check('spawn')

# two laps across the walkable plateau: north, east, south, home
for lap in range(2):
    walk_to((10, 34), 'north%d' % lap)
    walk_to((19, 37), 'east%d' % lap)
    walk_to((10, 40), 'south%d' % lap)
    walk_to((10, 35), 'home%d' % lap)

print('overworld scroll validations passed:', checked[0])
assert checked[0] >= 15, 'not enough validation points'
print('=== overworld scroll + NT correctness: PASS ===')
