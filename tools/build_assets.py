#!/usr/bin/env python3
"""
build_assets.py -- HTML5-Keen -> Sega Master System asset pipeline.

Reads the original melonJS game data (TMX levels, PNG tilesets/sprites)
and emits devkitSMS-ready C data:

  gen/game_data.h   externs + structs + constants   (fixed ROM)
  gen/game_data.c   level descriptor tables + font + palettes (fixed ROM)
  gen/bank2.c       sprite graphics (keen, yorp, ow-keen, bullet)
  gen/bank3.c       title screen (tiles + tilemap)
  gen/bank4.c       level 1 data
  gen/bank5.c       level 2 data
  gen/bank6.c       level 3 data
  gen/bank7.c       mars overworld data

World model: 16x16 metatiles (= 2x2 SMS 8x8 tiles). Per level we emit
  - a subtile blob (unique 8x8 tiles, SMS 4bpp planar, loaded at VRAM tile 0)
  - metatile defs (4 subtile indices each) + per-metatile flags
  - the map as one byte (metatile index) per cell
  - object lists (items, deadly stamps are pre-stamped, yorps, exit, spawn)
Items are composited over the background art at their exact cell, producing
an "item metatile" placed in the initial map plus a "restore metatile"
(the background alone) that the engine writes back on pickup.
"""
import os, re, sys, base64, struct
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KEEN = os.environ.get('KEEN_SRC', '/home/claude/work/HTML5-Keen-master')
DATA = os.path.join(KEEN, 'data')
GEN  = os.path.join(ROOT, 'gen')
os.makedirs(GEN, exist_ok=True)

# ---------------------------------------------------------------- palettes --
EGA = [
    (0x00,0x00,0x00),(0x00,0x00,0xAA),(0x00,0xAA,0x00),(0x00,0xAA,0xAA),
    (0xAA,0x00,0x00),(0xAA,0x00,0xAA),(0xAA,0x55,0x00),(0xAA,0xAA,0xAA),
    (0x55,0x55,0x55),(0x55,0x55,0xFF),(0x55,0xFF,0x55),(0x55,0xFF,0xFF),
    (0xFF,0x55,0x55),(0xFF,0x55,0xFF),(0xFF,0xFF,0x55),(0xFF,0xFF,0xFF),
]
def nearest_ega(rgb):
    r,g,b = rgb
    best, bi = 1<<30, 0
    for i,(er,eg,eb) in enumerate(EGA):
        d = (r-er)**2 + (g-eg)**2 + (b-eb)**2
        if d < best: best, bi = d, i
    return bi
def sms_color(rgb):
    r,g,b = rgb
    q = lambda v: (v*3 + 127)//255      # 0..255 -> 0..3
    return (q(b)<<4) | (q(g)<<2) | q(r)

BG_PAL  = [sms_color(c) for c in EGA]                     # BG CRAM = EGA order

# ------------------------------------------------------------ image helpers --
def load_rgba(path):
    return Image.open(path).convert('RGBA')

def img_to_ega(img, transparent=False):
    """RGBA image -> 2D list of palette indices. transparent pixels -> -1."""
    w,h = img.size
    px = img.load()
    out = [[0]*w for _ in range(h)]
    cache = {}
    for y in range(h):
        for x in range(w):
            r,g,b,a = px[x,y]
            if transparent and a < 128:
                out[y][x] = -1
                continue
            key = (r,g,b)
            if key not in cache:
                cache[key] = nearest_ega(key)
            out[y][x] = cache[key]
    return out

def tile_planar(idx8x8):
    """8x8 list of 4-bit indices -> 32 bytes SMS planar."""
    out = bytearray()
    for row in idx8x8:
        for p in range(4):
            b = 0
            for x in range(8):
                b |= ((row[x]>>p)&1) << (7-x)
            out.append(b)
    return bytes(out)

def cut_tile(grid, x, y):
    return [grid[y+r][x:x+8] for r in range(8)]

