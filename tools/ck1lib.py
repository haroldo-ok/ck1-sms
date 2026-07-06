#!/usr/bin/env python3
"""Decoders for Commander Keen 1 data files (EGAHEAD/EGALATCH/LEVELxx.CK1)
plus CloneKeen's extracted tile-attribute table. Self-contained; used by
convert_ck1.py to build the SMS data banks."""
import struct, os

# ------------------------------------------------------------------ LZW ----
def lzw_decompress(data, outlen):
    """Keen's LZW: MSB-first bitstream, 9..12-bit codes, 257=EOF, dictionary
    grows from 258; code width bumps when the next free index reaches
    (1<<bits)-1 (one early vs. textbook LZW); new entry is defined before
    the current code is emitted (handles the KwKwK case implicitly)."""
    out = bytearray()
    strings = [bytes([i]) for i in range(256)] + [b''] * ((1 << 12) + 1 - 256)
    bitpos = 0

    def readbits(n):
        nonlocal bitpos
        v = 0
        for _ in range(n):
            v = (v << 1) | ((data[bitpos >> 3] >> (7 - (bitpos & 7))) & 1)
            bitpos += 1
        return v

    numbits = 9
    maxidx = (1 << numbits) - 1
    nextidx = 258
    grow_ok = True

    last = readbits(numbits)
    out += strings[last]
    while len(out) < outlen:
        code = readbits(numbits)
        if code in (256, 257):
            break
        src = last if not strings[code] else code
        if grow_ok:
            strings[nextidx] = strings[last] + strings[src][:1]
            nextidx += 1
            if nextidx >= maxidx:
                if numbits < 12:
                    numbits += 1
                    maxidx = (1 << numbits) - 1
                else:
                    grow_ok = False
        out += strings[code]
        last = code
    return bytes(out[:outlen])

# ------------------------------------------------------------ EGA header ---
def load_header(path):
    d = open(path, 'rb').read()
    f = struct.unpack_from
    h = {}
    (h['LatchPlaneSize'], h['SpritePlaneSize'], h['OffBitmapTable'],
     h['OffSpriteTable']) = f('<4L', d, 0)
    o = 16
    h['Num8Tiles'],  h['Off8Tiles']  = f('<HL', d, o); o += 6
    h['Num32Tiles'], h['Off32Tiles'] = f('<HL', d, o); o += 6
    h['Num16Tiles'], h['Off16Tiles'] = f('<HL', d, o); o += 6
    h['NumBitmaps'], h['OffBitmaps'] = f('<HL', d, o); o += 6
    h['NumSprites'], h['OffSprites'] = f('<HL', d, o); o += 6
    h['Compressed'], = f('<H', d, o)
    h['raw'] = d
    return h

def sprite_table(hdr):
    """EGAHEAD sprite records: 8 uint16 fields + 16 name bytes = 32 bytes,
    and each sprite's record is repeated 4 times (128-byte stride)."""
    d = hdr['raw']
    out = []
    o = hdr['OffSpriteTable']
    for i in range(hdr['NumSprites']):
        w, h, od, op, rx1, ry1, rx2, ry2 = struct.unpack_from('<8H', d, o)
        o += 32 * 4
        out.append(dict(w=w*8, h=h, bitoff=op*16*8 + od,
                        rx1=rx1 >> 8, ry1=ry1 >> 8, rx2=rx2 >> 8, ry2=ry2 >> 8))
    return out

# ------------------------------------------------------------- EGA latch ---
def load_latch_tiles16(latch_path, hdr):
    raw = open(latch_path, 'rb').read()
    size = hdr['LatchPlaneSize'] * 4
    if hdr['Compressed']:
        raw = lzw_decompress(raw[6:], size)
    else:
        raw = raw[:size]
    n = hdr['Num16Tiles']
    base = hdr['Off16Tiles']
    ps = hdr['LatchPlaneSize']
    tiles = []
    rowbytes = 2                                    # 16 px = 2 bytes/plane
    for t in range(n):
        px = [[0]*16 for _ in range(16)]
        for p in range(4):
            off = base + p*ps + t*16*rowbytes
            for y in range(16):
                b0 = raw[off + y*2]
                b1 = raw[off + y*2 + 1]
                bits = (b0 << 8) | b1
                for x in range(16):
                    if bits & (0x8000 >> x):
                        px[y][x] |= (1 << p)
        tiles.append(px)
    return tiles

