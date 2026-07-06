#!/usr/bin/env python3
"""Convert original Commander Keen 1 data (user-supplied CK1 files) into the
SMS engine's gen/ banks: all 16 levels + the Mars world map, EGA enemy
sprites, plus the existing HTML5-derived Keen/yorp art, title and font.

Simplifications (documented in README):
  - background tile animation dropped (first frame used)
  - ice chunks stun Keen instead of the frozen-in-ice-cube animation
  - the L16 chandelier rope is inert; vorticons die to 4 zaps instead
  - the L13 secret-level teleporter and world-map teleporter are inert
  - vorticons (24x32) are center-cropped to 16x24 hardware sprites
"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ck1lib
from PIL import Image, ImageDraw, ImageFont

CK   = '/home/claude/newdata'
HTML = '/home/claude/work/HTML5-Keen-master/data'
GEN  = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'gen')
ATTR = CK + '/clonekeen-master/bin/ep1attr.dat'
if not os.path.exists(ATTR):
    ATTR = '/home/claude/newdata/clonekeen-master/bin/ep1attr.dat'

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
        near = min(used, key=lambda e: sum((a-b)**2 for a,b in zip(EGA[e],EGA[victim])))
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
ATTRS = ck1lib.load_attrs(ATTR)

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
SPECIAL_KIND = {175:K_AMMO, 176:K_POGO,
                190:K_CARDY, 191:K_CARDR, 192:K_CARDG, 193:K_CARDB,
                221:K_JOYSTICK, 237:K_BATTERY, 241:K_VACUUM, 245:K_FUEL}
PTS_KIND = {100:K_PTS100, 200:K_PTS200, 500:K_PTS500,
            1000:K_PTS1000, 5000:K_PTS5000}
DOOR_COLOR = {173:0,174:0, 195:1,196:1, 197:2,198:2, 199:3,200:3}
EXIT_TILE  = 159
F_SOLID, F_PLAT, F_DEADLY = 1, 2, 4

# ------------------------------------------------------------ level build --
def build(idx):
    """idx 0 = world map (LEVEL80), 1..16 = levels"""
    src = 80 if idx == 0 else idx
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
    if idx == 0:
        for i, o in enumerate(lv['objs']):
            o &= 0x7fff
            if 1 <= o <= 16:
                ct = norm(ATTRS[tmap[i]]['chgtile'])
                used.add(ct)
                entries.append((i % W, i // W, o, ct))

    mts = sorted(used)
    mtidx = {t: i for i, t in enumerate(mts)}
    assert len(mts) <= 255, 'level %d: %d metatiles' % (idx, len(mts))

    # ---- subtiles with flip dedup
    subs, scache = [], {}
    def addsub(q):
        for flip, v in ((0, q), (0x200, tuple(r[::-1] for r in q)),
                        (0x400, q[::-1]), (0x600, tuple(r[::-1] for r in q[::-1]))):
            if v in scache:
                return scache[v] | flip
        scache[q] = len(subs); subs.append(q)
        return len(subs) - 1

    mtdef, mtflags = [], []
    for t in mts:
        px = TILES[t] if t < len(TILES) else [[0]*16]*16
        a = ATTRS[t]
        pri = 0x1000 if a['priority'] else 0
        for sy in range(2):
            for sx in range(2):
                q = tuple(tuple(px[sy*8+y][sx*8+x] & 15 for x in range(8))
                          for y in range(8))
                mtdef.append(addsub(q) | pri)
        fl = 0
        if a['solidl'] or a['solidr'] or a['solidceil']: fl |= F_SOLID
        elif a['solidfall']:                             fl |= F_PLAT
        if a['lethal']:                                  fl |= F_DEADLY
        mtflags.append(fl)

    # ---- map bytes
    mapb = bytes(mtidx[t] for t in tmap)

    # ---- items / doors / exits
    items, doors, exits = [], [], []
    for i, t in enumerate(tmap):
        mx, my = i % W, i // W
        a = ATTRS[t]
        if idx and a['pickupable']:
            kind = SPECIAL_KIND.get(t, PTS_KIND.get(a['points']))
            if kind is None: kind = K_PTS100
            items.append((mx, my, kind, mtidx[norm(a['chgtile'])]))
        if idx and t in DOOR_COLOR:
            doors.append((mx, my, DOOR_COLOR[t], mtidx[norm(a['chgtile'])]))
        if idx and t == EXIT_TILE:
            exits.append((mx, my))

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
                exits=exits, ents=ents, entries=entries, spawn=(sx*16, sy*16))

levels = [build(i) for i in range(17)]

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
              len(lv['entries'])*4 + len(lv['exits'])*2 + 64)
    if a_size + b_size <= BANK_CAP:
        b = alloc(a_size + b_size)
        lv['bank'] = lv['map_bank'] = b
        banks[b].append((i, 'AB'))
    else:
        ba = alloc(a_size); lv['bank'] = ba; banks[ba].append((i, 'A'))
        bb = alloc(b_size); lv['map_bank'] = bb; banks[bb].append((i, 'B'))

# ---------------------------------------------------------------- sprites --
keen_g = img_to_ega(load_rgba(HTML+'/sprites/keen.png'), True)
owk_g  = img_to_ega(load_rgba(HTML+'/sprites/keen_overworld.png'), True)
blt_g  = img_to_ega(load_rgba(HTML+'/sprites/bullet.png'), True)
yorp_g = img_to_ega(load_rgba(HTML+'/sprites/enemies/yorp.png'), True)

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

GARG_F   = [60, 64, 65, 66, 67]              # stand, walkR x2, walkL x2
VORT_F   = [74, 75, 70, 71, 80, 81]          # walkR x2, walkL x2, jumpR/L
BUTLER_F = [88, 89, 92, 93]                  # walkR x2, walkL x2 (16x16)
TANK_F   = [98, 99, 102, 103]                # walkR x2, walkL x2
garg_frames   = [ega_frame(i) for i in GARG_F]
vort_frames   = [ega_frame(i) for i in VORT_F]
butler_frames = [ega_frame(i) for i in BUTLER_F]
tank_frames   = [ega_frame(i) for i in TANK_F]
eray_frame    = ega_frame(109, 16, 8)
chunk_frame   = ega_frame(110, 16, 16)

allspr = [keen_g, owk_g, blt_g, yorp_g] + garg_frames + vort_frames + \
         butler_frames + tank_frames + [eray_frame, chunk_frame]
ega2slot, SPR_PAL = build_sprite_palette(allspr)

spr_keen = b''
for f in range(28):
    spr_keen += sprite_frame_tiles(keen_g, (f%6)*16, (f//6)*24, 2, 3, ega2slot)
spr_yorp = b''
for f in range(12):
    spr_yorp += sprite_frame_tiles(yorp_g, f*16, 0, 2, 3, ega2slot)
spr_owk = b''
for f in range(16):
    cx, cy = (f%8)*12, (f//8)*17 if load_rgba(HTML+'/sprites/keen_overworld.png').size[1] >= 34 else (f//8)*16
    pad = [[-1]*16 for _ in range(16)]
    for y in range(16):
        for x in range(12):
            if cy+y < len(owk_g) and cx+x < len(owk_g[0]):
                pad[y][x+2] = owk_g[cy+y][cx+x]
    padded = [[(0 if v < 0 else ega2slot[v]) for v in row] for row in pad]
    for ty in range(2):
        for tx in range(2):
            spr_owk += tile_planar(cut_tile(padded, tx*8, ty*8))
spr_blt = b''
for f in range(3):
    spr_blt += sprite_frame_tiles(blt_g, f*16, 0, 2, 2, ega2slot)

def frames_blob(frames):
    out = b''
    for g in frames:
        h = len(g)
        out += sprite_frame_tiles(g, 0, 0, 2, h//8, ega2slot)
    return out

spr_garg   = frames_blob(garg_frames)        # 5 x 6 tiles
spr_vort   = frames_blob(vort_frames)        # 6 x 6
spr_butler = frames_blob([  # pad 16x16 to 16x24 bottom-anchored
    [[-1]*16 for _ in range(8)] + g for g in butler_frames])
spr_tank   = frames_blob(tank_frames)        # 4 x 6
spr_eray   = sprite_frame_tiles(eray_frame, 0, 0, 2, 1, ega2slot)
spr_chunk  = sprite_frame_tiles(chunk_frame, 0, 0, 2, 2, ega2slot)

# ----------------------------------------------------------------- title ---
title = load_rgba(HTML+'/GUI/title_screen.png').resize((256,192), Image.LANCZOS)
d = ImageDraw.Draw(title)
d.text((256//2-46, 150), 'PRESS  BUTTON  1', fill=(255,255,85,255),
       font=ImageFont.load_default())
tg = img_to_ega(title)
title_tiles, title_map, tcache = [], [], {}
for ty in range(24):
    for tx in range(32):
        blob = tile_planar(cut_tile(tg, tx*8, ty*8))
        if blob not in tcache:
            tcache[blob] = len(title_tiles); title_tiles.append(blob)
        title_map.append(tcache[blob])
print('title: %d unique tiles' % len(title_tiles))

# ----------------------------------------------------------------- font ----
font_chars = 59
fimg = Image.new('L', (8*font_chars, 8), 0)
fd = ImageDraw.Draw(fimg)
ff = ImageFont.load_default()
for i in range(font_chars):
    fd.text((i*8, -1), chr(32+i), fill=255, font=ff)
font_1bpp = bytearray()
fpx = fimg.load()
for i in range(font_chars):
    for y in range(8):
        b = 0
        for x in range(8):
            if fpx[i*8+x, y] > 96: b |= 1 << (7-x)
        font_1bpp.append(b)

# ------------------------------------------------------------------ emit ---
os.makedirs(GEN, exist_ok=True)

with open(os.path.join(GEN,'bank2.c'), 'w') as f:
    f.write('/* generated: sprite graphics */\n')
    carr(f, 'spr_keen', spr_keen);   carr(f, 'spr_yorp', spr_yorp)
    carr(f, 'spr_owk', spr_owk);     carr(f, 'spr_blt', spr_blt)
    carr(f, 'spr_garg', spr_garg);   carr(f, 'spr_vort', spr_vort)
    carr(f, 'spr_butler', spr_butler); carr(f, 'spr_tank', spr_tank)
    carr(f, 'spr_eray', spr_eray);   carr(f, 'spr_chunk', spr_chunk)

with open(os.path.join(GEN,'bank3.c'), 'w') as f:
    f.write('/* generated: title screen */\n')
    carr(f, 'title_tiles', b''.join(title_tiles))
    warr(f, 'title_map', title_map)
    f.write('const unsigned int title_ntiles = %d;\n' % len(title_tiles))

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

with open(os.path.join(GEN,'game_data.h'), 'w') as f:
    f.write('#ifndef GAME_DATA_H\n#define GAME_DATA_H\n')
    f.write('#define F_SOLID  1\n#define F_PLAT   2\n#define F_DEADLY 4\n')
    f.write('#define BANK_SPRITES 2\n#define BANK_TITLE 3\n')
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
    f.write('  unsigned char bank, map_bank, nmt, W2;\n')
    f.write('  unsigned int W, H;\n')
    f.write('  int spawn_x, spawn_y;\n')
    f.write('} LevelDesc;\n')
    f.write('extern const LevelDesc level_descs[17];\n')
    f.write('extern const unsigned char bg_palette[16], spr_palette[16];\n')
    f.write('extern const unsigned char font_1bpp[%d];\n' % len(font_1bpp))
    f.write('extern const unsigned char spr_keen[],spr_yorp[],spr_owk[],'
            'spr_blt[],spr_garg[],spr_vort[],spr_butler[],spr_tank[],'
            'spr_eray[],spr_chunk[];\n')
    f.write('extern const unsigned char title_tiles[];\n')
    f.write('extern const unsigned int title_map[], title_ntiles;\n')
    for i in range(17):
        p = 'lvl%d' % i
        f.write('extern const unsigned char %s_tiles[],%s_mtflags[],%s_map[],'
                '%s_items[],%s_doors[],%s_ents[],%s_entries[],%s_exits[];\n'
                % (p,p,p,p,p,p,p,p))
        f.write('extern const unsigned int %s_mtdef[];\n' % p)
    f.write('#endif\n')

with open(os.path.join(GEN,'game_data.c'), 'w') as f:
    f.write('#include "game_data.h"\n')
    carr(f, 'bg_palette', bytes(BG_PAL))
    carr(f, 'spr_palette', bytes(SPR_PAL))
    carr(f, 'font_1bpp', bytes(font_1bpp))
    f.write('const LevelDesc level_descs[17] = {\n')
    for i in range(17):
        lv, p = levels[i], 'lvl%d' % i
        f.write('  { %s_tiles,%d, %s_mtdef,%s_mtflags,%s_map, '
                '%s_items,%d, %s_doors,%d, %s_ents,%d, %s_entries,%d, '
                '%s_exits,%d, %d,%d,%d,0, %d,%d, %d,%d },\n'
                % (p, len(lv['tiles']), p, p, p,
                   p, len(lv['items']), p, len(lv['doors']),
                   p, len(lv['ents']), p, len(lv['entries']),
                   p, len(lv['exits']),
                   lv['bank'], lv['map_bank'], len(lv['mtflags']),
                   lv['W'], lv['H'], lv['spawn'][0], lv['spawn'][1]))
    f.write('};\n')

with open(os.path.join(GEN,'banks.mk'), 'w') as f:
    bl = [2, 3] + sorted(banks)
    f.write('BANKS := %s\n' % ' '.join(str(b) for b in bl))
    f.write('BANKFLAGS := %s\n' %
            ' '.join('-Wl-b_BANK%d=0x%X' % (b, (b<<16)|0x8000) for b in bl))

print('banks used:', [2,3]+sorted(banks),
      'sizes:', {b: bank_sizes[b] for b in sorted(banks)})
print('ROM banks total:', max(banks)+1)
print('done.')
