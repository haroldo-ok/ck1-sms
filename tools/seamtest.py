#!/usr/bin/env python3
"""Vertical-scroll seam test. Enter level 1, walk/jump around so the camera
scrolls UP repeatedly, and every frame verify the *top* visible name-table
row already holds the correct map tiles for the row the scroll register
points at. A stale top row (the "loading seam") would mismatch here.

The seam is ultimately a raster-timing artifact, but the fix works by
guaranteeing the entering top row is in VRAM *before* the scroll register
reveals it -- exactly the invariant this test checks on settled frames."""
import re, types, glob

src = open('tools/smssim.py').read().split(
    "# --------------------------------------------------------------- scenario ---")[0]
g = {'__name__': 'sim', 'sys': types.SimpleNamespace(argv=['x', 'keen.sms'])}
exec(compile(src, 'infra', 'exec'), g)
run_frame, pad, m = g['run_frame'], g['pad'], g['m']
vram, vreg = g['vram'], g['vreg']
MEMOFF = 40
mem = m.get_state_view()
def rd16(a):
    v = mem[MEMOFF+a] | (mem[MEMOFF+a+1] << 8)
    return v - 65536 if v & 0x8000 else v
def rd8(a): return mem[MEMOFF+a]

def sym(name):
    for line in open('keen.map'):
        mm = re.match(r'\s*0000([0-9A-Fa-f]{4})\s+_%s\b' % name, line)
        if mm: return int(mm.group(1), 16)
    raise SystemExit('symbol %s not found' % name)
A_GS, A_LV = sym('game_state'), sym('cur_level')
A_PX, A_PY = sym('px'), sym('py')

BTN = {'U':1,'D':2,'L':4,'R':8,'1':16,'2':32}
def frames(n, *bt):
    v = 0xFF
    for b in bt: v &= ~BTN[b]
    pad[0] = v
    for _ in range(n): run_frame()

def bank_with(sym_):
    for f in glob.glob('gen/bank*.c'):
        s = open(f).read()
        if sym_ in s: return s
def arrays(txt, name, kind='char'):
    mm = re.search(r'const unsigned %s %s\[\d+\] = \{(.*?)\};' % (kind, name), txt, re.S)
    return [int(x) for x in re.findall(r'\d+', mm.group(1))]

# level 1: 120x21, bank5
b5 = bank_with('lvl1_map[')
l1_map   = arrays(b5, 'lvl1_map')
l1_mtdef = arrays(b5, 'lvl1_mtdef', 'int')
W1, H1 = 120, 21
PNT = (vreg[2] & 0x0E) << 10  # updated after first frame; recomputed in check

def expected_tile(wc, wr):
    """world subtile (col wc, row wr) -> NT word the engine should write"""
    m_ = l1_map[(wr >> 1) * W1 + (wc >> 1)]
    return l1_mtdef[(m_ << 2) + ((wr & 1) << 1) + (wc & 1)]

def nt_word(ntc, ntr):
    base = ((vreg[2] & 0x0E) << 10) + ntr * 64 + ntc * 2
    return vram[base] | (vram[base+1] << 8)

checked = [0]
def check_top_rows():
    """the top 2 visible pixel-rows' worth of NT cells must match the map"""
    px, py = rd16(A_PX), rd16(A_PY)
    cam_x = max(0, min(px + 8 - 124, W1*16 - 256))
    cam_y = max(0, min(py + 12 - 92, H1*16 - 192))
    # scroll registers must agree with the derived (settled) camera
    if vreg[8] != ((0 - cam_x) & 0xFF): return
    if vreg[9] != (cam_y % 224): return
    c0 = (cam_x + 8) >> 3
    c1 = (cam_x + 255) >> 3
    # check the top three subtile rows of the visible window
    for wr in range((cam_y >> 3), (cam_y >> 3) + 3):
        ntr = wr % 28
        for wc in range(c0, c1 + 1):
            if wc >= W1*2: break
            exp = expected_tile(wc, wr)
            got = nt_word(wc & 31, ntr)
            assert got == exp, (
                'SEAM at top row wr=%d wc=%d: NT has %d, map wants %d '
                '(cam_y=%d scrollY=%d)' % (wr, wc, got, exp, cam_y, vreg[9]))
            checked[0] += 1

# boot -> world -> walk to level 1 city and enter
import collections
wt = bank_with('lvl0_map'); WW, HH = 70, 73
ow_map = arrays(wt, 'lvl0_map'); ow_fl = arrays(wt, 'lvl0_mtflags')
ow_en = arrays(wt, 'lvl0_entries')
l1cell = [(ow_en[i], ow_en[i+1]) for i in range(0, len(ow_en)-3, 4) if ow_en[i+2] == 1][0]
def walk_to(goal):
    solid = lambda mx, my: ow_fl[ow_map[my*WW+mx]] & 1
    start = (rd16(A_PX) >> 4, rd16(A_PY) >> 4)
    q = collections.deque([start]); prev = {start: None}
    while q:
        c = q.popleft()
        if c == goal: break
        for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
            n = (c[0]+dx, c[1]+dy)
            if 0 <= n[0] < WW and 0 <= n[1] < HH and n not in prev and not solid(*n):
                prev[n] = c; q.append(n)
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
walk_to(l1cell); frames(3, '1'); frames(200)
assert rd8(A_GS) == 2 and rd8(A_LV) == 1, 'failed to enter level 1 (state=%d lv=%d cell=%s)' % (rd8(A_GS), rd8(A_LV), (rd16(A_PX)>>4, rd16(A_PY)>>4))

# Drive lots of upward scrolling: descend, then jump/climb up repeatedly.
# Spawn is low in the level (py=240*? ~ mid), so move right into terrain and
# jump a lot to force the camera up, checking the top rows every frame.
ups = 0
prev_cam_y = None
for step in range(1200):
    # bias toward jumping (up) and roaming right/left
    bt = ['R'] if (step // 60) % 2 == 0 else ['L']
    if step % 5 == 0: bt.append('1')      # jump -> camera rises
    frames(1, *bt)
    py = rd16(A_PY)
    cam_y = max(0, min(py + 12 - 92, H1*16 - 192))
    if prev_cam_y is not None and cam_y < prev_cam_y:
        ups += 1
        check_top_rows()
    else:
        check_top_rows()
    prev_cam_y = cam_y
    if rd8(A_GS) != 2:                     # fell in a pit / exited; restart level
        frames(60)
        if rd8(A_GS) == 1:
            walk_to(l1cell); frames(3, '1'); frames(200)
            prev_cam_y = None

print('upward-scroll frames observed:', ups)
print('top-row cell comparisons:', checked[0])
assert checked[0] > 2000, 'not enough coverage'
print('=== seam test: PASS (no stale top row on any settled frame) ===')
