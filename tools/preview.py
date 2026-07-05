#!/usr/bin/env python3
"""Render the *generated* SMS data (gen/bank*.c, gen/game_data.c) back to PNGs.
This verifies the exact byte arrays the console will consume."""
import re, os, sys
from PIL import Image

GEN = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'gen')
OUT = os.path.join(os.path.dirname(GEN), 'preview')
os.makedirs(OUT, exist_ok=True)

def load_arrays(path):
    s = open(path).read()
    out = {}
    for m in re.finditer(r'const unsigned (char|int) (\w+)\[(\d+)\] ?= ?\{(.*?)\};', s, re.S):
        vals = [int(v) for v in re.findall(r'\d+', m.group(4))]
        out[m.group(2)] = vals
    return out

gd = load_arrays(os.path.join(GEN,'game_data.c'))
def sms_rgb(c):
    r = (c & 3) * 85; g = ((c>>2)&3)*85; b = ((c>>4)&3)*85
    return (r,g,b)
BGPAL = [sms_rgb(c) for c in gd['bg_palette']]
SPRPAL = [sms_rgb(c) for c in gd['spr_palette']]

def decode_tile(blob, off, pal):
    im = Image.new('RGB', (8,8))
    px = im.load()
    for y in range(8):
        p0,p1,p2,p3 = blob[off+y*4:off+y*4+4]
        for x in range(8):
            bit = 7-x
            idx = ((p0>>bit)&1)|(((p1>>bit)&1)<<1)|(((p2>>bit)&1)<<2)|(((p3>>bit)&1)<<3)
            px[x,y] = pal[idx]
    return im

# levels: (name, tilebank, mapbank, W, H)
LV = [('lvl1','bank4','bank4'),('lvl2','bank5','bank5'),
      ('lvl3','bank6','bank6'),('lvl0','bank7','bank8')]
DIMS = {'lvl1':(116,17),'lvl2':(20,26),'lvl3':(77,49),'lvl0':(71,69)}
for p, tb, mb in LV:
    a = load_arrays(os.path.join(GEN, tb+'.c'))
    a.update(load_arrays(os.path.join(GEN, mb+'.c')))
    tiles = a[p+'_tiles']; mtdef = a[p+'_mtdef']; mp = a[p+'_map']
    W,H = DIMS[p]
    img = Image.new('RGB', (W*16, H*16))
    tcache = {}
    for my in range(H):
        for mx in range(W):
            m = mp[my*W+mx]
            for sy in range(2):
                for sx in range(2):
                    t = mtdef[m*4+sy*2+sx]
                    if t not in tcache:
                        tcache[t] = decode_tile(tiles, t*32, BGPAL)
                    img.paste(tcache[t], (mx*16+sx*8, my*16+sy*8))
    img.save(os.path.join(OUT, p+'.png'))
    print(p, img.size)

# title
a = load_arrays(os.path.join(GEN,'bank3.c'))
tiles, tmap = a['title_tiles'], a['title_map']
img = Image.new('RGB',(256,192))
for ty in range(24):
    for tx in range(32):
        img.paste(decode_tile(tiles, tmap[ty*32+tx]*32, BGPAL), (tx*8,ty*8))
img.save(os.path.join(OUT,'title.png')); print('title', img.size)

# sprites: keen frames strip + yorp + owkeen + bullet
a = load_arrays(os.path.join(GEN,'bank2.c'))
def strip(name, nframes, wt, ht):
    data = a[name]
    img = Image.new('RGB',(nframes*wt*8, ht*8),(40,40,40))
    for f in range(nframes):
        base = f*wt*ht*32
        i = 0
        for ty in range(ht):
            for tx in range(wt):
                img.paste(decode_tile(data, base+i*32, SPRPAL), (f*wt*8+tx*8, ty*8))
                i += 1
    img.save(os.path.join(OUT,name+'.png')); print(name, img.size)
strip('spr_keen', 28, 2, 3)
strip('spr_yorp', 12, 2, 3)
strip('spr_owk', 16, 2, 2)
strip('spr_blt', 3, 2, 2)
