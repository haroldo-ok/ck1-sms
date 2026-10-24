"""UI data for the SMS port, converted from the original files: dialog
fonts/frame (EGALATCH 8x8 tiles), status-screen icons (16x16 tiles) and the
text strings stored inside KEEN1.EXE."""
import struct
import ck1lib

# strings: (name, exe start, exe end, offset) -- same ranges Commander Genius
# uses for the 1.31 executable
EXE_STRINGS = [
    ('HINT2',  0x15280, 0x15314), ('HINT6',  0x1531A, 0x153B2),
    ('HINT9',  0x153B4, 0x1544C), ('HINT10', 0x1544E, 0x154E6),
    ('HINT11', 0x154E8, 0x1553E), ('HINT12', 0x15540, 0x155D9),
    ('HINT15', 0x155DB, 0x1565D),
    ('SHIP',   0x1679B, 0x167D3), ('LIVES',  0x160D8, 0x160E5),
    ('STATUS', 0x15052, 0x150F5), ('CHEAT',  0x14DEE, 0x14E51),
    ('E1', 0x15E24, 0x15EB0), ('E2', 0x15EBD, 0x15EFA),
    ('E3', 0x15EFB, 0x15F2F), ('E4', 0x15F30, 0x15F7F),
    ('E5', 0x15F80, 0x15F9B), ('E6', 0x15F9C, 0x15FE1),
    ('E7', 0x15FE2, 0x15FF5), ('E8', 0x15FF6, 0x16006),
    ('E9', 0x16007, 0x16071), ('TBC', 0x16072, 0x16085),
]

def exe_strings(img):
    out = {}
    for n, a, b in EXE_STRINGS:
        off = 0 if n == 'CHEAT' else -512
        out[n] = ck1lib.exe_string(img, a, b, off)
        if n == 'E1':                       # 35 columns wide: re-flow for 32
            words = out[n].replace('-\n', '-').replace('\n', ' ').split()
            lines, cur = [], ''
            for w in words:
                if cur and len(cur) + 1 + len(w) > 26: lines.append(cur); cur = w
                else: cur = (cur + ' ' + w) if cur else w
            out[n] = '\n'.join(lines + [cur])
        if n == 'E4' and out[n].endswith('\nA'): out[n] = out[n][:-2]
    return out

def exe_word(img, o): return struct.unpack_from('<H', img, o)[0]

def constants(img):
    return dict(raygun_tile=exe_word(img, 0x1166), pogo_tile=exe_word(img, 0x11A7),
                one_eyed_tile=exe_word(img, 0x4547),
                vort_hp=img[0x180D] + 1, commander_hp=img[0x1819] + 1)