def load_sprites(sprit_path, hdr):
    """EGASPRIT: 5 planes (4 color + 1 mask). Within each plane the sprites
    are stored back-to-back as a continuous bitstream (rows not padded),
    each sprite contributing w*h bits in index order. Returns 2D pixel
    arrays with -1 = transparent (mask bit clear)."""
    raw = open(sprit_path, 'rb').read()
    size = hdr['SpritePlaneSize'] * 5
    if hdr['Compressed']:
        raw = lzw_decompress(raw[6:], size)
    else:
        raw = raw[:size]
    ps = hdr['SpritePlaneSize']
    tab = sprite_table(hdr)

    def reader(plane, bitoff):
        pos = (hdr['OffSprites'] + plane * ps) * 8 + bitoff
        def rd():
            nonlocal pos
            b = (raw[pos >> 3] >> (7 - (pos & 7))) & 1
            pos += 1
            return b
        return rd

    out = []
    for s in tab:
        w, h = s['w'], s['h']
        px = [[0] * w for _ in range(h)]
        for p in range(4):
            rd = reader(p, s['bitoff'])
            for y in range(h):
                for x in range(w):
                    if rd():
                        px[y][x] |= (1 << p)
        rd = reader(4, s['bitoff'])           # mask plane: 1 = opaque
        for y in range(h):
            for x in range(w):
                if not rd():
                    px[y][x] = -1
        out.append(px)
    return out

# --------------------------------------------------------- tile attrs ------
ATTR_REC = 20
def load_attrs(path):
    d = open(path, 'rb').read()
    assert d[:3] == b'ATR', 'bad attr file'
    n = (len(d) - 5) // ATTR_REC
    out = []
    o = 5
    for t in range(n):
        (solidl, solidr, solidfall, solidceil, ice, semiice, priority,
         masktile, goodie, standgoodie, pickupable) = d[o:o+11]
        points, = struct.unpack_from('<h', d, o+11)
        lethal, bonklethal = d[o+13:o+15]
        chgtile, = struct.unpack_from('<h', d, o+15)
        isanim, animoff, animlen = d[o+17:o+20]
        out.append(dict(solidl=solidl, solidr=solidr, solidfall=solidfall,
                        solidceil=solidceil, ice=ice, priority=priority,
                        goodie=goodie, pickupable=pickupable, points=points,
                        lethal=lethal, chgtile=chgtile,
                        isanim=isanim, animoff=animoff, animlen=animlen))
        o += ATTR_REC
    return out

# ------------------------------------------------------------- levels ------
def rle_words(d):
    outlen, = struct.unpack_from('<L', d, 0)
    nwords = outlen // 2
    out = []
    o = 4
    while len(out) < nwords and o + 1 < len(d):
        w, = struct.unpack_from('<H', d, o); o += 2
        if w == 0xFEFE:
            cnt, = struct.unpack_from('<H', d, o); o += 2
            val, = struct.unpack_from('<H', d, o); o += 2
            out += [val]*cnt
        else:
            out.append(w)
    return out

def load_level(path):
    words = rle_words(open(path, 'rb').read())
    W, H = words[0], words[1]
    plane_size = words[7]                    # bytes
    p1 = 16
    p2 = (p1 + plane_size // 2 + 7) & ~7     # round up to 8-word boundary
    tilemap = [words[p1 + y*W + x] for y in range(H) for x in range(W)]
    objmap  = [words[p2 + y*W + x] for y in range(H) for x in range(W)]
    spawn = None
    for i, t in enumerate(objmap):
        if t == 255:
            spawn = (i % W, i // W)
            objmap[i] = 0
    return dict(W=W, H=H, tiles=tilemap, objs=objmap, spawn=spawn)