# ---------------------------------------------------------------- TMX parse --
def parse_tmx(path):
    s = open(path).read()
    m = re.search(r'<map[^>]*width="(\d+)" height="(\d+)"', s)
    W,H = int(m.group(1)), int(m.group(2))
    tilesets = []   # (firstgid, name, image_source)
    for m in re.finditer(r'<tileset firstgid="(\d+)" name="([^"]*)"[^>]*>\s*<image source="([^"]*)"', s):
        tilesets.append((int(m.group(1)), m.group(2), m.group(3)))
    layers = {}
    for m in re.finditer(r'<layer name="([^"]*)"[^>]*>.*?<data encoding="base64"[^>]*>\s*([A-Za-z0-9+/=\s]+?)\s*</data>', s, re.S):
        raw = base64.b64decode(re.sub(r'\s','',m.group(2)))
        gids = list(struct.unpack('<%dI' % (len(raw)//4), raw))
        layers[m.group(1)] = gids
    objects = []
    for m in re.finditer(r'<object name="([^"]*)" x="(\d+)" y="(\d+)" width="(\d+)" height="(\d+)"\s*(/>|>(.*?)</object>)', s, re.S):
        name = m.group(1); x=int(m.group(2)); y=int(m.group(3))
        w=int(m.group(4)); h=int(m.group(5))
        props = {}
        if m.group(7):
            for pm in re.finditer(r'<property name="([^"]*)" value="([^"]*)"', m.group(7)):
                props[pm.group(1)] = pm.group(2)
        objects.append(dict(name=name,x=x,y=y,w=w,h=h,props=props))
    return dict(W=W,H=H,tilesets=tilesets,layers=layers,objects=objects)

# ---------------------------------------------------------- metatile builder --
F_SOLID, F_PLAT, F_DEADLY = 1, 2, 4

class LevelBuilder:
    """Builds per-level subtile pool / metatile set / map."""
    def __init__(self, name):
        self.name = name
        self.tiles = []          # list of 32-byte planar blobs
        self.tile_idx = {}       # blob -> index
        self.mts = []            # list of (t0,t1,t2,t3,flags)
        self.mt_idx = {}
        self.map = None          # list of metatile indices, W*H
        self.W = self.H = 0

    def add_tile(self, blob):
        i = self.tile_idx.get(blob)
        if i is None:
            i = len(self.tiles)
            self.tiles.append(blob)
            self.tile_idx[blob] = i
        return i

    def add_mt(self, cell16, flags):
        """cell16: 16x16 grid of EGA indices (no transparency)."""
        t = tuple(self.add_tile(tile_planar(cut_tile(cell16, tx*8, ty*8)))
                  for ty in range(2) for tx in range(2))
        key = t + (flags,)
        i = self.mt_idx.get(key)
        if i is None:
            i = len(self.mts)
            self.mts.append(key)
            self.mt_idx[key] = i
        return i

def cell_from_tileset(ts_grid, ts_cols, tid):
    """Extract 16x16 EGA cell #tid from a tileset grid (16px cells)."""
    cx, cy = (tid % ts_cols)*16, (tid // ts_cols)*16
    return [ts_grid[cy+r][cx:cx+16] for r in range(16)]

def blank_cell(color):
    return [[color]*16 for _ in range(16)]

def composite(base, overlay, ox=0, oy=0):
    """Paste overlay (with -1 = transparent) onto a copy of base."""
    out = [row[:] for row in base]
    for y,row in enumerate(overlay):
        for x,v in enumerate(row):
            if v >= 0 and 0 <= y+oy < 16 and 0 <= x+ox < 16:
                out[y+oy][x+ox] = v
    return out

# ----------------------------------------------------------------- sprites --
def build_sprite_palette(images):
    """Collect EGA indices used by sprite art; map to CRAM slots 1..15.
       If all 16 EGA colors appear, remap the least-used one to its
       visually nearest used neighbour (slot 0 must stay transparent)."""
    counts = {}
    for img in images:
        g = img_to_ega(img, transparent=True)
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
        print('sprite palette: remapping EGA %d -> %d (%d px)' % (victim, near, counts[near]))
    ega2slot = {e: i+1 for i,e in enumerate(used)}
    for v,n in remap.items(): ega2slot[v] = ega2slot[n]
    cram = [0]*16
    for e in used:
        cram[ega2slot[e]] = sms_color(EGA[e])
    return ega2slot, cram

def sprite_frame_tiles(grid, x, y, w_t, h_t, ega2slot):
    """Cut w_t x h_t tiles (column-major per row: TL,TR / ML,MR ...) with
       transparency -> slot 0, colors remapped to sprite palette slots."""
    out = b''
    for ty in range(h_t):
        for tx in range(w_t):
            t8 = [[(0 if grid[y+ty*8+r][x+tx*8+c] < 0
                    else ega2slot[grid[y+ty*8+r][x+tx*8+c]])
                   for c in range(8)] for r in range(8)]
            out += tile_planar(t8)
    return out

# ------------------------------------------------------------------- C emit --
def carr(f, name, data, per=24):
    f.write('const unsigned char %s[%d] = {\n' % (name, len(data)))
    for i in range(0, len(data), per):
        f.write('  ' + ','.join(str(b) for b in data[i:i+per]) + ',\n')
    f.write('};\n')

# ================================================================== LEVELS ==
ITEM_KINDS = ['lollipop','soda','pizza','book','teddy-bear','raygun','pogo',
              'keycard-a','keycard-b','keycard-c','keycard-d']
ITEM_SCORE = [100,200,500,1000,5000,0,0,500,500,500,500]
DEADLY_STAMPS = {'pat-pat','garg','vorticon'}

ITEM_FILES = {k: k for k in ITEM_KINDS}
ITEM_FILES['pogo'] = 'pogo-stick'
item_imgs = {}
for k in ITEM_KINDS:
    item_imgs[k] = img_to_ega(load_rgba(os.path.join(DATA,'sprites/items/%s.png'%ITEM_FILES[k])), transparent=True)
patpat_img = img_to_ega(load_rgba(os.path.join(DATA,'sprites/enemies/pat-pat.png')), transparent=True)
patpat0 = [row[0:14] for row in patpat_img]                # first 14x16 frame
spikes_img = img_to_ega(load_rgba(os.path.join(DATA,'sprites/environment/green-spikes.png')), transparent=True)
spikes0 = [row[0:32] for row in spikes_img[0:16]]          # first 32x16 frame

LEVELS = [
    dict(id=1, tmx='1.tmx',    ts='tilesets/main.png',        bgcol=7),
    dict(id=2, tmx='2.tmx',    ts='tilesets/levels/2.png',    bgcol=0),
    dict(id=3, tmx='3.tmx',    ts='tilesets/levels/3.png',    bgcol=7),
    dict(id=0, tmx='mars.tmx', ts='tilesets/levels/mars.png', bgcol=0),  # overworld
]

def build_level(cfg):
    tmx = parse_tmx(os.path.join(DATA,'levels',cfg['tmx']))
    W,H = tmx['W'], tmx['H']
    ts_img = load_rgba(os.path.join(DATA, cfg['ts']))
    ts_grid = img_to_ega(ts_img)
    ts_cols = ts_img.size[0]//16

    gfx_firstgid = tmx['tilesets'][0][0]
    meta_firstgid = None
    for fg,name,src in tmx['tilesets']:
        if 'meta' in name.lower(): meta_firstgid = fg
    assert meta_firstgid

    # merge visual layers (Foreground over Background/Ground)
    vis = [0]*(W*H)
    for lname in ('Background','Ground','Foreground'):
        if lname in tmx['layers']:
            for i,g in enumerate(tmx['layers'][lname]):
                if g: vis[i] = g
    coll = tmx['layers'].get('Collision', [0]*(W*H))

    lb = LevelBuilder(cfg['tmx'].split('.')[0])
    lb.W, lb.H = W, H
    bg = cfg['bgcol']

    def cell_art(i):
        g = vis[i]
        if g == 0:
            return blank_cell(bg)
        tid = g - gfx_firstgid
        if tid < 0 or tid >= (ts_img.size[0]//16)*(ts_img.size[1]//16):
            return blank_cell(bg)
        return cell_from_tileset(ts_grid, ts_cols, tid)

    def cell_flags(i):
        g = coll[i]
        if not g: return 0
        mid = g - meta_firstgid
        if mid == 0: return F_SOLID
        if mid == 1: return F_PLAT
        return F_SOLID          # any other meta marker -> solid

    lb.map = [0]*(W*H)
    art_cache = {}
    for i in range(W*H):
        key = (vis[i], cell_flags(i))
        if key not in art_cache:
            art_cache[key] = lb.add_mt(cell_art(i), cell_flags(i))
        lb.map[i] = art_cache[key]

    # -------- objects
    items, yorps, entries, blocks_raw = [], [], [], []
    spawn = (16,16); exit_rect=(0,0,0,0)
    def cellxy(o): return o['x']//16, o['y']//16

    for o in tmx['objects']:
        n = o['name']
        mx,my = cellxy(o)
        idx = my*W+mx
        if n in ('mainPlayer','mainPlayerOW'):
            spawn = (o['x'], o['y'])
        elif n == 'exit':
            exit_rect = (mx,my, o['w']//16, o['h']//16)
        elif n in ITEM_KINDS:
            kind = ITEM_KINDS.index(n)
            base = cell_art(idx); restore = lb.map[idx]
            ov = item_imgs[n]
            oh, ow = len(ov), len(ov[0])
            stamped = composite(base, ov, ox=(16-ow)//2, oy=16-oh)
            item_mt = lb.add_mt(stamped, cell_flags(idx))
            lb.map[idx] = item_mt
            items.append((mx,my,kind,restore))
        elif n == 'green-spikes':
            for c in range(2):
                idx2 = my*W+mx+c
                base = cell_art(idx2)
                part = [row[c*16:(c+1)*16] for row in spikes0]
                lb.map[idx2] = lb.add_mt(composite(base, part),
                                         cell_flags(idx2)|F_DEADLY)
        elif n in DEADLY_STAMPS:
            base = cell_art(idx)
            lb.map[idx] = lb.add_mt(composite(base, patpat0, ox=1, oy=0),
                                    cell_flags(idx)|F_DEADLY)
        elif n == 'yorp':
            yorps.append((o['x'], o['y']-8))    # yorp is 16x24; obj is 16x16 anchor
        elif n == 'level':
            ln = o['props'].get('levelname','')
            if ln in ('1','2','3'):
                entries.append((mx,my,max(1,o['w']//16),max(1,o['h']//16),int(ln)))
        elif n == 'level-block':
            cells = []
            for dy in range(max(1,o['h']//16)):
                for dx in range(max(1,o['w']//16)):
                    ii=(my+dy)*W+(mx+dx)
                    key = lb.mts[lb.map[ii]]
                    if not (key[4] & F_SOLID): cells.append((mx+dx, my+dy))
                    lb.map[ii] = lb.add_mt_from_key(key) if False else lb.mt_idx.setdefault(
                        key[:4]+(key[4]|F_SOLID,),
                        lb._dup(key[:4], key[4]|F_SOLID))
            if cells:
                blocks_raw.append((cells,
                                   o['x'] + o['w']/2.0, o['y'] + o['h']/2.0))
    limit = 384 if cfg['id']==0 else 256
    if len(lb.tiles) > limit:
        raise SystemExit('%s: %d subtiles (>%d)' % (lb.name, len(lb.tiles), limit))
    if len(lb.mts) > 255:
        raise SystemExit('%s: %d metatiles (>255)' % (lb.name, len(lb.mts)))
    blocks = []
    for cells, bx, by in blocks_raw:
        best, bd = None, None
        for (emx,emy,ew,eh,elvl) in entries:
            ex, ey = (emx + ew/2.0)*16, (emy + eh/2.0)*16
            d = (ex-bx)*(ex-bx) + (ey-by)*(ey-by)
            if bd is None or d < bd: bd, best = d, elvl
        if best is not None:
            blocks += [(cx, cy, best) for (cx, cy) in cells]
    print('%-8s %3dx%-3d subtiles=%3d metatiles=%3d items=%2d yorps=%d entries=%d blocks=%d'
          % (lb.name, W,H, len(lb.tiles), len(lb.mts), len(items), len(yorps), len(entries), len(blocks)))
    return dict(lb=lb, items=items, yorps=yorps, entries=entries, blocks=blocks,
                spawn=spawn, exit=exit_rect, cfg=cfg)

def _dup(self, t4, flags):
    key = t4+(flags,)
    if key in self.mt_idx: return self.mt_idx[key]
    i = len(self.mts); self.mts.append(key); self.mt_idx[key]=i
    return i
LevelBuilder._dup = _dup

built = [build_level(c) for c in LEVELS]

# ================================================================ SPRITES ===
keen_img  = load_rgba(os.path.join(DATA,'sprites/keen.png'))
yorp_img  = load_rgba(os.path.join(DATA,'sprites/enemies/yorp.png'))
owk_img   = load_rgba(os.path.join(DATA,'sprites/keen_overworld.png'))
blt_img   = load_rgba(os.path.join(DATA,'sprites/bullet.png'))
ega2slot, SPR_PAL = build_sprite_palette([keen_img, yorp_img, owk_img, blt_img])

keen_g = img_to_ega(keen_img, transparent=True)
yorp_g = img_to_ega(yorp_img, transparent=True)
owk_g  = img_to_ega(owk_img,  transparent=True)
blt_g  = img_to_ega(blt_img,  transparent=True)

spr_keen = b''
for f in range(28):                       # 6 cols x 6 rows grid of 16x24 frames
    cx,cy = (f%6)*16, (f//6)*24
    spr_keen += sprite_frame_tiles(keen_g, cx, cy, 2, 3, ega2slot)

spr_yorp = b''
for f in range(12):                       # 12 frames of 16x24 in a row
    spr_yorp += sprite_frame_tiles(yorp_g, f*16, 0, 2, 3, ega2slot)

# ow keen: 8 cols x 2 rows of 12x16 -> pad into 16x16 (2x2 tiles)
spr_owk = b''
for f in range(16):
    cx, cy = (f%8)*12, (f//8)*17 if owk_img.size[1]>=34 else (f//8)*16
    pad = [[-1]*16 for _ in range(16)]
    for y in range(16):
        for x in range(12):
            if cy+y < len(owk_g) and cx+x < len(owk_g[0]):
                pad[y][x+2] = owk_g[cy+y][cx+x]
    padded = [[(0 if v<0 else ega2slot[v]) for v in row] for row in pad]
    for ty in range(2):
        for tx in range(2):
            spr_owk += tile_planar(cut_tile(padded, tx*8, ty*8))

spr_blt = b''
for f in range(3):                        # fly / zap / zot, 16x16
    spr_blt += sprite_frame_tiles(blt_g, f*16, 0, 2, 2, ega2slot)

# =============================================================== TITLE ======
title = load_rgba(os.path.join(DATA,'GUI/title_screen.png')).resize((256,192), Image.LANCZOS)
d = ImageDraw.Draw(title)
try: fnt = ImageFont.load_default()
except Exception: fnt = None
msg = 'PRESS  BUTTON  1'
d.text((256//2-46, 150), msg, fill=(255,255,85,255), font=fnt)
tg = img_to_ega(title)
title_tiles, title_map, tcache = [], [], {}
for ty in range(24):
    for tx in range(32):
        blob = tile_planar(cut_tile(tg, tx*8, ty*8))
        if blob not in tcache:
            tcache[blob] = len(title_tiles); title_tiles.append(blob)
        title_map.append(tcache[blob])
print('title: %d unique tiles' % len(title_tiles))
if len(title_tiles) > 448:
    raise SystemExit('title screen too many tiles')

# =============================================================== FONT =======
# 8x8 font for chars 32..90 rendered with PIL default bitmap font (1bpp).
font_chars = 59
fimg = Image.new('L', (8*font_chars, 8), 0)
fd = ImageDraw.Draw(fimg)
ffont = ImageFont.load_default()
for i in range(font_chars):
    fd.text((i*8, -1), chr(32+i), fill=255, font=ffont)
font_1bpp = bytearray()
fpx = fimg.load()
for i in range(font_chars):
    for y in range(8):
        b = 0
        for x in range(8):
            if fpx[i*8+x, y] > 96: b |= 1 << (7-x)
        font_1bpp.append(b)

# =============================================================== EMIT =======
def emit_level_bank(bank, b, map_bank=None):
    lb, cfg = b['lb'], b['cfg']
    p = 'lvl%d' % cfg['id']
    if map_bank is None: map_bank = bank
    f2path = os.path.join(GEN,'bank%d.c'%map_bank)
    with open(os.path.join(GEN,'bank%d.c'%bank),'w') as f:
        f.write('/* generated: %s tiles */\n' % lb.name)
        carr(f, p+'_tiles', b''.join(lb.tiles))
    with open(f2path, 'a' if map_bank==bank else 'w') as f:
        f.write('/* generated: %s map+objects */\n' % lb.name)
        mtdef = [x for mt in lb.mts for x in mt[:4]]
        f.write('const unsigned int %s_mtdef[%d] = {\n' % (p, len(mtdef)))
        for i in range(0, len(mtdef), 16):
            f.write('  '+','.join(str(v) for v in mtdef[i:i+16])+',\n')
        f.write('};\n')
        carr(f, p+'_mtflags', bytes(mt[4] for mt in lb.mts))
        carr(f, p+'_map', bytes(lb.map))
        items = b['items']
        f.write('const unsigned char %s_items[%d] = {\n' % (p, max(1,len(items)*4)))
        for mx,my,kind,restore in items:
            f.write('  %d,%d,%d,%d,\n' % (mx,my,kind,restore))
        if not items: f.write('0,')
        f.write('};\n')
        yorps = b['yorps']
        f.write('const unsigned int %s_yorps[%d] = {' % (p, max(1,len(yorps)*2)))
        f.write(','.join('%d,%d'%(x,y) for x,y in yorps) or '0')
        f.write('};\n')
        entries = b['entries']
        f.write('const unsigned char %s_entries[%d] = {' % (p, max(1,len(entries)*5)))
        f.write(','.join('%d,%d,%d,%d,%d'%(e) for e in entries) or '0')
        f.write('};\n')
        blocks = b['blocks']
        f.write('const unsigned char %s_blocks[%d] = {' % (p, max(1,len(blocks)*3)))
        f.write(','.join('%d,%d,%d'%(e) for e in blocks) or '0')
        f.write('};\n')
    return dict(prefix=p, bank=bank, map_bank=map_bank, ntiles=len(lb.tiles), nmt=len(lb.mts),
                W=lb.W, H=lb.H, nitems=len(items), nyorps=len(yorps),
                nentries=len(entries), nblocks=len(blocks),
                spawn=b['spawn'], exit=b['exit'])

descs = []
order = {1:4, 2:5, 3:6, 0:7}
for b in built:
    lid = b['cfg']['id']
    if lid == 0:
        descs.append(emit_level_bank(7, b, map_bank=8))
    else:
        descs.append(emit_level_bank(order[lid], b))

with open(os.path.join(GEN,'bank2.c'),'w') as f:
    f.write('/* generated: sprite graphics */\n')
    carr(f,'spr_keen',  spr_keen)
    carr(f,'spr_yorp',  spr_yorp)
    carr(f,'spr_owk',   spr_owk)
    carr(f,'spr_blt',   spr_blt)

with open(os.path.join(GEN,'bank3.c'),'w') as f:
    f.write('/* generated: title screen */\n')
    carr(f,'title_tiles', b''.join(title_tiles))
    f.write('const unsigned int title_map[768] = {\n')
    for i in range(0,768,16):
        f.write('  '+','.join(str(v) for v in title_map[i:i+16])+',\n')
    f.write('};\n')
    f.write('const unsigned int title_ntiles = %d;\n' % len(title_tiles))

# fixed-ROM data ------------------------------------------------------------
with open(os.path.join(GEN,'game_data.h'),'w') as f:
    f.write('''#ifndef GAME_DATA_H
#define GAME_DATA_H
#define F_SOLID  1
#define F_PLAT   2
#define F_DEADLY 4
#define BANK_SPRITES 2
#define BANK_TITLE   3
typedef struct {
  const unsigned char *tiles;   unsigned int  tiles_size;
  const unsigned int  *mtdef;   const unsigned char *mtflags;
  const unsigned char *map;
  const unsigned char *items;   unsigned char nitems;
  const unsigned int  *yorps;   unsigned char nyorps;
  const unsigned char *entries; unsigned char nentries;
  const unsigned char *blocks;  unsigned char nblocks;
  unsigned char bank, map_bank, W, H, nmt;
  unsigned int  spawn_x, spawn_y;
  unsigned char exit_mx, exit_my, exit_mw, exit_mh;
} LevelDesc;
extern const LevelDesc level_descs[4];   /* [0]=overworld, [1..3]=levels */
extern const unsigned char bg_palette[16];
extern const unsigned char spr_palette[16];
extern const unsigned char font_1bpp[];
extern const unsigned char item_score_hi[11], item_score_lo[11];
''')
    for dsc in descs:
        p=dsc['prefix']
        f.write('extern const unsigned char %s_tiles[],%s_mtflags[],%s_map[],%s_items[],%s_entries[],%s_blocks[];\n'
                % (p,p,p,p,p,p))
        f.write('extern const unsigned int %s_mtdef[];\n' % p)
        f.write('extern const unsigned int %s_yorps[];\n' % p)
    f.write('extern const unsigned char spr_keen[],spr_yorp[],spr_owk[],spr_blt[];\n')
    f.write('extern const unsigned char title_tiles[]; extern const unsigned int title_map[768]; extern const unsigned int title_ntiles;\n')
    f.write('#endif\n')

with open(os.path.join(GEN,'game_data.c'),'w') as f:
    f.write('#include "game_data.h"\n')
    f.write('const unsigned char bg_palette[16]={%s};\n' % ','.join(map(str,BG_PAL)))
    f.write('const unsigned char spr_palette[16]={%s};\n' % ','.join(map(str,SPR_PAL)))
    carr(f,'font_1bpp',bytes(font_1bpp))
    f.write('const unsigned char item_score_hi[11]={%s};\n' % ','.join(str(s>>8) for s in ITEM_SCORE))
    f.write('const unsigned char item_score_lo[11]={%s};\n' % ','.join(str(s&255) for s in ITEM_SCORE))
    f.write('const LevelDesc level_descs[4] = {\n')
    by_id = {}
    for dsc in descs: by_id[int(dsc['prefix'][3:])] = dsc
    for lid in (0,1,2,3):
        dsc = by_id[lid]; p = dsc['prefix']
        ex = dsc['exit']
        f.write('  { %s_tiles,%d, %s_mtdef,%s_mtflags,%s_map, %s_items,%d, %s_yorps,%d, %s_entries,%d, %s_blocks,%d, %d,%d,%d,%d,%d, %d,%d, %d,%d,%d,%d },\n'
          % (p, dsc['ntiles']*32, p,p,p, p,dsc['nitems'], p,dsc['nyorps'], p,dsc['nentries'],
             p,dsc['nblocks'],
             dsc['bank'], dsc['map_bank'], dsc['W'], dsc['H'], dsc['nmt'],
             dsc['spawn'][0], dsc['spawn'][1], ex[0],ex[1],ex[2],ex[3]))
    f.write('};\n')

print('sprite palette slots:', SPR_PAL)
print('done.')
