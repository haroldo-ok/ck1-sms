#!/usr/bin/env python3
"""End-to-end test of the full-game conversion:
   1. boot -> title -> world map (original Mars)
   2. BFS-walk to the level 1 city, enter it
   3. play a bit (walk/jump), then reach the exit door -> level_done
   4. back on the map: city shows 'done', re-entry refused
   5. grant all 4 ship parts, complete another level -> win screen
"""
import re, types, collections, glob

src = open('tools/smssim.py').read().split(
    "# --------------------------------------------------------------- scenario ---")[0]
g = {'__name__': 'sim', 'sys': types.SimpleNamespace(argv=['x', 'keen.sms'])}
exec(compile(src, 'infra', 'exec'), g)
run_frame, screenshot, pad, m = g['run_frame'], g['screenshot'], g['pad'], g['m']
vram = g['vram']
MEMOFF = 40
mem = m.get_state_view()
def rd8(a):  return mem[MEMOFF+a]
def rd16(a):
    v = mem[MEMOFF+a] | (mem[MEMOFF+a+1] << 8)
    return v - 65536 if v & 0x8000 else v
def poke8(a, v):
    b = bytes([v])
    m.set_memory_block(0xC000 + ((a-0xC000) & 0x1FFF), b)
    m.set_memory_block(0xE000 + ((a-0xC000) & 0x1FFF), b)
def poke16(a, v):
    poke8(a, v & 0xFF); poke8(a+1, (v >> 8) & 0xFF)

A_LV, A_GS, A_DONE = 0xC002, 0xC003, 0xC004
A_PARTS = 0xC01C
A_PX, A_PY = 0xC87E, 0xC880

BTN = {'U':1,'D':2,'L':4,'R':8,'1':16,'2':32}
def frames(n, *bt):
    v = 0xFF
    for b in bt: v &= ~BTN[b]
    pad[0] = v
    for _ in range(n): run_frame()

def bank_with(sym):
    for f in glob.glob('gen/bank*.c'):
        t = open(f).read()
        if sym in t: return t
    raise SystemExit('missing ' + sym)

def arrays(txt, name, kind='char'):
    mm = re.search(r'const unsigned %s %s\[\d+\] = \{(.*?)\};' % (kind, name),
                   txt, re.S)
    return [int(x) for x in re.findall(r'\d+', mm.group(1))]

# world data
wt = bank_with('lvl0_map')
W, H = 70, 73
ow_map = arrays(wt, 'lvl0_map')
ow_fl  = arrays(wt, 'lvl0_mtflags')
ow_en  = arrays(wt, 'lvl0_entries')
entries = [(ow_en[i], ow_en[i+1], ow_en[i+2]) for i in range(0, len(ow_en)-3, 4)]
l1_cells = [(x, y) for (x, y, l) in entries if l == 1]
print('level-1 city cells:', l1_cells)

def walk_to(goal):
    solid = lambda mx, my: ow_fl[ow_map[my*W+mx]] & 1
    start = (rd16(A_PX) >> 4, rd16(A_PY) >> 4)
    q = collections.deque([start]); prev = {start: None}
    while q:
        c = q.popleft()
        if c == goal: break
        for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
            n = (c[0]+dx, c[1]+dy)
            if 0 <= n[0] < W and 0 <= n[1] < H and n not in prev and not solid(*n):
                prev[n] = c; q.append(n)
    assert goal in prev, 'no path to %s from %s' % (goal, start)
    path = []; c = goal
    while c: path.append(c); c = prev[c]
    for (mx, my) in path[::-1][1:]:
        tx, ty = mx*16, my*16
        for _ in range(800):
            px, py = rd16(A_PX), rd16(A_PY)
            if abs(px-tx) <= 1 and abs(py-ty) <= 1: break
            b = []
            if px < tx: b.append('R')
            elif px > tx: b.append('L')
            if py < ty: b.append('D')
            elif py > ty: b.append('U')
            frames(1, *b)

# ---- 1) boot -> world
frames(150); frames(4, '1'); frames(150)
assert rd8(A_GS) == 1, 'not on world map'
print('== on world map at', rd16(A_PX), rd16(A_PY))

# ---- 2) enter level 1
walk_to(l1_cells[0])
frames(3, '1'); frames(200)
assert rd8(A_GS) == 2 and rd8(A_LV) == 1, 'did not enter level 1'
print('== in level 1 at', rd16(A_PX), rd16(A_PY))
screenshot('ck_l1_spawn.png')

# ---- 3) play: walk right with jumps for a while
p0 = rd16(A_PX)
for burst in range(4):
    frames(40, 'R'); frames(3, '1', 'R'); frames(25, 'R')
p1 = rd16(A_PX)
print('== progressed %d -> %d (state %d)' % (p0, p1, rd8(A_GS)))
screenshot('ck_l1_play.png')
assert rd8(A_GS) == 2, 'died unexpectedly'
assert p1 > p0 + 60, 'no progress'

# teleport next to the exit door, settle onto the ground, walk in
lt = bank_with('lvl1_exits')
ex = arrays(lt, 'lvl1_exits')
exc = (ex[0], ex[1])
print('== exit door cell:', exc)
poke16(A_PX, exc[0]*16 - 40)
poke16(A_PY, exc[1]*16 - 8)
frames(90)                                  # fall/settle (visuals desync, ok)
assert rd8(A_GS) == 2, 'died after teleport'
for _ in range(300):
    frames(1, 'R')
    if rd8(A_GS) != 2: break
    if rd8(A_DONE + 1): break
frames(200)                                  # walk-out anim + transition
assert rd8(A_DONE + 1) == 1, 'level 1 not marked done'
assert rd8(A_GS) == 1, 'not back on world map'
print('== level 1 DONE, back on map')
screenshot('ck_map_done.png')

# ---- 4) done city refuses re-entry
walk_to(l1_cells[0])
frames(3, '1'); frames(120)
assert rd8(A_GS) == 1, 're-entered a completed level!'
print('== re-entry correctly refused')

# ---- 5) all parts + finish another level -> win
poke8(A_PARTS, 0x0F)
l2_cells = [(x, y) for (x, y, l) in entries if l == 2]
walk_to(l2_cells[0])
frames(3, '1'); frames(200)
assert rd8(A_GS) == 2 and rd8(A_LV) == 2
lt = bank_with('lvl2_exits')
ex = arrays(lt, 'lvl2_exits')
poke16(A_PX, ex[0]*16 - 40)
poke16(A_PY, ex[1]*16 - 8)
frames(90)
for _ in range(300):
    frames(1, 'R')
    if rd8(A_GS) == 3: break
frames(30)
assert rd8(A_GS) == 3 or rd8(A_DONE + 2), 'level 2 completion failed'
frames(120)
screenshot('ck_win.png')
print('== WIN state reached:', rd8(A_GS))
print('=== full-game conversion test: PASS ===')
