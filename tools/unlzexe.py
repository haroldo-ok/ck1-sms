#!/usr/bin/env python3
"""Minimal LZEXE 0.91 unpacker (KEEN1.EXE is LZEXE-packed).
Returns the flat decompressed load image (without the MZ header)."""
import struct, sys

def _try(d, start):
    pos = start
    out = bytearray()
    def word():
        nonlocal pos
        v = d[pos] | (d[pos+1] << 8); pos += 2; return v
    bitbuf = word(); cnt = 16
    def bit():
        nonlocal bitbuf, cnt
        b = bitbuf & 1
        cnt -= 1
        if cnt == 0:
            bitbuf = word(); cnt = 16
        else:
            bitbuf >>= 1
        return b
    while True:
        if pos >= len(d): return None
        if bit():
            out.append(d[pos]); pos += 1; continue
        if not bit():
            ln = bit() << 1; ln |= bit(); ln += 2
            span = d[pos] | 0xff00; pos += 1
        else:
            span = d[pos]; ln = d[pos+1]; pos += 2
            span |= ((ln & ~7) << 5) | 0xe000
            ln = (ln & 7) + 2
            if ln == 2:
                ln = d[pos]; pos += 1
                if ln == 0: break
                if ln == 1: continue
                ln += 1
        span -= 0x10000
        if -span > len(out): return None
        for _ in range(ln):
            out.append(out[-(-span)])
    return bytes(out)

def unpack(path):
    d = open(path, 'rb').read()
    hdrparas = struct.unpack_from('<H', d, 8)[0]
    assert d[0x1c:0x20] == b'LZ91', 'not LZEXE 0.91'
    cs = struct.unpack_from('<H', d, 0x16)[0]
    best = None
    for p in range(max(0, (hdrparas) * 16 - 64), (hdrparas + cs) * 16 + 16, 16):
        try:
            r = _try(d, p)
        except IndexError:
            continue
        if r and (best is None or len(r) > len(best[1])):
            best = (p, r)
    return best

if __name__ == '__main__':
    p, img = unpack(sys.argv[1])
    print('start', hex(p), 'size', hex(len(img)))
    open(sys.argv[2], 'wb').write(img)
