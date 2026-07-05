#!/usr/bin/env python3
"""Validate the optimized strip writers on 'hardware': roam level 1 in the
headless emulator and, every few frames, verify each visible name-table
cell equals mtdef[map] for the current camera (recomputed from px/py)."""
import re, types, collections

src = open('tools/smssim.py').read().split(
    "# --------------------------------------------------------------- scenario ---")[0]
g = {'__name__': 'sim', 'sys': types.SimpleNamespace(argv=['x', 'keen.sms'])}
exec(compile(src, 'infra', 'exec'), g)
m, pad, vram = g['m'], g['pad'], g['vram']
vreg, status = g['vreg'], g['status']
MEMOFF = 40
mem = m.get_state_view()
def rd8(a):  return mem[MEMOFF+a]
def rd16(a):
    v = mem[MEMOFF+a] | (mem[MEMOFF+a+1] << 8)
    return v - 65536 if v & 0x8000 else v
A_GS, A_LV, A_PX, A_PY = 0xC003, 0xC002, 0xC254, 0xC256

BTN = {'U':1,'D':2,'L':4,'R':8,'1':16,'2':32}
def frames(n, *bt):
    v = 0xFF
    for b in bt: v &= ~BTN[b]
    pad[0] = v
    for _ in range(n): run()
def run():
    g['run_frame']()

def arrays(path, *names):
    s = open(path).read()
    out = []
    for n in names:
        mm = re.search(n + r'\[\d+\] = \{(.*?)\};', s, re.S)
        out.append([int(x) for x in re.findall(r'\d+', mm.group(1))])
    return out

# level 1 data (bank4) and overworld path data (bank8)
l1_map, l1_mtdef, l1_fl, l1_items = arrays('gen/bank4.c', 'lvl1_map', 'lvl1_mtdef', 'lvl1_mtflags', 'lvl1_items')
item_cells = {}
for i in range(0, len(l1_items), 4):
    item_cells[(l1_items[i], l1_items[i+1])] = l1_items[i+3]   # (mx,my)->restore mt
ow_map, ow_fl = arrays('gen/bank8.c', 'lvl0_map', 'lvl0_mtflags')
W1, H1 = 116, 17

checked = [0]
def check_nt():
    """visible NT cells == expected world tiles for camera derived from px/py"""
    px, py = rd16(A_PX), rd16(A_PY)
    cam_x = max(0, min(px + 8 - 124, W1*16 - 256))
    cam_y = max(0, min(py + 12 - 92, H1*16 - 192))
    c0 = (cam_x + 8) >> 3
    c1 = (cam_x + 255) >> 3
    r0 = cam_y >> 3
    r1 = (cam_y + 191) >> 3
    # settled => scroll registers MUST agree with the derived camera
    assert vreg[8] == ((0 - cam_x) & 0xFF), ('scrollX wrong', vreg[8], cam_x)
    assert vreg[9] == (cam_y % 224), ('scrollY wrong', vreg[9], cam_y % 224)
    for wr in range(r0, r1 + 1):
        for wc in range(c0, c1 + 1):
            e = 0x3800 + (wr % 28) * 64 + (wc & 31) * 2
            got = (vram[e] | (vram[e+1] << 8)) & 0x1FF
            cell = (wc >> 1, wr >> 1)
            mt = l1_map[cell[1] * W1 + cell[0]]
            sub = (wr & 1)*2 + (wc & 1)
            ok = {l1_mtdef[mt*4 + sub]}
            if cell in item_cells:                       # collected or not
                ok.add(l1_mtdef[item_cells[cell]*4 + sub])
            assert got in ok, ('NT mismatch', wc, wr, got, ok, cam_x, cam_y)
    checked[0] += 1

# boot -> level 1 (BFS overworld nav)
frames(150); frames(4, '1'); frames(120)
Wo, Ho = 71, 69
start = (rd16(A_PX) >> 4, rd16(A_PY) >> 4); goal = (19, 37)
solid = lambda mx, my: ow_fl[ow_map[my*Wo+mx]] & 1
q = collections.deque([start]); prev = {start: None}
while q:
    c = q.popleft()
    if c == goal: break
    for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
        n = (c[0]+dx, c[1]+dy)
        if 0 <= n[0] < Wo and 0 <= n[1] < Ho and n not in prev and not solid(*n):
            prev[n] = c; q.append(n)
path = []; c = goal
while c: path.append(c); c = prev[c]
for (mx, my) in path[::-1][1:]:
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
frames(3, '1'); frames(180)
assert rd8(A_GS) == 2 and rd8(A_LV) == 1

# roam a bounded corridor (x in [120, 700], validated safe ground):
# both directions, periodic jumps for vertical crossings, settle+check often
def settle_check():
    last = (rd16(A_PX), rd16(A_PY)); still = 0
    for _ in range(240):
        frames(1)
        cur = (rd16(A_PX), rd16(A_PY))
        still = still + 1 if cur == last else 0
        last = cur
        if still >= 3: break
    if still >= 3 and rd8(A_GS) == 2:
        check_nt()

guard = 0
for lap in range(4):
    if rd8(A_GS) != 2: break
    f = 0
    while rd16(A_PX) < 700 and rd8(A_GS) == 2 and f < 1200:
        b = ['R']
        if f % 45 == 0: b.append('1')
        frames(1, *b); f += 1; guard += 1
        if f % 25 == 0: settle_check()
    f = 0
    while rd16(A_PX) > 120 and rd8(A_GS) == 2 and f < 1200:
        b = ['L']
        if f % 45 == 0: b.append('1')
        frames(1, *b); f += 1; guard += 1
        if f % 25 == 0: settle_check()

print('NT validations passed:', checked[0])
print('final state', rd8(A_GS), 'level', rd8(A_LV), 'pos', rd16(A_PX), rd16(A_PY))
assert checked[0] >= 100, 'not enough validation points'
print('=== strip writer NT correctness: PASS ===')
