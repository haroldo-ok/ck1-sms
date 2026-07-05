#!/usr/bin/env python3
"""Minimal headless SMS around the `z80` pip package.
Boots keen.sms, scripts controller input, dumps PNG screenshots."""
import sys, os
import z80
from PIL import Image

ROM_PATH = sys.argv[1] if len(sys.argv) > 1 else 'keen.sms'
OUT = 'sim'
os.makedirs(OUT, exist_ok=True)
rom = open(ROM_PATH, 'rb').read()
NBANK = len(rom) // 0x4000

m = z80.Z80Machine()
m.set_memory_block(0x0000, rom[0x0000:0x8000])        # slots 0+1 fixed
m.set_memory_block(0x8000, rom[0x8000:0xC000])        # slot 2 = bank 2
m.set_memory_block(0xC000, bytes(0x4000))             # RAM + mirror

cur_bank2 = [2]

# ------------------------------------------------------------------- VDP ----
vram = bytearray(0x4000)
cram = bytearray(32)
vaddr = [0]; vcode = [0]; vlatch = [None]; vbuf = [0]
vreg = bytearray(16)
status = [0]

def vdp_ctrl_write(v):
    if vlatch[0] is None:
        vlatch[0] = v
    else:
        code = v >> 6
        vcode[0] = code
        vaddr[0] = ((v & 0x3F) << 8) | vlatch[0]
        vlatch[0] = None
        if code == 2:
            vreg[v & 0x0F] = vaddr[0] & 0xFF
        elif code == 0:
            vbuf[0] = vram[vaddr[0] & 0x3FFF]
            vaddr[0] = (vaddr[0] + 1) & 0x3FFF

def vdp_data_write(v):
    vlatch[0] = None
    if vcode[0] == 3:
        cram[vaddr[0] & 0x1F] = v
    else:
        vram[vaddr[0] & 0x3FFF] = v
    vaddr[0] = (vaddr[0] + 1) & 0x3FFF

def vdp_data_read():
    vlatch[0] = None
    r = vbuf[0]
    vbuf[0] = vram[vaddr[0] & 0x3FFF]
    vaddr[0] = (vaddr[0] + 1) & 0x3FFF
    return r

# --------------------------------------------------------------- IO hooks ---
pad = [0xFF]         # port DC value (active-low)
# NTSC VCounter sequence: 0x00..0xDA then 0xD5..0xFF (262 lines); advance per read
VSEQ = list(range(0x00, 0xDB)) + list(range(0xD5, 0x100))
vidx = [0]

def read_vcounter():
    v = VSEQ[vidx[0]]
    vidx[0] = (vidx[0] + 1) % len(VSEQ)
    return v

def on_in(addr16):
    p = addr16 & 0xFF
    if p == 0xBF or p == 0xBD:
        r = status[0]; status[0] = 0; vlatch[0] = None
        return r
    if p == 0xBE:
        return vdp_data_read()
    if p == 0x7E:
        return read_vcounter()
    if p == 0xDC or p == 0xC0:
        return pad[0]
    if p == 0xDD or p == 0xC1:
        return 0xFF
    return 0xFF

def on_out(addr16, v):
    p = addr16 & 0xFF
    if p == 0xBE: vdp_data_write(v)
    elif p == 0xBF or p == 0xBD: vdp_ctrl_write(v)
    # PSG (0x7F) etc: ignored

wr_count = [0]
def on_write(addr, v):
    wr_count[0] += 1
    if addr >= 0xC000:
        b = bytes([v])
        base = 0xC000 + ((addr - 0xC000) & 0x1FFF)
        m.set_memory_block(base, b)
        m.set_memory_block(base + 0x2000, b)
        if addr >= 0xFFFC:                       # SEGA mapper
            if addr == 0xFFFF:
                bank = v % NBANK
                if bank != cur_bank2[0]:
                    cur_bank2[0] = bank
                    m.set_memory_block(0x8000, rom[bank*0x4000:(bank+1)*0x4000])
            # (slot0/1 remaps unused by devkitSMS crt0)
    # writes below 0xC000: ROM, ignore

m.set_input_callback(on_in)
m.set_output_callback(on_out)
m.set_write_callback(on_write)

# ------------------------------------------------------------ frame runner --
TICKS_PER_FRAME = 59736
frame_no = [0]

IRQ_SLICES = 8      # sub-frame granularity for IRQ-line sampling

