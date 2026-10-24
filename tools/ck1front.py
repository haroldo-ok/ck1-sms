"""Front-end data for the SMS port, all converted from the original files:
text pages (STORYTXT / HELPTEXT / PREVIEWS / ENDTEXT + the ordering text and
menu strings in KEEN1.EXE), the 8x8 tile set the original draws text with,
the logo / game-over / high-score / finale / preview screens as de-duplicated
tile images, and the default high-score table from SCORES.CK1."""
import os, re, struct
import ck1lib

COLS = 30                       # text columns on a 32-column SMS screen

# ---------------------------------------------------------------- helpers --
def tile_planar(t8):
    out = bytearray()
    for y in range(8):
        b = [0, 0, 0, 0]
        for x in range(8):
            v = t8[y][x]
            for p in range(4):
                if v & (1 << p): b[p] |= 0x80 >> x
        out += bytes(b)
    return bytes(out)

def carr(f, name, data, per=24):
    data = bytes(data) or b'\0'
    f.write('const unsigned char %s[%d] = {\n' % (name, len(data)))
    for i in range(0, len(data), per):
        f.write('  ' + ','.join(str(b) for b in data[i:i+per]) + ',\n')
    f.write('};\n')

def warr(f, name, data, per=16):
    f.write('const unsigned int %s[%d] = {\n' % (name, len(data)))
    for i in range(0, len(data), per):
        f.write('  ' + ','.join(str(w) for w in data[i:i+per]) + ',\n')
    f.write('};\n')

# ------------------------------------------------------------------ text ---
def _prose(line):
    return bool(line and line[0] not in b' ~' and b'   ' not in line
                and all(32 <= c < 127 for c in line))

def _wrap(words, width=COLS, indent=0):
    out, cur = [], b''
    for w in words:
        if not cur: cur = w
        elif len(cur) + 1 + len(w) <= width: cur += b' ' + w
        else: out.append(cur); cur = b' ' * indent + w
    if cur: out.append(cur)
    return out

def _squeeze(line):
    """shrink an over-wide pre-formatted line by eating its long space runs"""
    line = bytearray(line)
    while len(line) > COLS:
        runs = [(m.start(), m.end()) for m in re.finditer(rb' {2,}', bytes(line))
                if m.start() > 0 or m.end() - m.start() >= 8]
        if not runs: break
        s, e = max(runs, key=lambda r: r[1] - r[0])
        del line[s]
    while len(line) > COLS and line[:1] == b' ':
        del line[0]
    return bytes(line)

def format_text(raw):
    """CTextViewer's rules, re-flowed for 30 columns.  Returns rows of
    exactly COLS tile codes (code == 8x8 tile index in the original set);
    '~' lines become the red-on-grey headings (glyph + 128)."""
    raw = raw.split(b'\x1a')[0]
    lines = [l.rstrip(b'\r').replace(b'\x1f', b' ') for l in raw.split(b'\n')]
    rows, i = [], 0
    while i < len(lines):
        l = lines[i]
        if l.startswith(b'~'):
            t = bytes(c + 128 if c < 128 else c for c in l[1:].rstrip())
            t = _squeeze(t) if len(t) > COLS else t
            rows.append(t + bytes([32 + 128]) * (COLS - len(t)))
            i += 1
        elif _prose(l):
            words = []
            while i < len(lines) and _prose(lines[i].rstrip(b'\r')):
                words += lines[i].split(); i += 1
            rows += [r for r in _wrap(words)]
        else:
            l = l.rstrip()
            if len(l) > COLS:
                s = _squeeze(l)
                if len(s) > COLS:
                    rows += _wrap(l.split(), COLS, 2); i += 1; continue
                l = s
            rows.append(l); i += 1
    # fixed-width rows
    out = []
    for r in rows:
        r = r[:COLS]
        out.append(r + bytes([32]) * (COLS - len(r)) if len(r) < COLS else r)
    return out

