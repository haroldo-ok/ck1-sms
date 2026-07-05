#!/usr/bin/env python3
"""Closed-loop end-to-end test for keen.sms:
   boot -> title (with cheat) -> overworld -> BFS-navigate to level 1 entry
   -> enter -> platform/jump/shoot in level 1 -> screenshots + assertions."""
import re, types, collections, sys

src = open('tools/smssim.py').read().split(
    "# --------------------------------------------------------------- scenario ---")[0]
g = {'__name__': 'sim', 'sys': types.SimpleNamespace(argv=['x', 'keen.sms'])}
exec(compile(src, 'infra', 'exec'), g)
run_frame, screenshot, m = g['run_frame'], g['screenshot'], g['m']
pad, vreg = g['pad'], g['vreg']
cur_bank2 = g['cur_bank2']

ADDR_CUR_LEVEL, ADDR_GAME_STATE, ADDR_PX, ADDR_PY = 0xC002, 0xC003, 0xC254, 0xC256
MEMOFF = 40                        # memory block offset inside get_state_view()
mem = m.get_state_view()

def rd8(a):  return mem[MEMOFF + a]
def rd16(a):
    v = mem[MEMOFF + a] | (mem[MEMOFF + a + 1] << 8)
    return v - 65536 if v & 0x8000 else v

BTN_UP, BTN_DN, BTN_L, BTN_R, BTN_1, BTN_2 = 1, 2, 4, 8, 16, 32
def frames(n, *btns):
    v = 0xFF
    for b in btns: v &= ~b
    pad[0] = v
    for _ in range(n): run_frame()

# ---------------------------------------------------------------- BFS path --
def load_ow_grid():
    s = open('gen/bank8.c').read()
    mp = [int(x) for x in re.findall(r'\d+',
          re.search(r'lvl0_map\[\d+\] = \{(.*?)\};', s, re.S).group(1))]
    fl = [int(x) for x in re.findall(r'\d+',
          re.search(r'lvl0_mtflags\[\d+\] = \{(.*?)\};', s, re.S).group(1))]
    W, H = 71, 69
    return W, H, mp, fl

def bfs(W, H, mp, fl, start, goal):
    solid = lambda mx, my: fl[mp[my*W+mx]] & 1
    q = collections.deque([start]); prev = {start: None}
    while q:
        c = q.popleft()
        if c == goal: break
        for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
            n = (c[0]+dx, c[1]+dy)
            if 0 <= n[0] < W and 0 <= n[1] < H and n not in prev and not solid(*n):
                prev[n] = c; q.append(n)
    if goal not in prev: return None
    path = []; c = goal
    while c: path.append(c); c = prev[c]
    return path[::-1]

# ------------------------------------------------------------------- boot ---
print('== boot & title ==')
frames(150)
assert rd8(ADDR_GAME_STATE) == 0, 'expected STATE_TITLE'
screenshot('t01_title.png')

print('== start with cheat (hold B2, press B1) ==')
frames(2, BTN_2)
frames(4, BTN_2, BTN_1)
frames(120)
assert rd8(ADDR_GAME_STATE) == 1, 'expected STATE_OW, got %d' % rd8(ADDR_GAME_STATE)
px, py = rd16(ADDR_PX), rd16(ADDR_PY)
print('overworld pos', px, py)
screenshot('t02_overworld.png')

print('== BFS to level 1 entry ==')
W, H, mp, fl = load_ow_grid()
start = (px >> 4, py >> 4)
goal  = (304 >> 4, 592 >> 4)         # level-1 entry cell
path = bfs(W, H, mp, fl, start, goal)
assert path, 'no path on overworld!'
print('path len', len(path))

stuck_guard = 0
for (mx, my) in path[1:]:
    tx, ty = mx * 16, my * 16
    for step in range(600):
        px, py = rd16(ADDR_PX), rd16(ADDR_PY)
        if abs(px - tx) <= 1 and abs(py - ty) <= 1: break
        b = []
        if px < tx: b.append(BTN_R)
        elif px > tx: b.append(BTN_L)
        if py < ty: b.append(BTN_DN)
        elif py > ty: b.append(BTN_UP)
        frames(1, *b)
    else:
        stuck_guard += 1
        print('WARN: waypoint (%d,%d) not reached (at %d,%d)' % (mx, my, px, py))
        if stuck_guard > 2: break
px, py = rd16(ADDR_PX), rd16(ADDR_PY)
print('at entry area:', px, py)
screenshot('t03_at_entry.png')

print('== press button to enter level 1 ==')
frames(3, BTN_1)
frames(40)          # ENTER jingle wait (30) + margin
screenshot('t04_interstitial.png')
frames(140)         # interstitial 90 + load
gs, lv = rd8(ADDR_GAME_STATE), rd8(ADDR_CUR_LEVEL)
print('game_state', gs, 'cur_level', lv, 'bank', cur_bank2[0])
assert gs == 2 and lv == 1, 'did not enter level 1'
screenshot('t05_level1_spawn.png')

print('== platforming: walk right, jump, land ==')
frames(60, BTN_R)
p0 = rd16(ADDR_PX)
screenshot('t06_walk_right.png')
frames(3, BTN_1, BTN_R)      # jump while moving
frames(20, BTN_R)
screenshot('t07_midjump.png')
frames(60, BTN_R)
p1 = rd16(ADDR_PX)
print('x progress: %d -> %d' % (p0, p1))
assert p1 > p0 + 40, 'player did not make progress walking right'

print('== fire the raygun (cheat ammo) ==')
frames(2, BTN_2)
frames(6)
screenshot('t08_shoot.png')
frames(30)

print('== keep going right across the level ==')
for burst in range(6):
    frames(50, BTN_R)
    frames(3, BTN_1, BTN_R)
    frames(25, BTN_R)
px, py = rd16(ADDR_PX), rd16(ADDR_PY)
print('deep in level at', px, py, 'state', rd8(ADDR_GAME_STATE))
screenshot('t09_deep_level.png')

print('== all assertions passed ==')