def try_irq():
    """VDP keeps /INT asserted while (status&0x80) && IE; the CPU takes it
    as soon as interrupts are enabled. Retry until the ISR's status read
    clears the flag (mirrors real hardware, unlike one-shot delivery)."""
    if (status[0] & 0x80) and (vreg[1] & 0x20):
        m.on_handle_active_int()

def run_frame():
    step = TICKS_PER_FRAME // IRQ_SLICES
    for s in range(IRQ_SLICES):
        m.ticks_to_stop = step
        m.run()
        try_irq()               # pending line from a previous boundary
    status[0] |= 0x80           # vblank reached: raise the line
    try_irq()
    frame_no[0] += 1

# ------------------------------------------------------------- screenshot ---
def sms_rgb(c):
    return ((c & 3) * 85, ((c >> 2) & 3) * 85, ((c >> 4) & 3) * 85)

def tile_pix(tile, row):
    o = tile * 32 + row * 4
    p0, p1, p2, p3 = vram[o], vram[o+1], vram[o+2], vram[o+3]
    out = []
    for x in range(8):
        b = 7 - x
        out.append(((p0>>b)&1) | (((p1>>b)&1)<<1) | (((p2>>b)&1)<<2) | (((p3>>b)&1)<<3))
    return out

def screenshot(name):
    img = Image.new('RGB', (256, 192))
    px = img.load()
    nt = (vreg[2] & 0x0E) << 10
    sx = vreg[8]; sy = vreg[9]
    leftblank = vreg[0] & 0x20
    for y in range(192):
        vy = (y + sy) % 224
        trow = vy >> 3
        for x in range(256):
            vx = (x - sx) & 0xFF
            tcol = vx >> 3
            e = nt + trow*64 + tcol*2
            w = vram[e] | (vram[e+1] << 8)
            tile = w & 0x1FF
            pal = 16 if (w & 0x800) else 0
            hf = w & 0x200; vf = w & 0x400
            rr = (vy & 7) ^ (7 if vf else 0)
            cc = (vx & 7) ^ (7 if hf else 0)
            ci = tile_pix(tile, rr)[cc]
            px[x, y] = sms_rgb(cram[pal + ci])
    # sprites (no per-line limit, fine for screenshots)
    sat = (vreg[5] & 0x7E) << 7
    tilebase = 256 if (vreg[6] & 4) else 0
    for s in range(64):
        yy = vram[sat + s]
        if yy == 0xD0: break
        xx = vram[sat + 0x80 + s*2]
        tt = vram[sat + 0x80 + s*2 + 1] + tilebase
        for r in range(8):
            oy = yy + 1 + r
            if oy >= 192: continue
            rowp = tile_pix(tt, r)
            for c in range(8):
                ox = xx + c
                if ox >= 256: continue
                ci = rowp[c]
                if ci: px[ox, oy] = sms_rgb(cram[16 + ci])
    if leftblank:
        for y in range(192):
            for x in range(8):
                px[x, y] = (0, 0, 0)
    img.save(os.path.join(OUT, name))
    print('saved', name, 'frame', frame_no[0], 'bank', cur_bank2[0],
          'scroll', vreg[8], vreg[9])

# --------------------------------------------------------------- scenario ---
BTN_UP, BTN_DN, BTN_L, BTN_R, BTN_1, BTN_2 = 1, 2, 4, 8, 16, 32
def set_pad(*btns):
    v = 0xFF
    for b in btns: v &= ~b
    pad[0] = v

def frames(n, *btns):
    set_pad(*btns)
    for _ in range(n): run_frame()

print('booting...')
frames(30)
screenshot('01_boot.png')
frames(90)
screenshot('02_title.png')

print('press button 1 (start)...')
frames(3, BTN_1)
frames(180)                       # ENTER interstitial? no: straight to OW load
screenshot('03_overworld.png')

print('walk right on overworld...')
frames(90, BTN_R)
screenshot('04_ow_right.png')
print('walk down...')
frames(40, BTN_DN)
screenshot('05_ow_down.png')
print('walk right+down toward level 1 marker...')
frames(60, BTN_R, BTN_DN)
screenshot('06_ow_seek.png')

print('mash button 1 to try entering a level...')
for i in range(8):
    frames(2, BTN_1)
    frames(10)
frames(140)
screenshot('07_after_enter.png')
frames(120)
screenshot('08_level_maybe.png')

print('hold right in level...')
frames(120, BTN_R)
screenshot('09_level_right.png')
frames(3, BTN_1)          # jump
frames(30, BTN_R)
screenshot('10_level_jump.png')
print('total memory writes:', wr_count[0])
