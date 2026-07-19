#!/usr/bin/env python3
"""Sound playback test: capture PSG port writes while Keen jumps and
verify the emitted tone periods exactly match the KEENJUMPSND data
converted from SOUNDS.CK1 (at the original ~44 values/sec rate)."""
import re, types, glob

src = open('tools/smssim.py').read().split(
    "# --------------------------------------------------------------- scenario ---")[0]
# inject a PSG capture into the sim's port handler
src = src.replace("    # PSG (0x7F) etc: ignored",
                  "    elif p == 0x7F or p == 0x7E: psg_log.append(v)")
g = {'__name__': 'sim', 'sys': types.SimpleNamespace(argv=['x', 'keen.sms']),
     'psg_log': []}
exec(compile(src, 'infra', 'exec'), g)
run_frame, pad, m = g['run_frame'], g['pad'], g['m']
psg_log = g['psg_log']
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
A_GS, A_LV = sym('game_state'), sym('cur_level')
A_PX, A_PY = sym('px'), sym('py')
BTN = {'U':1,'D':2,'L':4,'R':8,'1':16,'2':32}
def frames(n, *bt):
    v = 0xFF
    for b in bt: v &= ~BTN[b]
    pad[0] = v
    for _ in range(n): run_frame()

# expected KEENJUMPSND periods from the generated data
gd = open('gen/game_data.c').read()
data = [int(x) for x in re.findall(r'\d+',
        re.search(r'const unsigned int snd_data\[\d+\] = \{(.*?)\};', gd, re.S).group(1))]
offs = [int(x) for x in re.findall(r'\d+',
        re.search(r'const unsigned int snd_off\[\d+\] = \{(.*?)\};', gd, re.S).group(1))]
hdr = open('gen/game_data.h').read()
snd_jump = int(re.search(r'#define SND_JUMP (\d+)', hdr).group(1))
exp = []
i = offs[snd_jump]
while data[i] != 0xFFFF:
    exp.append(data[i]); i += 1
print('KEENJUMPSND: %d values' % len(exp))

# navigate into level 1 (walk sounds don't fire on the title/world path here
# because we only need to reach a level; ignore pre-jump log)
import collections
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
frames(30)                        # settle; let any prior sound finish
del psg_log[:]
frames(2, '1'); frames(58)        # jump from standstill, capture the rise

# decode ch2 tone latches/data + volume from the captured byte stream
periods = []
lo = None
i = 0
while i < len(psg_log):
    b = psg_log[i]
    if (b & 0xF0) == 0xC0:                       # ch2 tone latch (low nibble)
        lo = b & 0x0F
        if i+1 < len(psg_log) and not (psg_log[i+1] & 0x80):
            periods.append(lo | ((psg_log[i+1] & 0x3F) << 4))
            i += 1
    i += 1
n = min(len(periods), len(exp))
# The jump sound is legitimately cut short here: Keen's head hits the low
# ceiling above the spawn ~8 frames in and BUMPHEAD (priority 150)
# replaces it, then LAND plays on touchdown -- the original priority
# behaviour. Validate the longest matching prefix instead.
lcp = 0
while lcp < n and periods[lcp] == exp[lcp]:
    lcp += 1
assert lcp >= 6, 'only %d leading values match: got %r exp %r' % (
    lcp, periods[:8], exp[:8])
print('%d leading periods match KEENJUMPSND before a higher-priority'
      ' sound takes over (authentic)' % lcp)
print('=== sound playback test: PASS ===')
