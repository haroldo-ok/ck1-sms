#!/usr/bin/env python3
"""Convert the original Commander Keen 1 data files (CK1 dir + KEEN1.EXE,
user-supplied) into the SMS engine's gen/ banks: all 16 levels, the Mars
world map and the ship-flight starfield, EGA sprites (Keen, enemies, items,
ship), every 8x8 font/icon tile, the front-end screens (title, logos,
high-score, preview, finale), the text pages, strings and PC-speaker
sounds.  Nothing is hand-drawn.

Known simplifications (see README): enemy AI is a simplified port of the
original's behaviour (CG-style physics, not a state-for-state copy); the
ice cannon chunk stuns Keen instead of the frozen-in-ice-cube animation;
the L16 chandelier rope is inert.
"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ck1lib, ck1ui
from PIL import Image, ImageDraw, ImageFont

CK   = os.environ.get('CK1_DIR', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'original'))
GEN  = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'gen')

# ------------------------------------------------------------ shared bits --
EGA = [(0,0,0),(0,0,170),(0,170,0),(0,170,170),(170,0,0),(170,0,170),
       (170,85,0),(170,170,170),(85,85,85),(85,85,255),(85,255,85),
       (85,255,255),(255,85,85),(255,85,255),(255,255,85),(255,255,255)]

def nearest_ega(rgb):
    return min(range(16), key=lambda i: sum((a-b)**2 for a,b in zip(EGA[i], rgb)))

def sms_color(rgb):
    r,g,b = (min(3, (c*4)//256 if c<255 else 3) for c in rgb)
    return r | (g<<2) | (b<<4)

BG_PAL = [sms_color(c) for c in EGA]

def load_rgba(path): return Image.open(path).convert('RGBA')

def img_to_ega(img, transparent=False):
    w,h = img.size; px = img.load()
    return [[(-1 if transparent and px[x,y][3] < 128 else nearest_ega(px[x,y][:3]))
             for x in range(w)] for y in range(h)]

def tile_planar(t8):
    out = bytearray()
    for y in range(8):
        b = [0,0,0,0]
        for x in range(8):
            v = t8[y][x]
            for p in range(4):
                if v & (1<<p): b[p] |= 0x80 >> x
        out += bytes(b)
    return bytes(out)

def cut_tile(grid, x, y):
    return [[grid[y+r][x+c] for c in range(8)] for r in range(8)]

def carr(f, name, data, per=24):
    f.write('const unsigned char %s[%d] = {\n' % (name, len(data)))
    for i in range(0, len(data), per):
        f.write('  ' + ','.join(str(b) for b in data[i:i+per]) + ',\n')
    f.write('};\n')

def warr(f, name, data, per=16):
    f.write('const unsigned int %s[%d] = {\n' % (name, len(data)))
    for i in range(0, len(data), per):
        f.write('  ' + ','.join(str(w) for w in data[i:i+per]) + ',\n')
    f.write('};\n')

def build_sprite_palette(grids):
    counts = {}
    for g in grids:
        for row in g:
            for v in row:
                if v >= 0: counts[v] = counts.get(v,0)+1
    used = sorted(counts)
    remap = {}
    while len(used) > 15:
        victim = min(used, key=lambda e: counts[e])
        used.remove(victim)
        near = min(used, key=lambda e: sum(w*(a-b)**2 for w,a,b in
                   zip((2,4,3), EGA[e], EGA[victim])))
        remap[victim] = near
        counts[near] += counts.pop(victim)
        print('sprite palette: remapping EGA %d -> %d' % (victim, near))
    ega2slot = {e: i+1 for i,e in enumerate(used)}
    for v,n in remap.items(): ega2slot[v] = ega2slot[n]
    cram = [0]*16
    for e in used: cram[ega2slot[e]] = sms_color(EGA[e])
    return ega2slot, cram

def sprite_frame_tiles(grid, x, y, w_t, h_t, ega2slot):
    out = b''
    for ty in range(h_t):
        for tx in range(w_t):
            t8 = [[(0 if grid[y+ty*8+r][x+tx*8+c] < 0
                    else ega2slot[grid[y+ty*8+r][x+tx*8+c]])
                   for c in range(8)] for r in range(8)]
            out += tile_planar(t8)
    return out

# ---------------------------------------------------------------- decode ---
hdr   = ck1lib.load_header(CK+'/EGAHEAD.CK1')
TILES = ck1lib.load_latch_tiles16(CK+'/EGALATCH.CK1', hdr)
SPR   = ck1lib.load_sprites(CK+'/EGASPRIT.CK1', hdr)
EXE   = ck1lib.exe_image(CK+'/KEEN1.EXE')
TI    = ck1lib.exe_tileinfo(EXE)
UI_C  = ck1ui.constants(EXE)
def _attr(t):
    solid = t['bleft'] or t['bright'] or t['bdown']
    pts = {7:100, 8:200, 6:500, 9:1000, 10:5000}.get(t['beh'], 0)
    pick = (6 <= t['beh'] <= 16) or (18 <= t['beh'] <= 21) or t['beh'] in (22, 24)
    return dict(solidl=t['bleft'], solidr=t['bright'], solidceil=t['bdown'],
                solidfall=t['bup'] and not solid, priority=1 if t['beh'] < 0 else 0,
                goodie=pick, pickupable=pick, points=pts,
                lethal=1 if t['beh'] == 1 else 0,
                chgtile=(0x114 if i_ >= 0x131 else 0x8F) if pick else 0,
                isanim=1 if t['anim'] > 1 else 0, animoff=0, animlen=t['anim'])
ATTRS = []
for i_, t_ in enumerate(TI): ATTRS.append(_attr(t_))
# doors: chgtile as for pickups
for i_, t_ in enumerate(TI):
    if 2 <= t_['beh'] <= 5: ATTRS[i_]['chgtile'] = 0x8F
    if t_['beh'] == 22:     # hint tile: statue turns into the "used" tile
        ATTRS[i_]['chgtile'] = 434 if 435 <= i_ <= 438 else 315
        ATTRS[i_]['chgtile'] = (UI_C['one_eyed_tile'] if 435 <= i_ <= 438 else 315)
    if t_['beh'] == 24:
        ATTRS[i_]['chgtile'] = i_


def norm(t):
    """animated tiles behave as their first frame"""
    if t < len(ATTRS) and ATTRS[t]['isanim']:
        t -= ATTRS[t]['animoff']
    return t

# item kinds
K_PTS100,K_PTS200,K_PTS500,K_PTS1000,K_PTS5000 = 0,1,2,3,4
K_AMMO,K_POGO = 5,6
K_CARDY,K_CARDR,K_CARDG,K_CARDB = 7,8,9,10
K_JOYSTICK,K_BATTERY,K_VACUUM,K_FUEL = 11,12,13,14
K_HINT, K_TELE = 15, 16
# kind by the original's tile behaviour code (CPlayerItems::procGoodie)
BEH_KIND = {7:K_PTS100, 8:K_PTS200, 6:K_PTS500, 9:K_PTS1000, 10:K_PTS5000,
            15:K_AMMO, 16:K_POGO, 18:K_CARDY, 19:K_CARDR, 20:K_CARDG, 21:K_CARDB,
            11:K_JOYSTICK, 12:K_BATTERY, 13:K_VACUUM, 14:K_FUEL,
            22:K_HINT, 24:K_TELE}
HINT_LEVEL = {2:0, 6:1, 9:2, 10:3, 11:4, 12:5, 15:6}    # level -> HINTn string
# world-map teleporter pads (object code -> destination cell), straight from
# the original's table at 0x158DF/0x158E9/0x158F3 and CG's pad pairing
def _w(o): return ck1ui.exe_word(EXE, o)
TP_DEST = {38: (_w(0x158E9)//16, _w(0x158ED)//16),    # pad A -> B
           41: (_w(0x158DF)//16, _w(0x158E3)//16),    # pad B -> A
           46: (_w(0x158E9)//16, _w(0x158ED)//16)}    # dark-side pad -> B
TP_FROMLEVEL = (_w(0x158F3)//16, _w(0x158F7)//16)     # in-level teleporter lands here
LVL_SHIP = 20
DOOR_COLOR = {173:0,174:0, 195:1,196:1, 197:2,198:2, 199:3,200:3}
EXIT_TILE  = 159
# per-metatile collision byte: the original's four directional blockers
# (bup = floor, bdown = ceiling, bleft/bright = side walls) plus lethal and
# the two ice grades (slippery 2 = coasting, 3 = full slide)
F_UP, F_CEIL, F_BLKR, F_BLKL, F_DEADLY, F_ICE, F_ICE3 = 1, 2, 4, 8, 16, 32, 64

# ------------------------------------------------------- animated tiles ----
# The original's tile-info "anim" words (CG's CTileLoader): a word n >= 2 at
# tile j starts a group of n consecutive tiles j..j+n-1 that cycle every
# 6 ticks; every placed tile advances in lock-step.  A placed member m shows
# frame (m+step) % n.  Quadrants that never change stay ordinary shared
# subtiles; the others become "live" subtiles that the engine re-uploads
# from ANIM_ART (4 frames x 32 bytes per group-quadrant, n=2 repeated).
ANIM_GRP = {}
_j = 0
while _j < len(TI):
    _v = TI[_j]['anim']
    if _v in (0, 1) or _v < 0: _j += 1; continue
    for _k in range(_v): ANIM_GRP[_j + _k] = (_j, _v)
    _j += _v
ANIM_ART = {}                       # (group base, quadrant) -> id
def _quad(t, sy, sx):
    px = TILES[t]
    return tuple(tuple(px[sy*8+y][sx*8+x] & 15 for x in range(8)) for y in range(8))
def anim_quad_id(base, n, qd):
    key = (base, qd)
    if key not in ANIM_ART: ANIM_ART[key] = (len(ANIM_ART), n)
    return ANIM_ART[key][0]

# ------------------------------------------------------------ level build --
def build(idx):
    """idx 0 = world map (LEVEL80), 1..16 = levels"""
    src = 80 if idx == 0 else (81 if idx == 17 else idx)
    lv = ck1lib.load_level(CK+'/LEVEL%02d.CK1' % src)
    W, H = lv['W'], lv['H']
    tmap = [norm(t) for t in lv['tiles']]

    # ---- collect used tiles (+ chgtiles of items/doors/done-cities)
    used = set(tmap)
    for t in set(tmap):
        a = ATTRS[t]
        if (a['pickupable'] or t in DOOR_COLOR) and a['chgtile']:
            used.add(norm(a['chgtile']))
    entries = []
    teleports = []
    if idx == 0:
        # completed-city art, exactly like CVorticonMapLoader: a lone marker
        # becomes tile 77; four markers forming a 2x2 block become 78 79 / 80 81
        cells = {}
        for i, o in enumerate(lv['objs']):
            o &= 0x7fff
            if 1 <= o <= 16: cells[(i % W, i // W)] = o
        for (cx, cy), o in sorted(cells.items(), key=lambda kv: (kv[0][1], kv[0][0])):
            ct = 77
            if all(cells.get(c) == o for c in ((cx-1, cy-1), (cx, cy-1), (cx-1, cy))):
                ct = 81
            elif cells.get((cx+1, cy)) == o and cells.get((cx, cy+1)) == o and cells.get((cx+1, cy+1)) == o:
                ct = 78
            elif cells.get((cx-1, cy)) == o and cells.get((cx-1, cy+1)) == o and cells.get((cx, cy+1)) == o:
                ct = 79
            elif cells.get((cx, cy-1)) == o and cells.get((cx+1, cy-1)) == o and cells.get((cx+1, cy)) == o:
                ct = 80
            used.add(ct)
            entries.append((cx, cy, o, ct))
        # Mars teleporter pair: object markers 38 <-> 41 (from CloneKeen's
        # ep1 teleport_from/dest tables). Emit each endpoint with its
        # destination cell so stepping on one warps to the other.
        for i, o in enumerate(lv['objs']):
            o &= 0x7fff
            if o in TP_DEST:
                teleports.append((i % W, i // W) + TP_DEST[o])
            if o == LVL_SHIP:
                entries.append((i % W, i // W, 100, tmap[i]))

    mts = sorted(used)
    mtidx = {t: i for i, t in enumerate(mts)}
    assert len(mts) <= 255, 'level %d: %d metatiles' % (idx, len(mts))
    # entries were collected before the metatile set existed; their "done"
    # tile is still a global CK1 index -- remap to the local metatile index
    entries = [(mx, my, l, mtidx[ct]) for (mx, my, l, ct) in entries]

    # ---- subtiles with flip dedup
    subs, scache = [], {}
    def addsub(q):
        for flip, v in ((0, q), (0x200, tuple(r[::-1] for r in q)),
                        (0x400, q[::-1]), (0x600, tuple(r[::-1] for r in q[::-1]))):
            if v in scache:
                return scache[v] | flip
        scache[q] = len(subs); subs.append(q)
        return len(subs) - 1

    mtdef, mtflags, anim = [], [], []
    for t in mts:
        px = TILES[t] if t < len(TILES) else [[0]*16]*16
        a = ATTRS[t]
        pri = 0x1000 if a['priority'] else 0
        for sy in range(2):
            for sx in range(2):
                q = tuple(tuple(px[sy*8+y][sx*8+x] & 15 for x in range(8))
                          for y in range(8))
                if t in ANIM_GRP:
                    b_, n_ = ANIM_GRP[t]
                    if len(set(_quad(b_ + f, sy, sx) for f in range(n_))) > 1:
                        subs.append(q)                  # live subtile
                        assert len(subs) <= 256 or idx == 0
                        anim.append((len(subs) - 1,
                                     anim_quad_id(b_, n_, sy*2 + sx), t - b_))
                        mtdef.append((len(subs) - 1) | pri)
                        continue
                mtdef.append(addsub(q) | pri)
        ti = TI[t] if t < len(TI) else None
        fl = 0
        if ti:
            if ti['bup']:    fl |= F_UP
            if ti['bdown']:  fl |= F_CEIL
            if ti['bleft']:  fl |= F_BLKR        # tile's left face stops rightward motion
            if ti['bright']: fl |= F_BLKL
            if ti['bup'] >= 2: fl |= F_ICE
            if ti['bup'] == 3: fl |= F_ICE3
        if a['lethal']:      fl |= F_DEADLY
        mtflags.append(fl)

    # ---- map bytes
    mapb = bytes(mtidx[t] for t in tmap)

    # ---- items / doors / exits
    items, doors, exits = [], [], []
    for i, t in enumerate(tmap):
        mx, my = i % W, i // W
        a = ATTRS[t]
        if idx and a['pickupable']:
            kind = BEH_KIND[TI[t]['beh']]
            items.append((mx, my, kind, mtidx[norm(a['chgtile'])]))
        if idx and t in DOOR_COLOR:
            doors.append((mx, my, DOOR_COLOR[t], mtidx[norm(a['chgtile'])]))
        if (idx and t == EXIT_TILE):
            exits.append((mx, my))

    # items sorted by column so the engine can scan just a small mx window
    items.sort(key=lambda it: (it[0], it[1]))

    # ---- enemies (codes 1..9); 10=rope and >10 special data are inert
    ents = []
    if idx:
        for i, o in enumerate(lv['objs']):
            if 1 <= o <= 5:
                ents.append((o - 1, i % W, i // W))          # yorp..tank
            elif 6 <= o <= 9:
                ents.append((o - 1, i % W, i // W))          # cannons 5..8
    sx, sy = lv['spawn'] or (2, 2)
    print('L%02d: %3dx%-3d mt=%3d sub=%3d items=%3d doors=%d ents=%2d exits=%d'
          % (src, W, H, len(mts), len(subs), len(items), len(doors),
             len(ents), len(exits)))
    assert len(subs) <= (440 if idx == 0 else 256), 'subtile budget'
    assert len(items) <= 176 and len(ents) <= 30 and len(doors) <= 8
    tiles_blob = b''.join(tile_planar([[v for v in row] for row in q])
                          for q in subs)
    return dict(idx=idx, W=W, H=H, tiles=tiles_blob, mtdef=mtdef,
                mtflags=bytes(mtflags), mapb=mapb, items=items, doors=doors,
                exits=exits, ents=ents, entries=entries, teleports=teleports, anim=anim,
                spawn=(sx*16, sy*16))

LVL_FLY = 17                      # LEVEL81: the starfield the ship flies over
levels = [build(i) for i in range(18)]

# ------------------------------------------------------------ bank packing -
BANK_CAP = 16 * 1024 - 64
banks = {}          # bank no -> list of (emit_fn closure data)
bank_sizes = {}

def alloc(size):
    for b in sorted(bank_sizes):
        if bank_sizes[b] + size <= BANK_CAP:
            bank_sizes[b] += size
            return b
    b = (max(bank_sizes) + 1) if bank_sizes else 4
    bank_sizes[b] = size
    banks[b] = []
    return b

for lv in sorted(levels, key=lambda l: -(len(l['tiles']))):
    i = lv['idx']
    a_size = len(lv['tiles'])
    b_size = (len(lv['mtdef'])*2 + len(lv['mtflags']) + len(lv['mapb']) +
              len(lv['items'])*4 + len(lv['doors'])*4 + len(lv['ents'])*3 +
              len(lv['entries'])*4 + len(lv['exits'])*2 +
              len(lv['teleports'])*4 + len(lv['anim'])*4 + 64)
    if a_size + b_size <= BANK_CAP:
        b = alloc(a_size + b_size)
        lv['bank'] = lv['map_bank'] = b
        banks[b].append((i, 'AB'))
    else:
        ba = alloc(a_size); lv['bank'] = ba; banks[ba].append((i, 'A'))
        bb = alloc(b_size); lv['map_bank'] = bb; banks[bb].append((i, 'B'))

# ---------------------------------------------------------------- sprites --
def spr_grid(i, w=16, h=24):
    px = SPR[i]; sh = len(px); sw = len(px[0])
    g = [[-1]*w for _ in range(h)]
    for y in range(sh):
        for x in range(min(sw, w)):
            g[h-sh+y][x] = px[y][x]          # bottom anchored
    return g
KEEN_FR = [spr_grid(i) for i in range(32)]
OWK_FR  = [spr_grid(i, 16, 16) for i in range(32, 48)]
def top_pad(i, h=16):
    px = SPR[i]; g = [[-1]*16 for _ in range(h)]
    for y in range(len(px)):
        for x in range(16): g[y+ (h-len(px))//2][x] = px[y][x]
    return g
BLT_FR = [top_pad(108), top_pad(110), top_pad(111)]
# yorp straight from EGASPRIT, ordered to match the engine's frame
# semantics (look x4, walk R x2, walk L x2, stun x2, zapped, dead)
YORP_F = [48, 49, 50, 51, 54, 55, 52, 53, 56, 57, 58, 59]

def ega_frame(si, w=16, h=24):
    """EGASPRIT sprite -> w x h grid, center-cropped x, bottom-anchored y"""
    px = SPR[si]
    sh, sw = len(px), len(px[0])
    g = [[-1]*w for _ in range(h)]
    x0 = max(0, (sw - w)//2)
    y0 = max(0, sh - h)
    for y in range(min(h, sh)):
        for x in range(min(w, sw)):
            g[h-1-y][w-1-x] = px[sh-1-y-0][sw-1-x-0] if (sh-1-y >= y0 and sw-1-x >= x0) else -1
    # simpler: rebuild directly
    g = [[-1]*w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            sy, sx2 = y0 + y, x0 + x
            if 0 <= sy < sh and 0 <= sx2 < sw:
                g[y][x] = px[sy][sx2]
    return g

GARG_F   = [60, 64, 65, 66, 67, 68, 69]      # stand, wR x2, wL x2, dying, dead
VORT_F   = [74, 75, 70, 71, 80, 81, 85, 87]  # wR x2, wL x2, jump R/L, dying, dead
BUTLER_F = [88, 89, 92, 93]                  # walkR x2, walkL x2 (16x16)
TANK_F   = [98, 99, 102, 103]                # walkR x2, walkL x2
garg_frames   = [ega_frame(i, 24, 32) for i in GARG_F]
vort_frames   = [ega_frame(i, 24, 32) for i in VORT_F]
butler_frames = [ega_frame(i) for i in BUTLER_F]
tank_frames   = [ega_frame(i) for i in TANK_F]
eray_frame    = ega_frame(109, 16, 8)        # green enemy zap
chunk_frame   = ega_frame(112, 16, 16)       # the flying ice cube
# ending: the ship (right / left, 16x8) and the ? / ! marks (8x16)
ship_frames = [ega_frame(115, 16, 8), ega_frame(116, 16, 8),
               ega_frame(118, 8, 16), ega_frame(117, 8, 16)]

yorp_frames   = [ega_frame(i) for i in YORP_F]
allspr = [g for g in KEEN_FR] + OWK_FR + BLT_FR + yorp_frames + garg_frames + vort_frames + \
         butler_frames + tank_frames + [eray_frame, chunk_frame] + ship_frames
ega2slot, SPR_PAL = build_sprite_palette(allspr)

spr_keen = b''.join(sprite_frame_tiles(g, 0, 0, 2, 3, ega2slot) for g in KEEN_FR)
spr_owk  = b''.join(sprite_frame_tiles(g, 0, 0, 2, 2, ega2slot) for g in OWK_FR)
spr_blt  = b''.join(sprite_frame_tiles(g, 0, 0, 2, 2, ega2slot) for g in BLT_FR)

def frames_blob(frames):
    out = b''
    for g in frames:
        h = len(g)
        out += sprite_frame_tiles(g, 0, 0, len(g[0])//8, h//8, ega2slot)
    return out

spr_yorp   = frames_blob(yorp_frames)        # 12 x 6 tiles
spr_garg   = frames_blob(garg_frames)        # 5 x 6 tiles
spr_vort   = frames_blob(vort_frames)        # 6 x 6
spr_butler = frames_blob([  # pad 16x16 to 16x24 bottom-anchored
    [[-1]*16 for _ in range(8)] + g for g in butler_frames])
spr_tank   = frames_blob(tank_frames)        # 4 x 6
spr_eray   = sprite_frame_tiles(eray_frame, 0, 0, 2, 1, ega2slot)
spr_chunk  = sprite_frame_tiles(chunk_frame, 0, 0, 2, 2, ega2slot)
spr_ship   = (sprite_frame_tiles(ship_frames[0], 0, 0, 2, 1, ega2slot) +
              sprite_frame_tiles(ship_frames[1], 0, 0, 2, 1, ega2slot) +
              sprite_frame_tiles(ship_frames[2], 0, 0, 1, 2, ega2slot) +
              sprite_frame_tiles(ship_frames[3], 0, 0, 1, 2, ega2slot))

# ----------------------------------------------------------------- title ---
BM = ck1lib.load_bitmaps(CK+'/EGALATCH.CK1', hdr)
T8 = ck1lib.load_latch_tiles8(CK+'/EGALATCH.CK1', hdr)
tb = BM['TITLE']                              # 264x112 -> crop 4px per side
tg = [[0]*256 for _ in range(192)]
for y in range(112):
    for x in range(256): tg[8+y][x] = tb[y][x+4]
title_tiles, title_map, tcache = [], [], {}
for ty in range(24):
    for tx in range(32):
        blob = tile_planar(cut_tile(tg, tx*8, ty*8))
        if blob not in tcache:
            tcache[blob] = len(title_tiles); title_tiles.append(blob)
        title_map.append(tcache[blob])
print('title: %d unique tiles' % len(title_tiles))

# font: original 8x8 tiles, ASCII 32..90 (white font = tile index == ASCII)
font_chars = 59
font_1bpp = bytearray()
for i in range(font_chars):
    t = T8[32+i]
    for y in range(8):
        b = 0
        for x in range(8):
            if t[y][x] in (15, 14, 7): b |= 1 << (7-x)
        font_1bpp.append(b)

# ------------------------------------------------------------------ emit ---
os.makedirs(GEN, exist_ok=True)

with open(os.path.join(GEN,'bank2.c'), 'w') as f:
    f.write('/* generated: sprite graphics */\n')
    carr(f, 'spr_keen', spr_keen);   carr(f, 'spr_yorp', spr_yorp)
    carr(f, 'spr_owk', spr_owk);     carr(f, 'spr_blt', spr_blt)
    carr(f, 'spr_butler', spr_butler); carr(f, 'spr_tank', spr_tank)
    carr(f, 'spr_eray', spr_eray);   carr(f, 'spr_chunk', spr_chunk)

with open(os.path.join(GEN,'bank3.c'), 'w') as f:
    f.write('/* generated: spare bank (second sprite sheet) */\n')
    carr(f, 'spr_ship', spr_ship)
    _art = bytearray(len(ANIM_ART) * 128)
    for (b_, qd), (aid, n_) in ANIM_ART.items():
        for fr in range(4):
            _art[aid*128 + fr*32: aid*128 + fr*32 + 32] = tile_planar([list(r) for r in _quad(b_ + fr % n_, qd >> 1, qd & 1)])
    carr(f, 'anim_art', bytes(_art))
    carr(f, 'spr_garg', spr_garg);   carr(f, 'spr_vort', spr_vort)

for b in sorted(banks):
    with open(os.path.join(GEN,'bank%d.c' % b), 'w') as f:
        f.write('/* generated bank %d */\n' % b)
        for (i, part) in banks[b]:
            lv = levels[i]
            p = 'lvl%d' % i
            if 'A' in part:
                carr(f, p+'_tiles', lv['tiles'])
            if 'B' in part:
                warr(f, p+'_mtdef', lv['mtdef'])
                carr(f, p+'_mtflags', lv['mtflags'])
                carr(f, p+'_map', lv['mapb'])
                items = []
                for (mx,my,k,r) in lv['items']: items += [mx,my,k,r]
                carr(f, p+'_items', bytes(items) or b'\0')
                doors = []
                for (mx,my,c,r) in lv['doors']: doors += [mx,my,c,r]
                carr(f, p+'_doors', bytes(doors) or b'\0')
                ents = []
                for (t,mx,my) in lv['ents']: ents += [t,mx,my]
                carr(f, p+'_ents', bytes(ents) or b'\0')
                ee = []
                for (mx,my,l,dm) in lv['entries']: ee += [mx,my,l,dm]
                carr(f, p+'_entries', bytes(ee) or b'\0')
                ex = []
                for (mx,my) in lv['exits']: ex += [mx,my]
                carr(f, p+'_exits', bytes(ex) or b'\0')
                tp = []
                for (sx,sy,dx,dy) in lv['teleports']: tp += [sx,sy,dx,dy]
                carr(f, p+'_teleports', bytes(tp) or b'\0')
                an = []
                for (sb,aid,m) in lv['anim']: an += [sb & 255, sb >> 8, aid, m]
                carr(f, p+'_anim', bytes(an) or b'\0')

# ------------------------------------------------- PC speaker sounds -----
# SOUNDS.CK1: word at 0x06 = sound count; 16-byte directory entries from
# 0x10 (word data-offset, byte priority, pad, 12-char name); sound data =
# 16-bit words: 0 = silence tick, 0xFFFF = end, else a PIT divisor
# (frequency = 1193180 / word).  The SMS PSG clock is exactly 3x the PIT
# clock, so the PSG tone period is simply word * 3 / 32.
SND_SEL = [
    ('KEENWALKSND', 'WALK1'),    ('KEENWLK2SND', 'WALK2'),
    ('WLDWALKSND',  'WLDWALK'),  ('KEENBLOKSND', 'BLOK'),
    ('KEENJUMPSND', 'JUMP'),     ('KEENLANDSND', 'LAND'),
    ('KEENPOGOSND', 'POGO'),     ('POGOJUMPSND', 'POGOJUMP'),
    ('BUMPHEADSND', 'BUMPHEAD'), ('KEENDIESND',  'DIE'),
    ('KEENFIRESND', 'FIRE'),     ('GUNCLICK',    'GUNCLICK'),
    ('GOTBONUSSND', 'BONUS'),    ('GOTITEMSND',  'ITEM'),
    ('GOTPARTSND',  'PART'),     ('GETCARDSND',  'CARD'),
    ('DOOROPENSND', 'DOOR'),     ('LVLDONESND',  'LVLDONE'),
    ('WLDENTERSND', 'WLDENTER'), ('GOINDOORSND', 'GOINDOOR'),
    ('TELEPORTSND', 'TELEPORT'), ('SHOTHIT',     'SHOTHIT'),
    ('YORPBOPSND',  'YORPBOP'),  ('YORPSCREAM',  'YORPSCREAM'),
    ('GARGSCREAM',  'GARGSCREAM'), ('vortscream', 'VORTSCREAM'),
    ('keencicle',   'KEENCICLE'), ('YORPBUMPSND', 'YORPBUMP'),
    ('TANKFIRE',    'TANKFIRE'),  ('CANNONFIRE',  'CANNON'),  ('PLUMMETSND',  'PLUMMET'),
    ('WLDBLOCKSND', 'WLDBLOCK'),  ('GAMEOVERSND', 'GAMEOVER'),
    ('HISCORESND',  'HISCORE'),   ('CHUNKSMASH',  'CHUNKSMASH'),
    ('USEKEYSND',   'USEKEY'),    ('SLAMSND',     'SLAM'),
    ('CLICKSND',    'CLICK'),     ('CRYSTALSND',  'CRYSTAL'),
    ('EXTRAMANSND', 'EXTRAMAN'),  ('keensleft',   'KEENSLEFT'),
]

def load_ck1_sounds(path):
    import struct
    d = open(path, 'rb').read()
    n = struct.unpack_from('<H', d, 6)[0]
    out = {}
    for j in range(n):
        off, pri = struct.unpack_from('<HB', d, 0x10 + j*16)
        name = d[0x10+j*16+4:0x10+j*16+16].split(b'\0')[0].decode('ascii', 'replace')
        words = []
        p = off
        while p + 1 < len(d):
            v = struct.unpack_from('<H', d, p)[0]; p += 2
            if v == 0xFFFF: break
            words.append(v)
        out[name] = (pri, words)
    return out

_snds = load_ck1_sounds(CK + '/SOUNDS.CK1')
snd_data, snd_off, snd_pri = [], [], []
for fname, cname in SND_SEL:
    pri, words = _snds[fname]
    snd_off.append(len(snd_data))
    snd_pri.append(pri)
    for w in words:
        if w == 0:
            snd_data.append(0)
        else:
            p = (w * 3 + 16) >> 5
            if p < 1: p = 1
            if p > 1023: p = 1023
            snd_data.append(p)
    snd_data.append(0xFFFF)
print('sounds: %d selected, %d words (%d bytes)'
      % (len(SND_SEL), len(snd_data), len(snd_data)*2))

BANK_SND = max(banks) + 1
BANK_TXT = BANK_SND + 1

# ---------------------------------------------------- dialogs / strings ----
STR = ck1ui.exe_strings(EXE)
STR_ORDER = ['HINT2','HINT6','HINT9','HINT10','HINT11','HINT12','HINT15',
             'SHIP','LIVES','STATUS','CHEAT',
             'E1','E2','E3','E4','E5','E6','E7','E8','E9','TBC']
txt_blob, txt_off = b'', []
for n in STR_ORDER:
    txt_off.append(len(txt_blob)); txt_blob += STR[n].encode('latin-1') + b'\0'
def t8_blob(lo, hi, base=0):
    return b''.join(tile_planar(T8[base + c]) for c in range(lo, hi))
dlg_f0    = t8_blob(32, 128)              # black on white, ASCII 32..127
dlg_f1    = t8_blob(32, 96, 128)          # status-box font: red on grey, 32..95
dlg_frame = t8_blob(1, 9)                 # silver tube frame pieces 1..8
ICON_TILES = [UI_C['raygun_tile'], UI_C['pogo_tile'], 424, 425, 426, 427,
              321, 322, 323, 324, 448, 449, 450, 451]
def icon16(ti):
    px = TILES[ti]
    return b''.join(tile_planar([[px[sy*8+y][sx*8+x] & 15 for x in range(8)]
                                 for y in range(8)]) for sy in range(2) for sx in range(2))
dlg_icons = b''.join(icon16(t) for t in ICON_TILES)
dlg_keen  = b''.join(tile_planar([[(15 if OWK_FR[4][sy*8+y][sx*8+x] < 0 else OWK_FR[4][sy*8+y][sx*8+x])
                                    for x in range(8)] for y in range(8)])
                     for sy in range(2) for sx in range(2))   # overworld Keen "down" frame on white
with open(os.path.join(GEN,'bank%d.c' % BANK_SND), 'w') as f:
    f.write('/* generated: sound data (original SOUNDS.CK1 -> PSG periods) */\n')
    warr(f, 'snd_data', snd_data)

with open(os.path.join(GEN,'bank%d.c' % BANK_TXT), 'w') as f:
    f.write('/* generated: dialog tiles + original strings from KEEN1.EXE */\n')
    carr(f, 'dlg_f0', dlg_f0); carr(f, 'dlg_f1', dlg_f1)
    carr(f, 'dlg_frame', dlg_frame); carr(f, 'dlg_icons', dlg_icons)
    carr(f, 'dlg_keen', dlg_keen)
    carr(f, 'txt_data', txt_blob); warr(f, 'txt_off', txt_off)

# ----------------------------------------------- front-end data + overlays --
import ck1front
FE_BANKS, FE_HDR, FE_DATA, FE_LIST = ck1front.build(
    CK, hdr, EXE, T8, TILES, BANK_TXT + 1, BANK_CAP, tg)
OVLS = ['front']          # code overlays: src/ovl_<name>.c, one bank each
OVL_BANK = {n: BANK_TXT + 1 + len(FE_LIST) + i for i, n in enumerate(OVLS)}
for b, src in FE_BANKS.items():
    with open(os.path.join(GEN, 'bank%d.c' % b), 'w') as f: f.write(src)

with open(os.path.join(GEN,'game_data.h'), 'w') as f:
    f.write('#ifndef GAME_DATA_H\n#define GAME_DATA_H\n')
    f.write('#define BANK_TXT %d\n' % BANK_TXT)
    for k, n in enumerate(STR_ORDER): f.write('#define TXT_%s %d\n' % (n, k))
    f.write('extern const unsigned char dlg_f0[], dlg_f1[], dlg_frame[], dlg_icons[], dlg_keen[], txt_data[];\n')
    f.write('extern const unsigned int txt_off[];\n')
    f.write('#define BANK_SND %d\n' % BANK_SND)
    f.write('#define TELE_DX %d\n#define TELE_DY %d\n' % TP_FROMLEVEL)
    f.write('#define K_HINT %d\n#define K_TELE %d\n' % (K_HINT, K_TELE))
    f.write('#define F_UP 1\n#define F_CEIL 2\n#define F_BLKR 4\n#define F_BLKL 8\n'
            '#define F_DEADLY 16\n#define F_ICE 32\n#define F_ICE3 64\n')
    f.write('#define BANK_SPRITES 2\n#define BANK_SPR2 3\n#define LVL_FLY %d\n' % LVL_FLY)
    f.write('typedef struct {\n')
    f.write('  const unsigned char *tiles; unsigned int tiles_size;\n')
    f.write('  const unsigned int  *mtdef;\n')
    f.write('  const unsigned char *mtflags;\n')
    f.write('  const unsigned char *map;\n')
    f.write('  const unsigned char *items;   unsigned char nitems;\n')
    f.write('  const unsigned char *doors;   unsigned char ndoors;\n')
    f.write('  const unsigned char *ents;    unsigned char nents;\n')
    f.write('  const unsigned char *entries; unsigned char nentries;\n')
    f.write('  const unsigned char *exits;   unsigned char nexits;\n')
    f.write('  const unsigned char *teleports; unsigned char nteleports;\n')
    f.write('  const unsigned char *anim; unsigned char nanim;\n')
    f.write('  unsigned char bank, map_bank, nmt, W2;\n')
    f.write('  unsigned int W, H;\n')
    f.write('  int spawn_x, spawn_y;\n')
    f.write('} LevelDesc;\n')
    f.write('extern const LevelDesc level_descs[18];\n')
    f.write('extern const unsigned char bg_palette[16], spr_palette[16];\n')
    f.write('extern const unsigned char font_1bpp[%d];\n' % len(font_1bpp))
    for k, (_, cname) in enumerate(SND_SEL):
        f.write('#define SND_%s %d\n' % (cname, k))
    f.write('extern const unsigned int snd_data[];\n')
    f.write('extern const unsigned int snd_off[];\n')
    f.write('extern const unsigned char snd_pri[];\n')
    f.write('extern const unsigned char spr_keen[],spr_yorp[],spr_owk[],'
            'spr_blt[],spr_garg[],spr_vort[],spr_butler[],spr_tank[],'
            'spr_eray[],spr_chunk[],spr_ship[],anim_art[];\n')
    for i in range(18):
        p = 'lvl%d' % i
        f.write('extern const unsigned char %s_tiles[],%s_mtflags[],%s_map[],'
                '%s_items[],%s_doors[],%s_ents[],%s_entries[],%s_exits[],'
                '%s_teleports[],%s_anim[];\n'
                % (p,p,p,p,p,p,p,p,p,p))
        f.write('extern const unsigned int %s_mtdef[];\n' % p)
    f.write(FE_HDR)
    for n in OVLS: f.write('#define BANK_OVL_%s %d\n' % (n.upper(), OVL_BANK[n]))
    f.write('#endif\n')

with open(os.path.join(GEN,'game_data.c'), 'w') as f:
    f.write('#include "game_data.h"\n')
    f.write(FE_DATA)
    f.write('const unsigned int snd_off[%d] = { %s };\n'
            % (len(snd_off), ','.join(str(x) for x in snd_off)))
    f.write('const unsigned char snd_pri[%d] = { %s };\n'
            % (len(snd_pri), ','.join(str(x) for x in snd_pri)))
    carr(f, 'bg_palette', bytes(BG_PAL))
    carr(f, 'spr_palette', bytes(SPR_PAL))
    carr(f, 'font_1bpp', bytes(font_1bpp))
    f.write('const LevelDesc level_descs[18] = {\n')
    for i in range(18):
        lv, p = levels[i], 'lvl%d' % i
        f.write('  { %s_tiles,%d, %s_mtdef,%s_mtflags,%s_map, '
                '%s_items,%d, %s_doors,%d, %s_ents,%d, %s_entries,%d, '
                '%s_exits,%d, %s_teleports,%d, %s_anim,%d, %d,%d,%d,0, %d,%d, %d,%d },\n'
                % (p, len(lv['tiles']), p, p, p,
                   p, len(lv['items']), p, len(lv['doors']),
                   p, len(lv['ents']), p, len(lv['entries']),
                   p, len(lv['exits']),
                   p, len(lv['teleports']), p, len(lv['anim']),
                   lv['bank'], lv['map_bank'], len(lv['mtflags']),
                   lv['W'], lv['H'], lv['spawn'][0], lv['spawn'][1]))
    f.write('};\n')

with open(os.path.join(GEN,'banks.mk'), 'w') as f:
    dbl = [2, 3] + sorted(banks) + [BANK_SND, BANK_TXT] + FE_LIST
    bl = dbl + [OVL_BANK[n] for n in OVLS]
    f.write('BANKS := %s\n' % ' '.join(str(b) for b in bl))
    f.write('OVLS := %s\n' % ' '.join(OVLS))
    for n in OVLS: f.write('OVL_%s := %d\n' % (n, OVL_BANK[n]))
    f.write('BANKRELS := %s\n' % ' '.join(
        ['build/bank%d.rel' % b for b in dbl] + ['build/ovl_%s.rel' % n for n in OVLS]))
    f.write('DATABANKS := %s\n' % ' '.join(str(b) for b in dbl))
    f.write('BANKFLAGS := %s\n' %
            ' '.join('-Wl-b_BANK%d=0x%X' % (b, (b<<16)|0x8000) for b in bl))

print('banks used:', [2,3]+sorted(banks)+[BANK_SND, BANK_TXT],
      'sizes:', {b: bank_sizes[b] for b in sorted(banks)})
print('ROM banks total:', max(OVL_BANK.values())+1)
print('done.')
