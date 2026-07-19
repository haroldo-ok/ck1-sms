#!/usr/bin/env python3
"""Pogo physics test. Verifies the three vertical impulses against the
values calibrated from CloneKeen's jump model: normal jump -1024
(4.0 px/f), pogo bounce -980 (~0.92x jump height), held-jump pogo -1474
(~2.07x jump height -- the original's high pogo jump). vy is sampled
after the frame's gravity tick, so readings are impulse + GRAV (38)."""
import re, types, glob, collections

src = open('tools/smssim.py').read().split(
    "# --------------------------------------------------------------- scenario ---")[0]
g = {'__name__': 'sim', 'sys': types.SimpleNamespace(argv=['x', 'keen.sms'])}
exec(compile(src, 'infra', 'exec'), g)
run_frame, pad, m = g['run_frame'], g['pad'], g['m']
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
def lstsym(name):
    for ln in open('build/main.lst'):
        if re.search(r'\s_%s:\s*$' % name, ln):
            mm = re.match(r'\s*([0-9A-F]{6})\s', ln)
            if mm: return int(mm.group(1), 16) + 0xC000
A_GS, A_LV, A_PX, A_PY = sym('game_state'), sym('cur_level'), sym('px'), sym('py')
A_POGO, A_VY = lstsym('inv_pogo'), lstsym('vy')
GRAV = 38
BTN = {'U':1,'D':2,'L':4,'R':8,'1':16,'2':32}
def frames(n, *bt):
    v = 0xFF
    for b in bt: v &= ~BTN[b]
    pad[0] = v
    for _ in range(n): run_frame()
def poke8(a, v):
    b = bytes([v])
    m.set_memory_block(0xC000+((a-0xC000)&0x1FFF), b)
    m.set_memory_block(0xE000+((a-0xC000)&0x1FFF), b)
def bank_with(s_):
    for f in glob.glob('gen/bank*.c'):
        t = open(f).read()
        if s_ in t: return t
def arrays(txt, name):
    mm = re.search(r'const unsigned char %s\[\d+\] = \{(.*?)\};' % name, txt, re.S)
    return [int(x) for x in re.findall(r'\d+', mm.group(1))]
wt = bank_with('lvl0_map'); W, H = 70, 73
ow_map = arrays(wt, 'lvl0_map'); ow_fl = arrays(wt, 'lvl0_mtflags')
ow_en = arrays(wt, 'lvl0_entries')
l1 = [(ow_en[i], ow_en[i+1]) for i in range(0, len(ow_en)-3, 4) if ow_en[i+2] == 1][0]
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
    path = []; c = goal
    while c: path.append(c); c = prev[c]
    for (mx, my) in path[::-1][1:]:
        tx, ty = mx*16, my*16
        for _ in range(700):
            px, py = rd16(A_PX), rd16(A_PY)
            if abs(px-tx) <= 1 and abs(py-ty) <= 1: break
            b = []
            if px < tx: b.append('R')
            elif px > tx: b.append('L')
            if py < ty: b.append('D')
            elif py > ty: b.append('U')
            frames(1, *b)

frames(150); frames(4, '1'); frames(150)
walk_to(l1); frames(3, '1'); frames(200)
assert rd8(A_GS) == 2 and rd8(A_LV) == 1
frames(60)
def min_vy(nf, *bt):
    mv = 0
    for _ in range(nf):
        frames(1, *bt)
        v = rd16(A_VY)
        if v < mv: mv = v
    return mv
vj = min_vy(20, '1')
frames(90)
poke8(A_POGO, 1)
frames(2, 'D', '1'); frames(4)
vhi = min_vy(200, '1')
frames(10)
vlo = min_vy(160)
print('jump vy=%d, pogo held vy=%d, pogo low vy=%d (post-gravity reads)'
      % (vj, vhi, vlo))
assert vj  == -1024 + GRAV, 'jump impulse wrong'
assert vhi == -1474 + GRAV, 'held pogo impulse wrong'
assert vlo ==  -980 + GRAV, 'pogo bounce impulse wrong'
print('=== pogo physics test: PASS (jump -1024, bounce -980, held -1474) ===')