def sms_help(raw):
    """HELPTEXT with the PC key names swapped for the SMS pad (the only
    place the original text had to change)."""
    keys_old = raw[raw.index(b'KEY           WHAT IT DOES'):
                   raw.index(b'When you are on the Mars map')]
    keys_new = (b'KEY         WHAT IT DOES\r\n\r\n'
                b'Pad         Move Keen around\r\n\r\n'
                b'Button 1    Jump\r\n\r\n'
                b'Button 2    Use Pogo/Stop Pogo\r\n'
                b'            (if you have one)\r\n\r\n'
                b'1 AND 2     Fire raygun\r\n'
                b'            (if it has charges)\r\n\r\n\r\n'
                b'OTHER IMPORTANT KEYS\r\n\r\n'
                b'Pause       Status screen\r\n'
                b'            (on the map: also\r\n'
                b'            Save Game)\r\n\r\n'
                b'Controlling Keen while using the pogo is more difficult.\r\n\r\n')
    raw = raw.replace(keys_old, keys_new)
    raw = raw.replace(b'(press Ctrl or button 1)', b'(walk onto it)')
    raw = raw.replace(b'You can move Commander Keen with the joystick or keyboard.',
                      b'You move Commander Keen with the control pad.')
    raw = raw.replace(b'When you first start Keen, you will be in keyboard mode. '
                      b"Let's look at the\r\nkeys you need to know to play the game.",
                      b"Let's look at the buttons you need to know to play the game.")
    return raw

def order_text(img):
    """the ordering-info screen: the strings stored in KEEN1.EXE"""
    a = img.index(b'Commander Keen: Invasion')
    b = img.index(b'Or order toll free: 1-800-852-5659') + 40
    blob = bytes(img[a-0:b])
    parts = [p for p in blob.split(b'\0') if p]
    full = b''.join(parts).replace(b'\r', b'')
    assert b'Garland, TX 75047' in full and b'$30' in full
    # hand-flowed for 30 columns from those same strings
    L = [b'~         ORDERING INFO', b'',
         b'Commander Keen: Invasion of the Vorticons consists of three unique and challenging episodes:',
         b'',
         b'1. Marooned on Mars   $15',
         b'2. The Earth Explodes $15',
         b'3. Keen Must Die!     $15', b'',
         b'Order the trilogy for $30 and you get',
         b'* The "Secret Hints & Tricks" sheet',
         b'* The special "cheat mode" password',
         b'* The latest version of each game',
         b'* SEVERAL FREE BONUS GAMES!', b'',
         b'Mail orders to:',
         b'Apogee Software', b'P.O. Box 476389', b'Garland, TX 75047',
         b'(U.S. funds only checks or M/O\'s include $2 P&H)', b'',
         b'Specify 5.25/3.5 disk size when ordering', b'',
         b'Or order toll free:', b'1-800-852-5659']
    for s in (b'Marooned on Mars', b'The Earth Explodes', b'Keen Must Die!',
              b'SEVERAL FREE BONUS GAMES!', b'Apogee Software',
              b'P.O. Box 476389', b'1-800-852-5659',
              b'Specify 5.25/3.5 disk size when ordering'):
        assert s in full, s
    return b'\n'.join(L) + b'\n'

# ---------------------------------------------------------------- images ---
def canvas(w=256, h=192, c=0):
    return [[c] * w for _ in range(h)]

def blit(cv, bm, x, y):
    for r, row in enumerate(bm):
        for c, v in enumerate(row):
            if 0 <= y + r < len(cv) and 0 <= x + c < len(cv[0]):
                cv[y + r][x + c] = v

