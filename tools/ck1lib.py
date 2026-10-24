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
                elif nextidx >= (1 << 12):
                    # per CloneKeen's lz.c: at 12 bits the FINAL entry 4095
                    # is still defined; adding shuts off only at index 4096.
                    # Stopping one early silently drops bytes whenever the
                    # encoder later emits code 4095 (empty string), shifting
                    # everything decoded after that point.
                    grow_ok = False
        out += strings[code]
        last = code
    out = out[:outlen]
    if len(out) < outlen:                     # streams may omit tail padding
        out += bytes(outlen - len(out))
    return bytes(out)

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

    def reader(plane):
        pos = [(hdr['OffSprites'] + plane * ps) * 8]
        def rd():
            b = (raw[pos[0] >> 3] >> (7 - (pos[0] & 7))) & 1
            pos[0] += 1
            return b
        return rd

    # CloneKeen reads each plane as ONE continuous bitstream across all
    # sprites (the table's OffsetParas/Delta fields are ignored there, and
    # trusting them shears several sprites).
    color_rd = [reader(p) for p in range(4)]
    out = [[[0] * s['w'] for _ in range(s['h'])] for s in tab]
    for p in range(4):
        rd = color_rd[p]
        for si, s in enumerate(tab):
            for y in range(s['h']):
                for x in range(s['w']):
                    if rd():
                        out[si][y][x] |= (1 << p)
    rd = reader(4)                            # mask plane: 1 = TRANSPARENT
    for si, s in enumerate(tab):
        for y in range(s['h']):
            for x in range(s['w']):
                if rd():
                    out[si][y][x] = -1
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

# ------------------------------------------------- latch: 8x8 tiles/bitmaps -
def _latch_raw(latch_path, hdr):
    raw = open(latch_path, 'rb').read()
    size = hdr['LatchPlaneSize'] * 4
    if hdr['Compressed']:
        raw = lzw_decompress(raw[6:], size)
    else:
        raw = raw[:size]
    return raw

def load_latch_tiles8(latch_path, hdr):
    """256 8x8 tiles (the font + HUD chars), planar 4bpp -> pixel rows."""
    raw = _latch_raw(latch_path, hdr)
    ps = hdr['LatchPlaneSize']
    base = hdr['Off8Tiles']
    out = []
    for t in range(hdr['Num8Tiles']):
        px = [[0]*8 for _ in range(8)]
        for p in range(4):
            for y in range(8):
                b = raw[base + p*ps + t*8 + y]
                for x in range(8):
                    if b & (0x80 >> x): px[y][x] |= 1 << p
        out.append(px)
    return out

def load_bitmaps(latch_path, hdr):
    """EGALATCH bitmaps -> {name: pixel rows}. Each is planar, one plane
    after another (w/8*h bytes each) relative to OffBitmaps."""
    raw = _latch_raw(latch_path, hdr)
    ps = hdr['LatchPlaneSize']
    d = hdr['raw']
    o = hdr['OffBitmapTable']
    out = {}
    for i in range(hdr['NumBitmaps']):
        w8, h = struct.unpack_from('<HH', d, o + i*16)[0:2]
        loc, = struct.unpack_from('<L', d, o + i*16 + 4)
        name = d[o+i*16+8:o+i*16+16].split(b'\0')[0].decode('ascii')
        w = w8 * 8
        px = [[0]*w for _ in range(h)]
        for p in range(4):
            base = hdr['OffBitmaps'] + p*ps + loc
            for y in range(h):
                for xb in range(w8):
                    b = raw[base + y*w8 + xb]
                    for k in range(8):
                        if b & (0x80 >> k): px[y][xb*8+k] |= 1 << p
        out[name] = px
    return out

# --------------------------------------------------------- finale screens --
def load_finale(path):
    """FINALE.CK1 / PREVIEW2.CK1 / PREVIEW3.CK1: RLE-compressed planar
    320x200 EGA image (planes 1,2,4,8 in order, each 8000 bytes)."""
    d = open(path, 'rb').read()
    plane_len = struct.unpack_from('<L', d, 0)[0] * 2     # in bits
    pos = 4
    bits = []
    def emit(byte):
        for k in range(8): bits.append((byte >> (7-k)) & 1)
    total = plane_len * 4
    while len(bits) < total and pos < len(d):
        c = d[pos]; pos += 1
        if c & 0x80:
            n = (c & 0x7f) + 1
            for _ in range(n):
                emit(d[pos]); pos += 1
        else:
            n = c + 3
            v = d[pos]; pos += 1
            for _ in range(n): emit(v)
    W, H = 320, 200
    img = [[0]*W for _ in range(H)]
    for p in range(4):
        for i in range(plane_len):
            if i + p*plane_len >= len(bits): break
            if i >= W*H: break
            if bits[i + p*plane_len]:
                img[i // W][i % W] |= 1 << p
    return img

# --------------------------------------------------------------- unlzexe ---
def exe_image(path):
    sys_path = os.path.dirname(os.path.abspath(__file__))
    import importlib.util
    spec = importlib.util.spec_from_file_location('unlzexe', os.path.join(sys_path, 'unlzexe.py'))
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    return m.unpack(path)[1]

def exe_tileinfo(img, off=0x130F8, n=611):
    """CG's Vorticon tile-info block: 6 planes of n words:
    anim, behaviour, bup, bright, bdown, bleft."""
    w = struct.unpack_from('<%dh' % (6*n), img, off)
    return [dict(anim=w[i], beh=w[n+i], bup=w[2*n+i], bright=w[3*n+i],
                 bdown=w[4*n+i], bleft=w[5*n+i]) for i in range(n)]


def exe_string(img, start, end, off=-512):
    """CG-compatible string extraction: 0x0A may be followed by a 0 pad byte"""
    out = bytearray(); pos = start + off; end += off
    while pos < end:
        while img[pos] == 0x0A:
            out.append(0x0A); pos += 1
            if img[pos] == 0: pos += 1
        if img[pos] == 0: break
        out.append(img[pos]); pos += 1
    return out.decode('latin-1')
