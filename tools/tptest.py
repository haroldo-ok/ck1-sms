#!/usr/bin/env python3
"""World-map teleporter test: walk Keen onto the Mars teleporter pad, press a
button, assert he warps to the paired pad, then warp back. Verifies both
directions of the bidirectional 38<->41 pair converted from the CK1 data."""
import re, types, collections, glob

src = open('tools/smssim.py').read().split(
    "# --------------------------------------------------------------- scenario ---")[0]
g = {'__name__': 'sim', 'sys': types.SimpleNamespace(argv=['x', 'keen.sms'])}
exec(compile(src, 'infra', 'exec'), g)
run_frame, screenshot, pad, m = g['run_frame'], g['screenshot'], g['pad'], g['m']
MEMOFF = 40
mem = m.get_state_view()
def rd16(a):
    v = mem[MEMOFF+a] | (mem[MEMOFF+a+1] << 8)
    return v - 65536 if v & 0x8000 else v
def rd8(a): return mem[MEMOFF+a]

# RAM symbols (from keen.map). px/py move between builds; read from map.
def sym(name):
    for line in open('keen.map'):
        mm = re.match(r'\s*0000([0-9A-Fa-f]{4})\s+_%s\b' % name, line)
        if mm: return int(mm.group(1), 16)
    raise SystemExit('symbol %s not found' % name)
A_GS = sym('game_state'); A_PX = sym('px'); A_PY = sym('py')

BTN = {'U':1,'D':2,'L':4,'R':8,'1':16,'2':32}
def frames(n, *bt):
    v = 0xFF
    for b in bt: v &= ~BTN[b]
    pad[0] = v
    for _ in range(n): run_frame()

def bank_with(s):
    for f in glob.glob('gen/bank*.c'):
        t = open(f).read()
        if s in t: return t
def arrays(txt, name):
    mm = re.search(r'const unsigned char %s\[\d+\] = \{(.*?)\};' % name, txt, re.S)
    return [int(x) for x in re.findall(r'\d+', mm.group(1))]

wt = bank_with('lvl0_map'); W, H = 70, 73
ow_map = arrays(wt, 'lvl0_map'); ow_fl = arrays(wt, 'lvl0_mtflags')
tp = arrays(wt, 'lvl0_teleports')
teleports = [(tp[i], tp[i+1], tp[i+2], tp[i+3]) for i in range(0, len(tp), 4)]
assert teleports, 'no teleporters in world data'
print('teleporters:', teleports)

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
    assert goal in prev, 'no path to %s' % (goal,)
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

frames(150); frames(4, '1'); frames(120)
assert rd8(A_GS) == 1, 'not on world map'

s_cell = (teleports[0][0], teleports[0][1])
d_cell = (teleports[0][2], teleports[0][3])

walk_to(s_cell)
assert (rd16(A_PX) >> 4, rd16(A_PY) >> 4) == s_cell
frames(3, '1'); frames(20)
got = (rd16(A_PX) >> 4, rd16(A_PY) >> 4)
assert got == d_cell, 'forward warp: expected %s got %s' % (d_cell, got)
print('forward warp OK: %s -> %s' % (s_cell, d_cell))

frames(15); frames(3, '1'); frames(20)      # dest pad is also a source
got = (rd16(A_PX) >> 4, rd16(A_PY) >> 4)
assert got == s_cell, 'return warp: expected %s got %s' % (s_cell, got)
print('return warp OK: %s -> %s' % (d_cell, s_cell))

# loop still alive after warping
frames(20, 'D')
assert rd8(A_GS) == 1, 'left overworld unexpectedly'
print('=== teleporter test: PASS ===')