def to_image(cv, maxtiles=440):
    """cut a canvas into 8x8 tiles, drop duplicates -> (tiles, map words)"""
    H, W = len(cv), len(cv[0])
    tiles, cache, mp = [], {}, []
    for ty in range(H // 8):
        for tx in range(W // 8):
            t = tuple(tuple(cv[ty*8+r][tx*8+c] & 15 for c in range(8)) for r in range(8))
            if t not in cache:
                cache[t] = len(tiles); tiles.append(t)
            mp.append(cache[t])
    assert len(tiles) <= maxtiles, 'image needs %d tiles' % len(tiles)
    return b''.join(tile_planar(t) for t in tiles), mp, W // 8, H // 8

def crop(img, x0, y0, w=256, h=192):
    return [row[x0:x0+w] for row in img[y0:y0+h]]

def centre(cv, bm, y):
    blit(cv, bm, (len(cv[0]) - len(bm[0])) // 2, y)

# ------------------------------------------------------------------ build --
def build(CK, hdr, EXE, T8, TILES, first_bank, bank_cap, title_cv):
    """-> (banks {n: c source}, header text, game_data text, bank list)"""
    BM = ck1lib.load_bitmaps(CK + '/EGALATCH.CK1', hdr)
    out = {}
    nb = [first_bank]
    def newbank():
        b = nb[0]; nb[0] += 1; out[b] = {'size': 0, 'parts': []}; return b
    cur = [None]
    def put(size, emit):
        if cur[0] is None or out[cur[0]]['size'] + size > bank_cap:
            cur[0] = newbank()
        out[cur[0]]['size'] += size
        out[cur[0]]['parts'].append(emit)
        return cur[0]

    hdrtxt, datatxt = [], []

    # --- 8x8 original tile set + grey-background text font
    t8 = b''.join(tile_planar(T8[i]) for i in range(256))
    grey = b''.join(tile_planar([[(7 if v == 15 else v) for v in row] for row in T8[i]])
                    for i in range(32, 128))
    bank_t8 = put(len(t8), lambda f: carr(f, 'fe_t8', t8))
    bank_grey = put(len(grey), lambda f: carr(f, 'fe_font_grey', grey))
    hdrtxt.append('extern const unsigned char fe_t8[], fe_font_grey[];')

    # --- text pages
    pages = []
    def rd(n): return open(os.path.join(CK, n), 'rb').read()
    pages.append(('STORY',   format_text(rd('STORYTXT.CK1'))))
    pages.append(('HELP',    format_text(sms_help(rd('HELPTEXT.CK1')))))
    pages.append(('PREVIEW', format_text(rd('PREVIEWS.CK1'))))
    pages.append(('ENDTEXT', format_text(rd('ENDTEXT.CK1'))))
    pages.append(('ORDER',   format_text(order_text(EXE))))
    menu = [b'New Game', b'Continue Game', b'Story', b'About ID...',
            b'High Scores', b'Ordering Info', b'Previews!']
    for m in menu: assert m in bytes(EXE), m
    pages.append(('MENU', [m + b' ' * (COLS - len(m)) for m in menu]))
    txtdesc = []
    for name, rows in pages:
        blob = b''.join(rows)
        b = put(len(blob), (lambda n, bl: lambda f: carr(f, 'fe_pg_' + n.lower(), bl))(name, blob))
        txtdesc.append((name, b, len(rows)))
        hdrtxt.append('extern const unsigned char fe_pg_%s[];' % name.lower())
        hdrtxt.append('#define PG_%s %d' % (name, len(txtdesc) - 1))
        print('text %-8s %3d rows' % (name, len(rows)))

    # --- default high scores (SCORES.CK1): 8 x { name[8], score u32, parts }
    sc = rd('SCORES.CK1')
    scores = struct.unpack_from('<8L', sc, 0)
    names = [sc[112 + 13*i: 112 + 13*i + 13].split(b'\0')[0] for i in range(7)] + [b'']
    sblob = b''
    for i in range(8):
        nm = names[i][:8]
        sblob += nm + b'\0' * (8 - len(nm)) + struct.pack('<L', scores[i]) + b'\0'
    put(len(sblob), lambda f: carr(f, 'fe_scores_default', sblob))
    hdrtxt.append('extern const unsigned char fe_scores_default[];')
    sdb = cur[0]

    # --- images
    imgs = []
    def addimg(name, cv):
        t, mp, w, h = to_image(cv)
        n = len(t) // 32
        b = put(len(t) + 2 * len(mp),
                (lambda nm, t, mp: lambda f: (carr(f, 'fe_it_' + nm, t), warr(f, 'fe_im_' + nm, mp)))(name.lower(), t, mp))
        imgs.append((name, b, n, w, h))
        hdrtxt.append('extern const unsigned char fe_it_%s[];\nextern const unsigned int fe_im_%s[];' % (name.lower(), name.lower()))
        hdrtxt.append('#define IMG_%s %d' % (name, len(imgs) - 1))
        print('image %-8s %3d tiles  %dx%d' % (name, n, w, h))

    addimg('TITLE', title_cv)
    cv = canvas(); centre(cv, BM['AN'], 28); centre(cv, BM['APOGEE'], 60)
    centre(cv, BM['PRESENT'], 108); addimg('LOGO1', cv)
    cv = canvas(); centre(cv, BM['AN'], 28); centre(cv, BM['IDSOFT'], 56)
    centre(cv, BM['PRODUCT'], 116); addimg('LOGO2', cv)
    cv = canvas(); centre(cv, BM['IDLOGO'], 76); addimg('IDABOUT', cv)
    cv = canvas(); centre(cv, BM['GAMEOVER'], 84); addimg('GAMEOVER', cv)
    # high-score backdrop: the id-frame screen of LEVEL90 (left 8 + right 8 tiles)
    lv = ck1lib.load_level(CK + '/LEVEL90.CK1')
    cv = canvas()
    for ty in range(12):
        for k in range(16):
            mx = 84 + k if k < 8 else 96 + (k - 8)
            t = lv['tiles'][(2 + ty) * lv['W'] + mx]
            for y in range(16):
                for x in range(16):
                    cv[ty*16+y][k*16+x] = TILES[t][y][x] & 15
    for y in range(16, 32):
        for x in range(32, 224): cv[y][x] = 7
    centre(cv, BM['HIGHSCOR'], 0)
    blit(cv, BM['NAME'],  32, 32)
    blit(cv, BM['SCORE'], 96, 32)
    blit(cv, BM['PARTS'], 160, 32)
    addimg('HISCORE', cv)
    # screens stored as 320x200 EGA pictures: centred 256x192 window
    for nm, fn, y0 in (('FINALE', 'FINALE.CK1', 0), ('PREVIEW2', 'PREVIEW2.CK1', 4),
                       ('PREVIEW3', 'PREVIEW3.CK1', 4)):
        im = ck1lib.load_finale(os.path.join(CK, fn))
        addimg(nm, crop(im, 32, y0))
    # bitmaps drawn on top of the finale picture (kept as small tile sets)
    for nm in ('WINDON', 'WINDOFF', 'KEENSHIP', 'ONEMOMEN'):
        bm = BM[nm]
        cv = canvas(len(bm[0]), len(bm))
        blit(cv, bm, 0, 0)
        addimg(nm, cv)

    banks = {}
    for b, d in out.items():
        import io
        f = io.StringIO()
        f.write('/* generated: front-end data bank %d */\n' % b)
        for e in d['parts']: e(f)
        banks[b] = f.getvalue()

    hdrtxt.append('typedef struct { unsigned char bank; const unsigned char *rows; unsigned int nrows; } FeTxt;')
    hdrtxt.append('typedef struct { unsigned char bank; const unsigned char *tiles; unsigned int ntiles; '
                  'const unsigned int *map; unsigned char w, h; } FeImg;')
    hdrtxt.append('extern const FeTxt fe_txt[%d];\nextern const FeImg fe_img[%d];' % (len(txtdesc), len(imgs)))
    hdrtxt.append('#define BANK_FE_T8 %d\n#define BANK_FE_GREY %d\n#define BANK_FE_SCORES %d' % (bank_t8, bank_grey, sdb))
    d = ['const FeTxt fe_txt[%d] = {' % len(txtdesc)]
    for name, b, n in txtdesc:
        d.append('  { %d, fe_pg_%s, %d },' % (b, name.lower(), n))
    d.append('};')
    d.append('const FeImg fe_img[%d] = {' % len(imgs))
    for name, b, n, w, h in imgs:
        d.append('  { %d, fe_it_%s, %d, fe_im_%s, %d, %d },' % (b, name.lower(), n, name.lower(), w, h))
    d.append('};')
    return banks, '\n'.join(hdrtxt) + '\n', '\n'.join(d) + '\n', sorted(banks)
