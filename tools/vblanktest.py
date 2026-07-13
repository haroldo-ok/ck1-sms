#!/usr/bin/env python3
"""VBlank-budget test. Everything the engine writes to the VDP with raw
ports or UNSAFE_* OUTI bursts (SAT copy, sprite art streaming, scroll
strip blits) must finish inside the hardware VBlank window (~70 NTSC
lines = ~15,960 cycles): past its end the VDP silently drops over-fast
writes, which shows up as sprites snapping to the screen edge and
corrupted sprite frames while scrolling.

Measures, on non-overrun frames only (CPU parked in SMS_waitForVBlank's
spin when the frame interrupt fires), the ticks from the interrupt until
the game logic starts (entering player_update) -- i.e. the whole vblank
section plus a little slack -- while jump-running through level 13 to
force diagonal strip blits + art uploads. Fails if any frame exceeds the
window."""
import re, types, glob, collections

src = open('tools/smssim.py').read().split(
    "# --------------------------------------------------------------- scenario ---")[0]
g = {'__name__': 'sim', 'sys': types.SimpleNamespace(argv=['x', 'keen.sms'])}
exec(compile(src, 'infra', 'exec'), g)
run_frame, pad, m = g['run_frame'], g['pad'], g['m']
status, frame_no, try_irq = g['status'], g['frame_no'], g['try_irq']
TICKS = g['TICKS_PER_FRAME']
VB = TICKS * 70 // 262
MEMOFF = 40
mem = m.get_state_view()
def rd16(a):
    v = mem[MEMOFF+a] | (mem[MEMOFF+a+1] << 8)
    return v - 65536 if v & 0x8000 else v
def rd8(a): return mem[MEMOFF+a]

def sym(n):
    for line in open('keen.map'):
        mm = re.match(r'\s*0000([0-9A-Fa-f]{4})\s+_%s\b' % n, line)
        if mm: return int(mm.group(1), 16)
    raise SystemExit('symbol %s not found' % n)
A_GS, A_LV = sym('game_state'), sym('cur_level')
A_PX, A_PY = sym('px'), sym('py')
WFV = sym('SMS_waitForVBlank')
SPIN_LO, SPIN_HI = WFV + 5, WFV + 13

lstf = []
for ln in open('build/main.lst'):
    mm = re.search(r'^\s*([0-9A-F]{6})\s.*?(_[A-Za-z]\w*)::?\s*$', ln)
    if mm: lstf.append((int(mm.group(1), 16), mm.group(2)[1:]))
d = dict((n, a) for a, n in lstf)
delta = sym('main') - d['main']
PU_LO = d['player_update'] + delta
PU_HI = sorted(a + delta for a, n in lstf if a + delta > PU_LO)[0]

BTN = {'U':1,'D':2,'L':4,'R':8,'1':16,'2':32}
def frames(n, *bt):
    v = 0xFF
    for b in bt: v &= ~BTN[b]
    pad[0] = v
    for _ in range(n): run_frame()

def measured_frame():
    parked = SPIN_LO <= m.pc < SPIN_HI
    status[0] |= 0x80; try_irq(); frame_no[0] += 1
    if not parked:
        m.ticks_to_stop = TICKS; m.run()
        return None
    used = 0; done = None
    while used < TICKS:
        m.ticks_to_stop = 50; m.run(); used += 50
        if done is None and PU_LO <= m.pc < PU_HI:
            done = used; break
    if done is None: done = TICKS + 1
    m.ticks_to_stop = TICKS - used; m.run()
    return done

def bank_with(s_):
    for f in glob.glob('gen/bank*.c'):
        t = open(f).read()
        if s_ in t: return t
def arrays(txt, name):
    mm = re.search(r'const unsigned char %s\[\d+\] = \{(.*?)\};' % name, txt, re.S)
    return [int(x) for x in re.findall(r'\d+', mm.group(1))]
wt = bank_with('lvl0_map'); W, H = 70, 73
ow_map = arrays(wt, 'lvl0_map'); ow_fl = arrays(wt, 'lvl0_mtflags')
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
        for _ in range(600):
            px, py = rd16(A_PX), rd16(A_PY)
            if abs(px-tx) <= 1 and abs(py-ty) <= 1: break
            b = []
            if px < tx: b.append('R')
            elif px > tx: b.append('L')
            if py < ty: b.append('D')
            elif py > ty: b.append('U')
            frames(1, *b)

frames(150); frames(4, '1'); frames(150)
walk_to((28, 6)); frames(3, '1'); frames(20)   # teleporter to L13 region
walk_to((46, 26)); frames(3, '1'); frames(200)
assert rd8(A_GS) == 2 and rd8(A_LV) == 13, 'failed to enter level 13'

costs = []
for i in range(300):
    pad[0] = 0xFF & ~BTN['R']
    if i % 9 < 3:  pad[0] &= ~BTN['1']      # jumps -> vertical strips too
    if i % 7 == 0: pad[0] &= ~BTN['2']
    c = measured_frame()
    if c is not None: costs.append(c)
costs.sort()
over = sum(1 for c in costs if c > VB)
print('vblank section over %d parked frames: med=%d worst=%d, window=%d'
      % (len(costs), costs[len(costs)//2], costs[-1], VB))
assert len(costs) > 80, 'not enough parked frames sampled'
assert over == 0, '%d frames exceed the vblank window!' % over
print('=== vblank budget test: PASS ===')
