#!/usr/bin/env python3
"""Overworld gate test:
   1. the gate east of city 1 blocks while level 1 is unbeaten
   2. after level 1 is done the gate opens
   3. Keen can then reach and enter level 2 (previously unreachable)"""
import re, types, collections

src = open('tools/smssim.py').read().split(
    "# --------------------------------------------------------------- scenario ---")[0]
g = {'__name__': 'sim', 'sys': types.SimpleNamespace(argv=['x', 'keen.sms'])}
exec(compile(src, 'infra', 'exec'), g)
m, pad = g['m'], g['pad']
run_frame, screenshot = g['run_frame'], g['screenshot']
MEMOFF = 40
mem = m.get_state_view()
def rd8(a):  return mem[MEMOFF+a]
def rd16(a):
    v = mem[MEMOFF+a] | (mem[MEMOFF+a+1] << 8)
    return v - 65536 if v & 0x8000 else v
def poke8(a, v):
    b = bytes([v])
    m.set_memory_block(0xC000 + ((a - 0xC000) & 0x1FFF), b)
    m.set_memory_block(0xE000 + ((a - 0xC000) & 0x1FFF), b)
A_GS, A_LV, A_PX, A_PY, A_DONE = 0xC003, 0xC002, 0xC254, 0xC256, 0xC004

BTN = {'U':1,'D':2,'L':4,'R':8,'1':16,'2':32}
def frames(n, *bt):
    v = 0xFF
    for b in bt: v &= ~BTN[b]
    pad[0] = v
    for _ in range(n): run_frame()

s = open('gen/bank8.c').read()
ow_map = [int(x) for x in re.findall(r'\d+', re.search(r'lvl0_map\[\d+\] = \{(.*?)\};', s, re.S).group(1))]
ow_fl  = [int(x) for x in re.findall(r'\d+', re.search(r'lvl0_mtflags\[\d+\] = \{(.*?)\};', s, re.S).group(1))]
W, H = 71, 69
GATES = {(20,37), (20,38)}

def walk_to(goal, gate_open):
    def solid(mx, my):
        if gate_open and (mx, my) in GATES: return False
        return ow_fl[ow_map[my*W+mx]] & 1
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
        for _ in range(600):
            px, py = rd16(A_PX), rd16(A_PY)
            if abs(px-tx) <= 1 and abs(py-ty) <= 1: break
            b = []
            if px < tx: b.append('R')
            elif px > tx: b.append('L')
            if py < ty: b.append('D')
            elif py > ty: b.append('U')
            frames(1, *b)

# boot -> overworld
frames(150); frames(4, '1'); frames(120)
assert rd8(A_GS) == 1

print('== 1) gate blocks while level 1 unbeaten ==')
walk_to((19, 38), gate_open=False)        # stand just west of the gate
frames(90, 'R')                           # shove right for 1.5 s
px = rd16(A_PX)
print('   pushed against gate: px =', px)
assert px <= 308, 'gate did not block! px=%d' % px

print('== 2) beat level 1 -> gate opens ==')
poke8(A_DONE + 1, 1)                      # level_done[1] = 1
frames(90, 'R')
px = rd16(A_PX)
print('   after completion:    px =', px)
assert px >= 336, 'gate did not open! px=%d' % px

print('== 3) reach and enter level 2 ==')
walk_to((22, 28), gate_open=True)
frames(3, '1')
frames(220)
gs, lv = rd8(A_GS), rd8(A_LV)
print('   game_state', gs, 'cur_level', lv, 'bank', g['cur_bank2'][0])
assert gs == 2 and lv == 2, 'did not enter level 2'
screenshot('gate_level2.png')

print('=== gate test: PASS ===')
