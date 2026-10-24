/*
 * Commander Keen (HTML5-Keen) -> Sega Master System port
 * devkitSMS / SDCC / SMSlib
 *
 * Engine overview
 * ---------------
 * World       : 16x16-pixel metatiles = 2x2 SMS tiles. Per-level metatile
 *               definitions + per-metatile collision flags (solid / one-way
 *               platform / deadly).
 * Rendering   : 8-way free scroll over the 32x28 VDP name table used as a
 *               ring buffer in both axes.  When the camera crosses an 8-px
 *               boundary the entering tile column/row strip is queued and
 *               written right after the next VBlank (after scroll regs).
 * Banking     : SEGA mapper.  Bank 2 = sprite art (streamed to fixed VRAM
 *               slots each frame), bank 3 = title screen, banks 4..8 =
 *               per-level tiles/maps.  Map + metatile defs are read
 *               directly from the mapped bank; collision flags are copied
 *               to RAM at level load; collected items are handled with a
 *               small RAM override list (flags never change on pickup).
 * Sprites     : Keen is 16x24 = 6 hardware sprites, frame streamed from ROM
 *               every VBlank.  Up to 3 on-screen yorps share 3 streaming
 *               slots that are acquired/released dynamically.
 */

#include "SMSlib.h"
#include "game_data.h"
#include "engine.h"

__sfr __at 0x7F PSGPort;

/* slot-2 bank shadow: code that temporarily maps another bank (sound data
   fetches) restores whatever the main code had mapped */
unsigned char b2_shadow;
static void map2(unsigned char b) { b2_shadow = b; SMS_mapROMBank(b); }

/* ---------------------------------------------------------------- consts -- */

/* VRAM sprite tile slots (absolute tile numbers; pass n-256 to addSprite) */
#define VT_PLAYER   256            /* 6 tiles, streamed every frame          */
#define VT_SLOT0    262            /* 7 x 12 tiles, entity streaming slots   */
#define VT_BULLET   346            /* 12 tiles, loaded once per level        */
#define VT_ERAY     358            /* 2 tiles, enemy ray                     */
#define VT_CHUNK    360            /* 4 tiles, ice chunk                     */
#define VT_SHIP     364            /* 8 tiles, ending ship + ?/! marks        */
#define VT_OWKEEN   444            /* 4 tiles, overworld player              */
#define N_SLOTS     7

/* physics, 8.8 fixed point (units: 1/256 px per frame) */
/* values are Commander Genius' vorticon-engine constants (px/frame * 32 =
   its units; here 8.8 so units * 8): max jump 120, up-decel 4, fall accel
   8 after the apex, terminal 104, walk 68, air steer 3 per frame */
#define WALK_ACC     96
#define WALK_MAX    544
#define AIR_ACC      24
#define GRAV_UP      32     /* rising deceleration                      */
#define GRAV_DN      64     /* acceleration after the apex              */
#define GRAV_WALKOFF 32     /* walking off a ledge: 1px/f start + 4*8   */
#define JUMP_V     (-960)   /* full-height jump                         */
#define POGO_LO_V  (-960)   /* low pogo bounce = a normal jump          */
#define POGO_HI_V (-1450)   /* held-jump pogo (1.1 * 165 * 8)           */
#define FALL_MAX    832
#define POGO_STEER   77
#define BULLET_V    640

#define MAX_ENT     30
#define MAX_OVR    200
#define MAX_DOORS    8
#define MAX_ENTRIES 48
#define MAX_EXITS    4

/* item kinds (must match tools/convert_ck1.py) */
#define K_PTS100  0
#define K_PTS5000 4                     /* kinds 0..4 = points */
#define K_AMMO    5
#define K_POGO    6
#define K_CARDY   7                     /* 7..10 = keycards Y R G B */
#define K_CARDB  10
#define K_JOYSTICK 11                   /* 11..14 = ship parts */
#define K_FUEL   14

/* entity types (converter codes minus one) */
#define ET_YORP   0
#define ET_GARG   1
#define ET_VORT   2
#define ET_BUTLER 3
#define ET_TANK   4
#define ET_CANNON0 5                    /* 5..8: vectors (1,-1)(0,-1)(0,1)(-1,-1) */

/* keen sprite-sheet frame ids (16x24 frames, 6 per row) */
#define KF_STAND_R  0
#define KF_WALK_R   1     /* 1..3 */
#define KF_STAND_L  4
#define KF_WALK_L   5     /* 5..7 */
#define KF_FALL_R   13
#define KF_FALL_L   19
#define KF_SHOOT_R  20
#define KF_SHOOT_L  21
#define KF_DIE0     22
#define KF_DIE1     23
#define KF_POGO_UP_R 24
#define KF_POGO_DN_R 25
#define KF_POGO_UP_L 26
#define KF_POGO_DN_L 27

/* yorp frames */
#define YF_LOOK0    1
#define YF_WALK_L0  4
#define YF_WALK_R0  6
#define YF_CRY0     8
#define YF_DIE      10
#define YF_DEAD     11

#define F_ANY (F_UP | F_CEIL | F_BLKR | F_BLKL)

/* ---------------------------------------------------------------- state --- */
static const LevelDesc *ld;
unsigned char cur_level;                /* 0=overworld 1..3 */
unsigned char game_state;
unsigned char level_done[NLEVELS];
unsigned int  score_hi;          /* score = hi*10000 + lo (BCD-ish)   */
unsigned int  score_lo;

/* inventory */
unsigned char inv_ammo, inv_pogo;
unsigned char inv_keys;          /* bit 0..3 = card Y R G B, per level */
unsigned char inv_parts;                /* bit 0..3 = joystick/battery/vacuum/fuel */
unsigned char lives = 3;
unsigned int next_life = 2;      /* extra Keen every 20000 points (score_hi counts 10000s) */
unsigned char stun_t;            /* frozen-in-ice timer */
unsigned char jprep, jframe, jtimer, pogo_hold, parts_at_entry;
unsigned char hint_used, tele_pending, show_lives;
int ow_px = -1, ow_py;                /* where Keen entered the level (map checkpoint) */
static const unsigned char hint_msg[17] =   /* level -> TXT_HINT* string id */
    { 0, 0, TXT_HINT2, 0, 0, 0, TXT_HINT6, 0, 0, TXT_HINT9, TXT_HINT10,
      TXT_HINT11, TXT_HINT12, 0, 0, TXT_HINT15, 0 };
static const unsigned char walk_seq[4] = { 0, 1, 2, 1 };   /* frames 1,2,3,2 */

/* map access (valid while ld->map_bank mapped) */
static const unsigned char *map_rom;
static const unsigned int  *mtdef_rom;
static unsigned char flags_ram[256];
static unsigned int  mapW, mapH;        /* in metatiles */
static unsigned int  mapPW, mapPH;      /* in pixels    */

/* item overrides: cells whose metatile changed after pickup. The list is
   kept SORTED BY ROW with ovr_row0[r] = index of row r's first entry, so
   a cell lookup scans only its row's few entries instead of the whole
   list -- with 100+ collected items a per-cell full scan made every
   scroll strip cost tens of thousands of cycles (the big-level lag). */
#define MAX_OVR_ROWS 96
static unsigned char ovr_mx[MAX_OVR], ovr_my[MAX_OVR], ovr_mt[MAX_OVR];
static unsigned char ovr_row0[MAX_OVR_ROWS + 1];
static unsigned char n_ovr;

static void ovr_add(unsigned char mx, unsigned char my, unsigned char mt) {
    unsigned char i, pos;
    if (n_ovr >= MAX_OVR || my >= MAX_OVR_ROWS) return;
    pos = ovr_row0[my + 1];                 /* insert at end of my's bucket */
    for (i = n_ovr; i > pos; i--) {         /* shift tail up (rare, small)  */
        ovr_mx[i] = ovr_mx[i - 1];
        ovr_my[i] = ovr_my[i - 1];
        ovr_mt[i] = ovr_mt[i - 1];
    }
    ovr_mx[pos] = mx; ovr_my[pos] = my; ovr_mt[pos] = mt;
    n_ovr++;
    for (i = my + 1; i <= MAX_OVR_ROWS; i++) ovr_row0[i]++;
}

/* items copied to RAM at load */
static unsigned char it_mx[MAX_OVR], it_my[MAX_OVR], it_kind[MAX_OVR],
                     it_restore[MAX_OVR], it_taken[MAX_OVR];
static unsigned char n_items;
static unsigned char it_lo;               /* cached window start (sorted mx) */

/* overworld entries (one per city cell) */
static unsigned char en_mx[MAX_ENTRIES], en_my[MAX_ENTRIES],
                     en_lvl[MAX_ENTRIES], en_done[MAX_ENTRIES];
static unsigned char n_entries;

/* world-map teleporters: src cell -> dest cell (bidirectional pairs) */
#define MAX_TELEPORTS 4
static unsigned char tp_sx[MAX_TELEPORTS], tp_sy[MAX_TELEPORTS],
                     tp_dx[MAX_TELEPORTS], tp_dy[MAX_TELEPORTS];
static unsigned char n_teleports;

/* doors (per level): color 0..3 = Y R G B */
static unsigned char do_mx[MAX_DOORS], do_my[MAX_DOORS], do_col[MAX_DOORS],
                     do_restore[MAX_DOORS], do_open[MAX_DOORS];
static unsigned char n_doors;

/* exit cells */
static unsigned char ex_mx[MAX_EXITS], ex_my[MAX_EXITS];
static unsigned char n_exits;

/* camera */
static unsigned int cam_x, cam_y;
static unsigned int cam_c8, cam_r8;       /* cam_x>>3, cam_y>>3 */
static unsigned int pend_col, pend_row;   /* 0xFFFF = none */
static unsigned int pend_col_camr8;       /* row base captured with column   */

/* player */
int px, py;                             /* world pixel, sprite top-left      */
int vx, vy;                     /* 8.8                               */
static int  sx_acc, sy_acc;             /* subpixel accumulators             */
unsigned char dir_right, on_ground, jumping, pogoing, pogo_squat;
unsigned char shoot_timer, exiting, dying;
static unsigned int  seq_timer;
static unsigned char pframe, cur_pframe, anim_t;
static unsigned char fall_snd_on;

/* bullet */
static unsigned char b_active, b_hit, b_timer, b_right;
static int b_px, b_py;

/* entities */
static unsigned char y_n;
static unsigned char e_type[MAX_ENT];
static int  y_px[MAX_ENT], y_py[MAX_ENT];
static int  y_vx[MAX_ENT], y_vy[MAX_ENT];
static int  y_sx[MAX_ENT], y_sy[MAX_ENT];
static unsigned char  y_state[MAX_ENT];   /* 0 alive 1 stunned 2 dying 3 dead */
static unsigned int   y_t[MAX_ENT];
static unsigned char  y_hop[MAX_ENT];
static unsigned char  y_hp[MAX_ENT];
static unsigned char  y_frame[MAX_ENT];   /* (type<<4)|frame key for streaming */
static unsigned char  y_slot[MAX_ENT];    /* 0..N_SLOTS-1 or 0xFF */
static unsigned char  y_pg[MAX_ENT];      /* y_px>>8: byte prefilter page */
static unsigned char  cam_pg;             /* cam_x>>8, refreshed per frame */
static unsigned char  near_list[MAX_ENT]; /* indices of page-near entities */
static unsigned char  n_near;             /* entries in near_list */
static unsigned char  near_timer;         /* frames since last rebuild */
static unsigned char  near_pg;            /* cam_pg at last rebuild */
static unsigned char  slot_owner[N_SLOTS];
static unsigned char  slot_vframe[N_SLOTS];
static unsigned char  slot_part[N_SLOTS];     /* 12-tile art: 1 = first half loaded */
static unsigned char  slot_pkey[N_SLOTS];

/* projectiles: 2 ice chunks + 2 enemy rays */
static unsigned char pj_on[4];            /* 0 off, 1 chunk, 2 eray */
static int pj_x[4], pj_y[4], pj_vx[4], pj_vy[4];

static unsigned char rng;
static unsigned char frame_ct;               /* free-running frame counter */
static unsigned char scroll_y;          /* cam_y % 224, kept incrementally */
static unsigned char anim_ph;           /* walk anim phase 0..2 */



/* loop-side input edge detection: immune to ISR updates mid-frame
   (SMS_getKeysPressed loses edges whenever a game frame overruns and the
   ISR refreshes its previous/current pair before the loop reads it) */
unsigned int prev_ks;
static unsigned char jump_buf;          /* buffered jump press, frames */
#define JUMP_BUF_FRAMES 5

/* ------------------------------------------------------------------ sfx --- */
/* Plays the ORIGINAL PC-speaker sounds converted from SOUNDS.CK1. Each
   sound is a sequence of PSG tone periods (the PSG clock is exactly 3x
   the PC's timer clock, so periods are the original divisors * 3/32);
   0 = a silent tick, 0xFFFF = end. Values advance at the original rate
   of ~44/s (44 ticks per 60 frames via an accumulator). One channel,
   with the original priority rule: a new sound only replaces the
   current one if its priority is at least as high. */
static const unsigned int *snd_p;
static unsigned char snd_on, snd_pri_cur, snd_acc;

static void psg_tone(unsigned int period, unsigned char att) {
    PSGPort = 0xC0 | (period & 0x0F);        /* ch2 tone latch */
    PSGPort = (period >> 4) & 0x3F;
    PSGPort = 0xD0 | (att & 0x0F);           /* ch2 volume     */
}
static void psg_tone_off(void)  { PSGPort = 0xDF; }
static void psg_noise(unsigned char mode, unsigned char att) {
    PSGPort = 0xE0 | mode;                   /* ch3 noise      */
    PSGPort = 0xF0 | (att & 0x0F);
}
static void psg_noise_off(void) { PSGPort = 0xFF; }

void sfx_play(unsigned char id) {
    unsigned char p = snd_pri[id];
    if (snd_on && p < snd_pri_cur) return;
    snd_p = snd_data + snd_off[id];
    snd_pri_cur = p; snd_on = 1;
    snd_acc = 60;                      /* first value fires immediately */
}

void sfx_update(void) {
    if (!snd_on) return;
    snd_acc += 44;
    while (snd_acc >= 60) {
        unsigned int v;
        snd_acc -= 60;
        { unsigned char ob = b2_shadow;
          SMS_mapROMBank(BANK_SND);
          v = *snd_p++;
          SMS_mapROMBank(ob); }
        if (v == 0xFFFF) {
            psg_tone_off();
            snd_on = 0; snd_pri_cur = 0;
            return;
        }
        if (v == 0) psg_tone_off();
        else        psg_tone(v, 2);
        if (!snd_on) return;
    }
}

/* precomputed my*mapW offsets (mapH <= 69) */
static unsigned int row_off[96];

/* --------------------------------------------------------------- map query - */
static unsigned char cell_mt(unsigned int mx, unsigned int my) {
    unsigned char m = map_rom[row_off[my] + mx];
    unsigned char i, e;
    e = ovr_row0[(unsigned char)my + 1];
    for (i = ovr_row0[(unsigned char)my]; i < e; i++)
        if (ovr_mx[i] == mx) { m = ovr_mt[i]; break; }
    return m;
}

/* flags at world pixel; outside map: sides/top solid, bottom open */
static unsigned char n_open_doors;      /* fast skip when none open */
static unsigned char door_open_at(unsigned char cx, unsigned char cy) {
    unsigned char i;
    for (i = 0; i < n_doors; i++)
        if (do_open[i] && do_mx[i] == cx && do_my[i] == cy) return 1;
    return 0;
}
static unsigned char mflag(int wx, int wy) {
    unsigned int mx, my;
    if (wx < 0 || wy < 0) return F_ANY;
    mx = (unsigned int)wx >> 4;
    my = (unsigned int)wy >> 4;
    if (mx >= mapW) return F_ANY;
    if (my >= mapH) return 0;
    {
        unsigned char f = flags_ram[map_rom[row_off[my] + mx]];
        if ((f & F_ANY) && n_open_doors &&
            door_open_at((unsigned char)mx, (unsigned char)my))
            return 0;
        return f;
    }
}

/* -------------------------------------------------------------- NT strips -- */
/* Scroll strips are split in two: render_* runs during game logic and
   bakes the strip's tile words into a RAM buffer (all the map reads,
   override lookups and metatile math happen there), and blit_* runs in
   the VBlank window doing nothing but address setup + data writes. The
   VBlank section must stay well inside ~16k cycles: the UNSAFE_* OUTI
   bursts (SAT copy, sprite art) silently DROP writes if they spill into
   active display -- that showed up as sprites snapping to the screen
   edge and corrupted sprite frames while scrolling. */
__sfr __at 0xBF VDPCtrlPort;              /* raw ports: the blits below   */
__sfr __at 0xBE VDPDataPort;              /* run in vblank and must be    */
                                          /* fast, not paced              */
static unsigned int  colbuf[28];       /* tile words                     */
static unsigned int  colbuf_a[28];     /* their VRAM addresses           */
static unsigned char colbuf_n, col_ready;
static unsigned int  rowbuf[32];
static unsigned int  rowbuf_a0, rowbuf_a1;  /* run starts (a1 = after NT wrap) */
static unsigned char rowbuf_n1, rowbuf_n, row_ready;

static void render_col(unsigned int wc) {
    unsigned char r, sub;
    unsigned int wr;
    const unsigned char *mrow;
    const unsigned int  *mtsub;
    unsigned char nty;
    unsigned int addr;
    colbuf_n = 0;
    if (wc >= (mapW << 1)) return;
    wr    = pend_col_camr8;
    nty   = (unsigned char)(wr % 28);
    addr  = SMS_PNTAddress | ((((unsigned int)nty << 5) + (wc & 31)) << 1);
    sub   = wc & 1;
    mrow  = map_rom + row_off[wr >> 1] + (wc >> 1);
    mtsub = mtdef_rom + sub + ((wr & 1) << 1);
    for (r = 0; r < 25; r++, wr++) {
        unsigned char m;
        if (wr >= (mapH << 1)) break;
        m = *mrow;
        if (n_ovr) m = cell_mt(wc >> 1, wr >> 1);
        colbuf[r]   = mtsub[(unsigned int)m << 2];
        colbuf_a[r] = addr;
        colbuf_n++;
        if (++nty == 28) { nty = 0; addr -= 28u * 64u; }
        addr += 64;
        if (wr & 1) { mrow += mapW; mtsub -= 2; }
        else        { mtsub += 2; }
    }
    col_ready = 1;
}

static void blit_col(void) {
    /* little-endian int buffers stream in exactly the low-byte-first
       order the VDP ports want */
    const unsigned char *pa = (const unsigned char *)colbuf_a;
    const unsigned char *pt = (const unsigned char *)colbuf;
    unsigned char r;
    for (r = 0; r < colbuf_n; r++) {
        VDPCtrlPort = *pa++;
        VDPCtrlPort = *pa++;
        VDPDataPort = *pt++;
        VDPDataPort = *pt++;
    }
}

static void render_row(unsigned int wr) {
    unsigned char c, sub;
    unsigned int wc;
    const unsigned char *mrow;
    const unsigned int  *mtsub;
    unsigned char ntx0;
    unsigned int rowbase;
    rowbuf_n = 0;
    if (wr >= (mapH << 1)) return;
    rowbase = SMS_PNTAddress | (((unsigned int)(wr % 28)) << 6);
    wc      = cam_c8 + 1;
    ntx0    = (unsigned char)(wc & 31);
    rowbuf_a0 = rowbase + ((unsigned int)ntx0 << 1);
    rowbuf_a1 = rowbase;                    /* run 2: after the NT wrap */
    rowbuf_n1 = 32 - ntx0;                  /* entries before the wrap  */
    sub   = (wr & 1) << 1;
    mrow  = map_rom + row_off[wr >> 1];
    mtsub = mtdef_rom + sub;
    for (c = 0; c < 32; c++, wc++) {
        unsigned char m;
        if (wc >= (mapW << 1)) break;
        m = mrow[wc >> 1];
        if (n_ovr) m = cell_mt(wc >> 1, wr >> 1);
        rowbuf[c] = mtsub[((unsigned int)m << 2) + (wc & 1)];
        rowbuf_n++;
    }
    if (rowbuf_n1 > rowbuf_n) rowbuf_n1 = rowbuf_n;
    row_ready = 1;
}

static void blit_row(void) {
    const unsigned char *pt = (const unsigned char *)rowbuf;
    unsigned char c;
    VDPCtrlPort = (unsigned char)rowbuf_a0;
    VDPCtrlPort = (unsigned char)(rowbuf_a0 >> 8);
    for (c = 0; c < rowbuf_n1; c++) {
        VDPDataPort = *pt++;
        VDPDataPort = *pt++;
    }
    if (c < rowbuf_n) {
        VDPCtrlPort = (unsigned char)rowbuf_a1;
        VDPCtrlPort = (unsigned char)(rowbuf_a1 >> 8);
        for (; c < rowbuf_n; c++) {
            VDPDataPort = *pt++;
            VDPDataPort = *pt++;
        }
    }
}

static void nt_write_cell(unsigned int wc, unsigned int wr) {
    unsigned char m = cell_mt(wc >> 1, wr >> 1);
    unsigned int t = mtdef_rom[((unsigned int)m << 2) + ((wr & 1) << 1) + (wc & 1)];
    SMS_setTileatXY(wc & 31, wr % 28, t);
}

/* paced variant for use OUTSIDE vblank (teleporter full redraws happen
   with the display on; raw back-to-back port writes would be dropped) */
static void blit_col_paced(void) {
    unsigned char r;
    for (r = 0; r < colbuf_n; r++) {
        SMS_setAddr(colbuf_a[r]);
        SMS_setTile(colbuf[r]);
    }
}

static void full_redraw(void) {
    unsigned int c;
    pend_col_camr8 = cam_r8;
    for (c = 0; c <= 32; c++) {
        render_col(cam_c8 + c);
        blit_col_paced();
    }
    col_ready = row_ready = 0;
    pend_col = pend_row = 0xFFFF;
}

/* update camera to follow (cx,cy) target; queue entering strips */
static void set_scroll_y_full(void) { scroll_y = (unsigned char)(cam_y % 224); }

static void camera_follow(int tx, int ty) {
    unsigned int nc8, nr8;
    int d;
    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;
    if ((unsigned int)tx > mapPW - 256) tx = mapPW - 256;
    if ((unsigned int)ty > mapPH - 192) ty = mapPH - 192;
    /* keep scroll_y == cam_y % 224 incrementally (no per-frame division) */
    d = (int)scroll_y + (ty - cam_y);
    while (d >= 224) d -= 224;
    while (d < 0)    d += 224;
    scroll_y = (unsigned char)d;
    cam_x = tx; cam_y = ty;
    nc8 = cam_x >> 3; nr8 = cam_y >> 3;
    if (nc8 != cam_c8) {
        pend_col = (nc8 > cam_c8) ? (nc8 + 32) : (nc8 + 1);
        cam_c8 = nc8;
    }
    if (nr8 != cam_r8) {
        pend_row = (nr8 > cam_r8) ? (nr8 + 24) : nr8;
        cam_r8 = nr8;
    }
    pend_col_camr8 = cam_r8;
}

/* rewrite the NT tiles of a metatile cell, only where resident in the
   ring buffer (cols cam_c8+1..cam_c8+32, rows cam_r8..cam_r8+24) */
static void nt_sub_update(unsigned int wc, unsigned int wr) {
    if (wc < cam_c8 + 1 || wc > cam_c8 + 32) return;
    if (wr < cam_r8     || wr > cam_r8 + 24) return;
    nt_write_cell(wc, wr);
}
static void nt_update_cell(unsigned int mx, unsigned int my) {
    unsigned int wc = mx << 1, wr = my << 1;
    nt_sub_update(wc, wr);     nt_sub_update(wc + 1, wr);
    nt_sub_update(wc, wr + 1); nt_sub_update(wc + 1, wr + 1);
}

/* --------------------------------------------------------------- loading --- */
static void font_load(void) {
    SMS_load1bppTiles(font_1bpp, 0, 59 * 8, 0, 15);
    SMS_configureTextRenderer(-32);
}

static void print_at(unsigned char x, unsigned char y, const char *s) {
    SMS_setNextTileatXY(x, y);
    SMS_print((const unsigned char *)s);
}

static char numbuf[6];
static void print_num(unsigned char x, unsigned char y, unsigned int hi, unsigned int lo) {
    /* prints hi*10000+lo as 6+ digits (hi<=6553) */
    unsigned char i;
    unsigned long v = (unsigned long)hi * 10000UL + lo;
    for (i = 0; i < 6; i++) { numbuf[5 - i] = '0' + (unsigned char)(v % 10); v /= 10; }
    SMS_setNextTileatXY(x, y);
    for (i = 0; i < 6; i++) { unsigned char c = numbuf[i]; SMS_setTile((unsigned int)(c - 32)); }
}

static const unsigned int kind_score[15] =       /* ship parts: 10000; cards give none */
    { 100, 200, 500, 1000, 5000, 0, 0, 0, 0, 0, 0, 10000, 10000, 10000, 10000 };
static void add_score(unsigned char kind) {
    score_lo += kind_score[kind];
    while (score_lo >= 10000) { score_lo -= 10000; score_hi++; }
    if (score_hi >= next_life) {                 /* extra Keen every 20000 */
        next_life += 2;
        if (lives < 9) lives++;
        sfx_play(SND_EXTRAMAN);
    }
}

/* ---- animated tiles: live subtiles re-streamed from the original's frame
   cycle (see convert_ck1.py).  anim_scan() queues up to 6 stale tiles at
   the end of a frame; anim_upload() sends them in a VBlank that carries no
   other art burst. */
#define MAX_ANIM 80
static unsigned char an_cur[MAX_ANIM];
unsigned char an_n, an_cursor, ab_n;
static const unsigned char *an_list;
static unsigned int ab_tile[6];
static const unsigned char *ab_src[6];

static void anim_init(void) {
    unsigned char i;
    an_list = ld->anim; an_n = ld->nanim;
    if (an_n > MAX_ANIM) an_n = MAX_ANIM;
    for (i = 0; i < an_n; i++) an_cur[i] = an_list[i * 4 + 3] & 3;
    ab_n = 0; an_cursor = 0;
}

static void anim_scan(void) {
    unsigned char i, k, ph, step;
    const unsigned char *q;
    if (!an_n || ab_n) return;
    step = frame_ct >> 3;
    i = an_cursor;
    for (k = 0; k < an_n && ab_n < 6; k++) {
        q = an_list + i * 4;
        ph = (q[3] + step) & 3;
        if (ph != an_cur[i]) {
            an_cur[i] = ph;
            ab_tile[ab_n] = q[0] | ((unsigned int)q[1] << 8);
            ab_src[ab_n] = anim_art + (unsigned int)q[2] * 128 + ph * 32;
            ab_n++;
        }
        if (++i >= an_n) i = 0;
    }
    an_cursor = i;
}

static void anim_upload(void) {
    unsigned char k;
    map2(BANK_SPR2);
    for (k = 0; k < ab_n; k++) UNSAFE_SMS_loadNTiles(ab_src[k], ab_tile[k], 1);
    ab_n = 0;
}

static void load_level(unsigned char lvl) {
    unsigned char i;
    const unsigned char *p;

    SMS_displayOff();
    SMS_initSprites();
    SMS_copySpritestoSAT();
    SMS_VRAMmemsetW(0, 0x0000, 16384);

    cur_level = lvl;
    ld = &level_descs[lvl];
    mapW = ld->W; mapH = ld->H;
    mapPW = mapW << 4; mapPH = mapH << 4;
    {
        unsigned int o = 0, r;
        for (r = 0; r < mapH; r++) { row_off[r] = o; o += mapW; }
    }

    SMS_VDPturnOnFeature(VDPFEATURE_LEFTCOLBLANK);

    /* BG tiles */
    map2(ld->bank);
    SMS_loadTiles(ld->tiles, 0, ld->tiles_size);

    /* sprite art resident per level */
    map2(BANK_SPRITES);
    if (lvl) {
        SMS_loadTiles(spr_blt, VT_BULLET, 12 * 32);
        SMS_loadTiles(spr_eray, VT_ERAY, 2 * 32);
        SMS_loadTiles(spr_chunk, VT_CHUNK, 4 * 32);
    }
    if (lvl == LVL_FLY) {
        map2(BANK_SPR2);
        SMS_loadTiles(spr_ship, VT_SHIP, 8 * 32);
    }

    /* map-side data */
    map2(ld->map_bank);
    map_rom   = ld->map;
    mtdef_rom = ld->mtdef;
    for (i = 0; i < ld->nmt; i++) flags_ram[i] = ld->mtflags[i];
    /* scroll_y recomputed by callers after camera setup via set_scroll_y_full */

    n_ovr = 0;
    { unsigned char zi;
      for (zi = 0; zi <= MAX_OVR_ROWS; zi++) ovr_row0[zi] = 0; }
    n_items = ld->nitems;
    it_lo = 0;
    n_near = 0; near_timer = 0; near_pg = 0xFF;
    p = ld->items;
    for (i = 0; i < n_items; i++) {
        it_mx[i] = *p++; it_my[i] = *p++; it_kind[i] = *p++; it_restore[i] = *p++;
        it_taken[i] = 0;
    }
    n_doors = ld->ndoors;
    p = ld->doors;
    for (i = 0; i < n_doors; i++) {
        do_mx[i] = *p++; do_my[i] = *p++; do_col[i] = *p++; do_restore[i] = *p++;
        do_open[i] = 0;
    }
    n_exits = ld->nexits;
    p = ld->exits;
    for (i = 0; i < n_exits; i++) { ex_mx[i] = *p++; ex_my[i] = *p++; }
    y_n = ld->nents;
    if (y_n > MAX_ENT) y_n = MAX_ENT;
    p = ld->ents;
    for (i = 0; i < y_n; i++) {
        unsigned char t = *p++, emx = *p++, emy = *p++;
        e_type[i] = t;
        y_px[i] = (int)emx << 4;
        y_pg[i] = emx >> 4;                       /* (emx<<4)>>8 */
        y_py[i] = ((int)emy << 4) - (t <= ET_TANK ? 8 : 0);  /* 24px tall */
        y_vx[i] = y_vy[i] = y_sx[i] = y_sy[i] = 0;
        y_state[i] = 0; y_t[i] = (i * 37) & 63; y_hop[i] = (i * 13) & 31;
        y_hp[i] = (t == ET_VORT) ? 4 : 1;
        y_frame[i] = 0xFD; y_slot[i] = 0xFF;
        if (t == ET_YORP || t == ET_GARG) y_vx[i] = (i & 1) ? 128 : -128;
        if (t == ET_BUTLER || t == ET_TANK) y_vx[i] = (i & 1) ? 96 : -96;
    }
    for (i = 0; i < N_SLOTS; i++) { slot_owner[i] = 0xFF; slot_vframe[i] = 0xFE; slot_part[i] = 0; }
    for (i = 0; i < 4; i++) pj_on[i] = 0;
    inv_keys = 0; stun_t = 0; n_open_doors = 0;
    n_entries = ld->nentries;
    p = ld->entries;
    for (i = 0; i < n_entries && i < MAX_ENTRIES; i++) {
        en_mx[i] = *p++; en_my[i] = *p++; en_lvl[i] = *p++; en_done[i] = *p++;
        /* completed cities render their "done" art and stop being entries */
        if (en_lvl[i] < NLEVELS && level_done[en_lvl[i]])
            ovr_add(en_mx[i], en_my[i], en_done[i]);
    }
    n_teleports = ld->nteleports;
    if (n_teleports > MAX_TELEPORTS) n_teleports = MAX_TELEPORTS;
    p = ld->teleports;
    for (i = 0; i < n_teleports; i++) {
        tp_sx[i] = *p++; tp_sy[i] = *p++; tp_dx[i] = *p++; tp_dy[i] = *p++;
    }

    anim_init();

    px = (int)ld->spawn_x; py = (int)ld->spawn_y;
    if (lvl) py -= 8;                      /* sprite is 16x24, tile anchor */
    vx = vy = sx_acc = sy_acc = 0;
    on_ground = jumping = pogoing = pogo_squat = 0;
    jprep = 0; pogo_hold = 0; parts_at_entry = inv_parts; hint_used = 0;
    shoot_timer = 0; exiting = 0; dying = 0; seq_timer = 0;
    dir_right = 1; pframe = KF_STAND_R; cur_pframe = 0xFF; anim_t = 0;
    b_active = 0; fall_snd_on = 0;

    cam_x = cam_y = 0; scroll_y = 0;
    camera_follow(px + 8 - 124, py + 12 - 92);
    cam_c8 = cam_x >> 3; cam_r8 = cam_y >> 3;
    set_scroll_y_full();                 /* exact, in case of clamping */
    full_redraw();

    SMS_setBGScrollX((unsigned char)(0 - cam_x));
    SMS_setBGScrollY(scroll_y);

    SMS_setBackdropColor(0);
    SMS_displayOn();
}

/* ------------------------------------------------------------ interstitial - */
void wait_frames(unsigned int n) {
    while (n--) { SMS_waitForVBlank(); sfx_update(); }
}

static void screen_text_begin(void) {
    SMS_displayOff();
    SMS_VDPturnOffFeature(VDPFEATURE_LEFTCOLBLANK);
    SMS_initSprites();
    SMS_copySpritestoSAT();
    SMS_VRAMmemsetW(0, 0x0000, 16384);
    SMS_setBGScrollX(0); SMS_setBGScrollY(0);
    font_load();
}

/* -------------------------------------------------------------- dialogs --- */
/* Boxes are drawn straight into the scrolling name table. While one is up
   the scroll registers are snapped to a multiple of 8 so screen cell (cx,cy)
   is NT cell ((cam_c8+cx)&31, (cam_r8+cy)%28); closing re-renders exactly
   the covered cells from the map. Dialog tiles live in the sprite half of
   VRAM (256+), which is reloaded afterwards. */
char dlg_buf[168];
static unsigned char dlg_x0, dlg_y0, dlg_w, dlg_h;     /* box incl. frame, in cells */
static unsigned int  t_f0, t_f1, t_fr, t_ic, t_kn;     /* VRAM tile bases */

void txt_get(unsigned char id) {
    unsigned char ob = b2_shadow, i = 0;
    const unsigned char *p;
    SMS_mapROMBank(BANK_TXT);
    p = txt_data + txt_off[id];
    while (*p && i < sizeof(dlg_buf) - 1) dlg_buf[i++] = *p++;
    dlg_buf[i] = 0;
    SMS_mapROMBank(ob);
}

static void dlg_put(unsigned char cx, unsigned char cy, unsigned int tile) {
    SMS_setTileatXY((unsigned char)((cam_c8 + dlg_x0 + cx) & 31),
                    (unsigned char)((cam_r8 + dlg_y0 + cy) % 28), tile);
}

static void dlg_str(unsigned char cx, unsigned char cy, const char *s, unsigned int base) {
    unsigned char x = cx;
    for (; *s; s++) {
        if (*s == '\n') { x = cx; cy++; continue; }
        if (*s >= 32) dlg_put(x, cy, base + (unsigned char)(*s - 32));
        x++;
    }
}

/* box of (w x h) interior cells, centred; fill = tile for the interior */
static void dlg_box(unsigned char w, unsigned char h, unsigned int fill) {
    unsigned char x, y;
    dlg_w = w + 2; dlg_h = h + 2;
    dlg_x0 = 1 + (31 - dlg_w) / 2;
    dlg_y0 = (24 - dlg_h) / 2;
    SMS_waitForVBlank();
    SMS_initSprites(); SMS_copySpritestoSAT();
    SMS_setBGScrollX((unsigned char)(0 - (cam_c8 << 3)));
    SMS_setBGScrollY((unsigned char)((cam_r8 % 28) << 3));
    for (y = 0; y < dlg_h; y++)
        for (x = 0; x < dlg_w; x++) {
            unsigned int tile = fill;
            if (y == 0)              tile = t_fr + (x == 0 ? 0 : (x == dlg_w - 1 ? 2 : 1));
            else if (y == dlg_h - 1) tile = t_fr + (x == 0 ? 5 : (x == dlg_w - 1 ? 7 : 6));
            else if (x == 0)         tile = t_fr + 3;
            else if (x == dlg_w - 1) tile = t_fr + 4;
            dlg_put(x, y, tile);
        }
}

static void dlg_wait(void) {
    unsigned int ks;
    for (;;) { SMS_waitForVBlank(); sfx_update(); if (!(SMS_getKeysStatus() & (PORT_A_KEY_1 | PORT_A_KEY_2))) break; }
    for (;;) {
        SMS_waitForVBlank(); sfx_update();
        ks = SMS_getKeysStatus();
        if (ks & (PORT_A_KEY_1 | PORT_A_KEY_2)) break;
    }
    for (;;) { SMS_waitForVBlank(); if (!(SMS_getKeysStatus() & (PORT_A_KEY_1 | PORT_A_KEY_2))) break; }
    prev_ks = SMS_getKeysStatus();
}

static void dlg_close(void) {
    unsigned char x, y;
    for (y = 0; y < dlg_h; y++) {
        unsigned int wr = cam_r8 + dlg_y0 + y;
        if (wr >= (mapH << 1)) continue;
        for (x = 0; x < dlg_w; x++) {
            unsigned int wc = cam_c8 + dlg_x0 + x;
            if (wc >= (mapW << 1)) continue;
            nt_write_cell(wc, wr);
        }
    }
    SMS_waitForVBlank();
    SMS_setBGScrollX((unsigned char)(0 - cam_x));
    SMS_setBGScrollY(scroll_y);
    /* sprite art that lived under the dialog tiles must be streamed again */
    {
        unsigned char s;
        cur_pframe = 0xFF;
        for (s = 0; s < N_SLOTS; s++) { slot_vframe[s] = 0xFE; slot_part[s] = 0; }
        if (cur_level) {
            map2(BANK_SPRITES);
            SMS_loadTiles(spr_blt, VT_BULLET, 12 * 32);
            SMS_loadTiles(spr_eray, VT_ERAY, 2 * 32);
            SMS_loadTiles(spr_chunk, VT_CHUNK, 4 * 32);
            map2(ld->map_bank);
        }
    }
}

/* load the plain message-box tile set: font 0 (black on white) + frame */
static void dlg_tiles_msg(void) {
    unsigned char ob = b2_shadow;
    t_f0 = 256; t_fr = 256 + 96;
    SMS_mapROMBank(BANK_TXT);
    SMS_loadTiles(dlg_f0, t_f0, 96 * 32);
    SMS_loadTiles(dlg_frame, t_fr, 8 * 32);
    SMS_mapROMBank(ob);
}

/* "You hear in your mind..." style box from string id */
static void show_text_box(unsigned char id) {
    unsigned char w = 0, h = 1, cur = 0;
    const char *s;
    txt_get(id);
    for (s = dlg_buf; *s; s++) {
        if (*s == '\n') { if (cur > w) w = cur; cur = 0; h++; } else cur++;
    }
    if (cur > w) w = cur;
    dlg_tiles_msg();
    dlg_box(w + 2, h + 2, t_f0);              /* one cell of padding all round */
    dlg_str(2, 2, dlg_buf, t_f0);
}

static void show_hint(unsigned char lvl_msg) {
    show_text_box(lvl_msg);
    dlg_wait();
    dlg_close();
}

/* 16x16 icon (4 tiles) at interior cell (cx,cy); pal = NT palette bit */
static void dlg_icon(unsigned char cx, unsigned char cy, unsigned int base, unsigned int pal) {
    dlg_put(cx,     cy,     base     | pal);
    dlg_put(cx + 1, cy,     base + 1 | pal);
    dlg_put(cx,     cy + 1, base + 2 | pal);
    dlg_put(cx + 1, cy + 1, base + 3 | pal);
}

static void show_ship_box(void) {
    unsigned char i, ob = b2_shadow;
    show_text_box(TXT_SHIP);
    t_ic = t_fr + 8;
    SMS_mapROMBank(BANK_TXT);
    SMS_loadTiles(dlg_icons + 6 * 128, t_ic, 4 * 4 * 32);   /* the four "missing part" icons */
    SMS_mapROMBank(ob);
    for (i = 0; i < 4; i++)
        if (!(inv_parts & (1 << i)))
            dlg_icon(5 + 4 * i, 5, t_ic + 4 * i, 0);
    dlg_wait();
    dlg_close();
}

static void show_keens_left(void) {
    unsigned char i, ob = b2_shadow;
    dlg_tiles_msg();
    t_kn = t_fr + 8;
    SMS_mapROMBank(BANK_TXT);
    SMS_loadTiles(dlg_keen, t_kn, 4 * 32);
    SMS_mapROMBank(ob);
    dlg_box(23, 4, t_f0);
    txt_get(TXT_LIVES);
    dlg_str(5, 1, dlg_buf + 3, t_f0);          /* "Keens Left" without the lead-in spaces */
    for (i = 0; i < lives && i < 10; i++)
        dlg_icon(1 + i * 2, 3, t_kn, 0);
    {
        unsigned char f;
        for (f = 0; f < 180; f++) {
            SMS_waitForVBlank(); sfx_update();
            if (f > 20 && (SMS_getKeysStatus() & (PORT_A_KEY_1 | PORT_A_KEY_2))) break;
        }
    }
    dlg_close();
    prev_ks = SMS_getKeysStatus();
}

static void dlg_num(unsigned char cx, unsigned char cy, unsigned long v, unsigned char right_edge) {
    char nb[8]; unsigned char n = 0, i;
    do { nb[n++] = '0' + (unsigned char)(v % 10); v /= 10; } while (v && n < 7);
    for (i = 0; i < n; i++)
        dlg_put(right_edge - i, cy, t_f0 + (unsigned char)(nb[i] - 32));
    (void)cx;
}

static void dlg_field(unsigned char x, unsigned char y, unsigned char w, unsigned char h) {
    unsigned char i, j;
    for (j = 0; j < h; j++) for (i = 0; i < w; i++) dlg_put(x + i, y + j, t_f0);
}

/* the original status box (CStatusScreen): 30x15 cells */
static void show_status_box(void) {
    unsigned char i, ob = b2_shadow;
    unsigned long sc = (unsigned long)score_hi * 10000UL + score_lo;
    SMS_waitForVBlank();
    /* tile set: font1 (red on grey, 64) | font0 ' '..'9' (26) | frame (8) | icons (14*4) | keen (4) */
    t_f1 = 256; t_f0 = 256 + 64; t_fr = t_f0 + 26; t_ic = t_fr + 8; t_kn = t_ic + 56;
    SMS_mapROMBank(BANK_TXT);
    SMS_loadTiles(dlg_f1, t_f1, 64 * 32);
    SMS_loadTiles(dlg_f0, t_f0, 26 * 32);
    SMS_loadTiles(dlg_frame, t_fr, 8 * 32);
    SMS_loadTiles(dlg_icons, t_ic, 56 * 32);
    SMS_loadTiles(dlg_keen, t_kn, 4 * 32);
    SMS_mapROMBank(ob);
    dlg_box(28, 13, t_f1);
    txt_get(TXT_STATUS);
    dlg_str(1, 1, dlg_buf, t_f1);
    dlg_field(1, 2, 12, 1);  dlg_field(14, 2, 15, 1);          /* score / extra keen at */
    dlg_field(1, 4, 14, 3);  dlg_field(16, 4, 13, 3);          /* keens / ship parts */
    dlg_field(1, 8, 8, 3);   dlg_field(10, 8, 6, 5);  dlg_field(17, 8, 12, 5);
    dlg_field(1, 12, 8, 1);                                    /* charges */
    dlg_num(0, 2, sc, 11);
    dlg_num(0, 2, (unsigned long)next_life * 10000UL, 27);
    dlg_icon(4, 8, t_ic, 0);                                   /* raygun */
    if (inv_pogo) dlg_icon(12, 9, t_ic + 4, 0);
    for (i = 0; i < 4; i++)
        if (inv_keys & (1 << i))
            dlg_icon(20 + (i & 1) * 4, 8 + (i >> 1) * 2, t_ic + 8 + 4 * i, 0);
    for (i = 0; i < 4; i++)
        dlg_icon(17 + 3 * i, 4, t_ic + ((inv_parts & (1 << i)) ? 40 : 24) + 4 * i, 0);
    dlg_num(0, 12, inv_ammo, 5);
    for (i = 0; i < lives && i < 6; i++)
        dlg_icon(2 + i * 2, 4, t_kn, 0);
    dlg_wait();
    dlg_close();
}

/* ---------------------------------------------------------------- player --- */
/* hitbox: x px+3..px+12 (w10), y py+1..py+23 (h23), sprite 16x24 */

static unsigned char probe3_x(int wx, int y0) {
    return (mflag(wx, y0 + 2) | mflag(wx, y0 + 12) | mflag(wx, y0 + 22));
}

static void player_move_x(void) {
    int dx;
    sx_acc += vx;
    dx = sx_acc >> 8;
    sx_acc -= dx << 8;
    if (dx > 0) {
        unsigned char f = probe3_x(px + 12 + dx, py);
        if (f & F_BLKR) {
            px = (((px + 12 + dx) & ~15) - 13);
            vx = 0; sx_acc = 0;
        } else px += dx;
    } else if (dx < 0) {
        unsigned char f = probe3_x(px + 3 + dx, py);
        if (f & F_BLKL) {
            px = ((((px + 3 + dx) >> 4) + 1) << 4) - 3;
            vx = 0; sx_acc = 0;
        } else px += dx;
    }
    if (px < -3) px = -3;
    if (px > (int)mapPW - 13) px = (int)mapPW - 13;
}

static void player_move_y(void) {
    int dy;
    sy_acc += vy;
    dy = sy_acc >> 8;
    sy_acc -= dy << 8;
    if (dy > 0) {
        int ny = py + 23 + dy;                 /* new feet */
        unsigned char f = mflag(px + 4, ny) | mflag(px + 11, ny);
        unsigned char crossed = ((py + 23) >> 4) < (ny >> 4);
        if ((f & F_UP) && ((f & F_CEIL) || crossed)) {
            py = ((ny & ~15) - 24);
            vy = 0; sy_acc = 0;
            if (!on_ground) {                   /* landing */
                if (!pogoing) sfx_play(SND_LAND);
                fall_snd_on = 0;
            }
            on_ground = 1; jumping = 0;
        } else { py += dy; on_ground = 0; }
    } else if (dy < 0) {
        int ny = py + 1 + dy;                  /* new head */
        unsigned char f = mflag(px + 4, ny) | mflag(px + 11, ny);
        if (f & F_CEIL) {
            py = ((((ny >> 4) + 1) << 4) - 1);
            vy = 0; sy_acc = 0;
            sfx_play(SND_BUMPHEAD);
        } else py += dy;
        on_ground = 0;
    } else {
        /* recheck ground under feet */
        int fy = py + 24;
        unsigned char f = mflag(px + 4, fy) | mflag(px + 11, fy);
        on_ground = ((f & F_UP) && ((f & F_CEIL) || (fy & 15) == 0)) ? 1 : 0;
        if (on_ground) jumping = 0;
    }
}

/* player life cycle (CG: pdie / dieanim). lives are shown on the status
   screen; losing the last one ends the game. */

static void player_die(void) {
    if (dying) return;
    dying = 1; seq_timer = 0;
    sfx_play(SND_DIE);
    vx = vy = 0; pogoing = 0; jprep = 0; jumping = 0;
    lives--;
    inv_parts = parts_at_entry;               /* a part found in this level is lost */
}

static void spawn_bullet(void) {
    if (!inv_ammo) { sfx_play(SND_GUNCLICK); return; }
    if (b_active) return;
    inv_ammo--;
    b_active = 1; b_hit = 0; b_timer = 0;
    b_right = dir_right;
    b_px = px + (dir_right ? 12 : -12);
    b_py = py + 6;
    sfx_play(SND_FIRE);
}

static unsigned char flags_under(void) {      /* tiles just below the feet */
    return mflag(px + 4, py + 24) | mflag(px + 11, py + 24);
}

static void player_launch(int v) {
    vy = v; on_ground = 0; jumping = 1; jprep = 0; sy_acc = 0;
}

static void player_update(unsigned int ks, unsigned int kp) {
    unsigned char i, ice = 0, semi = 0;

    if (dying) {                               /* CG dieanim: 80 ticks, then fly off */
        seq_timer++;
        pframe = ((seq_timer / 6) & 1) ? KF_DIE1 : KF_DIE0;
        if (seq_timer > 80) {
            if (py > (int)cam_y - 32 && py > 48 - 128) {
                py -= 5;
                px += (int)(rng & 3) - 1;
            } else game_state = STATE_DEAD;
        }
        rng = rng * 5 + 3;
        return;
    }
    if (exiting) {
        seq_timer++;
        vx = 200; dir_right = 1;
        player_move_x();
        anim_t++;
        if (!(anim_t & 7)) {
            if (++anim_ph == 4) anim_ph = 0;
            if (on_ground)
                sfx_play((anim_ph & 1) ? SND_WALK2 : SND_WALK1);
        }
        pframe = KF_WALK_R + walk_seq[anim_ph];
        if (seq_timer > 70) {
            level_done[cur_level] = 1;
            game_state = STATE_OW;
        }
        return;
    }

    /* frozen by an ice chunk (CG keencicle: 250 ticks, thaw frame at the end) */
    if (stun_t) {
        stun_t--;
        ks = 0; kp = 0;
        if (on_ground && !(flags_under() & F_ICE)) vx = 0;
        pframe = (stun_t < 20) ? 31 : (28 + ((stun_t / 26) & 1));
    }

    if (on_ground && !pogoing) {
        unsigned char f = flags_under();
        if (f & F_ICE3) ice = 1;
        else if (f & F_ICE) semi = 1;
    }

    /* pogo toggle: DOWN + button 1 (CG: its own pogo key) */
    if ((kp & PORT_A_KEY_1) && (ks & PORT_A_KEY_DOWN) && inv_pogo && !jprep && !stun_t) {
        pogoing = !pogoing;
        pogo_squat = 0; pogo_hold = 0;
        if (pogoing) { jump_buf = 0; }
    }

    if (pogoing) {
        if (ks & PORT_A_KEY_1) { if (pogo_hold < 40) pogo_hold++; } else pogo_hold = 0;
        if (ks & PORT_A_KEY_LEFT)  { vx -= POGO_STEER; dir_right = 0; }
        if (ks & PORT_A_KEY_RIGHT) { vx += POGO_STEER; dir_right = 1; }
        if (vx >  WALK_MAX) vx =  WALK_MAX;
        if (vx < -WALK_MAX) vx = -WALK_MAX;
        if (on_ground) {
            /* compressed for PPOGO_PREPARE_TIME ticks, then bounce; the
               longer jump has been held, the higher (up to 1.1*165) */
            if (++pogo_squat > 10) {
                int v = POGO_LO_V;
                pogo_squat = 0;
                if (pogo_hold) {
                    v = -(960 + pogo_hold * 29);
                    if (v < POGO_HI_V) v = POGO_HI_V;
                    sfx_play(SND_POGOJUMP);
                } else sfx_play(SND_POGO);
                player_launch(v);
            }
        }
        pframe = on_ground ? (dir_right ? KF_POGO_DN_R : KF_POGO_DN_L)
                           : (dir_right ? KF_POGO_UP_R : KF_POGO_UP_L);
    } else if (jprep) {
        /* crouching for the jump: frames 8..13, one step per 3 ticks; the
           jump key is sampled as it is released (or frame 13 reached) */
        if (++jtimer > 2) {
            jtimer = 0;
            if (jframe == 5 || !(ks & PORT_A_KEY_1)) {
                int v = (jframe >= 4) ? JUMP_V : (int)(((long)JUMP_V * jframe) / 5);
                if (ice) dir_right = dir_right;     /* ice: keep facing */
                else if (!(ks & (PORT_A_KEY_LEFT | PORT_A_KEY_RIGHT))) vx = 0;
                if (ks & PORT_A_KEY_LEFT)  { vx = -512; dir_right = 0; }
                if (ks & PORT_A_KEY_RIGHT) { vx =  512; dir_right = 1; }
                sfx_play(SND_JUMP);
                player_launch(v);
            } else jframe++;
        }
        pframe = (dir_right ? 8 : 14) + jframe;
    } else {
        /* walking / sliding */
        if (ice) {
            if (ks & PORT_A_KEY_LEFT)       dir_right = 0;
            else if (ks & PORT_A_KEY_RIGHT) dir_right = 1;
            vx = dir_right ? WALK_MAX : -WALK_MAX;
        } else if (shoot_timer && on_ground) {
            vx = 0;                              /* frozen while shooting     */
        } else if (ks & PORT_A_KEY_LEFT) {
            vx -= on_ground ? WALK_ACC : AIR_ACC;
            if (vx < -WALK_MAX) vx = -WALK_MAX;
            dir_right = 0;
        } else if (ks & PORT_A_KEY_RIGHT) {
            vx += on_ground ? WALK_ACC : AIR_ACC;
            if (vx > WALK_MAX) vx = WALK_MAX;
            dir_right = 1;
        } else {
            if (on_ground) { if (!semi) vx = 0; }   /* semi-ice: coast */
            else vx = (vx * 15) >> 4;
        }
        /* jump press starts the crouch (buffered a few frames) */
        if ((kp & PORT_A_KEY_1) && !(ks & PORT_A_KEY_DOWN) && !stun_t)
            jump_buf = JUMP_BUF_FRAMES;
        if (jump_buf && on_ground && !shoot_timer) {
            jump_buf = 0;
            jprep = 1; jframe = 0; jtimer = 0;
            if (!ice) vx = 0;
        }
        if (jump_buf) jump_buf--;
    }

    /* fire (CG: shot frame shown 25 ticks) */
    if ((kp & PORT_A_KEY_2) && !jprep && !stun_t) {
        spawn_bullet();
        shoot_timer = 25;
    }
    if (shoot_timer) shoot_timer--;

    /* gravity (CG: up-decel 4, fall accel 8 after the apex, ledge fall 4) */
    if (!on_ground) {
        if (jumping) {
            vy += (vy < 0) ? GRAV_UP : GRAV_DN;
        } else {
            if (vy < 256) vy = 256;
            vy += GRAV_WALKOFF;
        }
        if (vy > FALL_MAX) vy = FALL_MAX;
    }

    if (!jprep) player_move_x();
    player_move_y();

    /* walked off a ledge */
    if (!on_ground && !jumping && !fall_snd_on) {
        fall_snd_on = 1;
        sfx_play(SND_PLUMMET);
    }

    /* deadly tiles: direct cell fetches (bounds-checked once) */
    if (py >= -4 && (unsigned int)(py + 20) < mapPH) {
        unsigned char cx0 = (unsigned char)((unsigned int)(px + 3) >> 4);
        unsigned char cx1 = (unsigned char)((unsigned int)(px + 12) >> 4);
        const unsigned char *r0 = map_rom + row_off[(unsigned int)(py + 4) >> 4];
        const unsigned char *r1 = map_rom + row_off[(unsigned int)(py + 20) >> 4];
        if ((flags_ram[r0[cx0]] | flags_ram[r0[cx1]] |
             flags_ram[r1[cx0]] | flags_ram[r1[cx1]]) & F_DEADLY) {
            player_die(); return;
        }
    }
    /* fell out of the world (CG: PLUMMET finishes, then Keen dies) */
    if (py > (int)mapPH + 16) { player_die(); seq_timer = 120; return; }

    /* items: the table is sorted by column; keep a cached start index and
       scan only items with mx in [pcx-1, pcx+1]. The cache follows the
       player incrementally, so per-frame cost is a handful of items
       instead of the whole table (up to ~170 on big levels). */
    {
    unsigned char pcx = (unsigned char)((unsigned int)(px + 8) >> 4);
    unsigned char pcy = (unsigned char)((unsigned int)(py + 12) >> 4);
    unsigned char lo = it_lo;
    while (lo && (unsigned char)(it_mx[lo - 1] + 1) >= pcx) lo--;
    while (lo < n_items && (unsigned char)(it_mx[lo] + 1) < pcx) lo++;
    it_lo = lo;
    for (i = lo; i < n_items && it_mx[i] <= (unsigned char)(pcx + 1); i++) {
        int cx, cy;
        if (it_taken[i]) continue;
        if ((unsigned char)(it_my[i] - pcy + 1) > 2) continue;
        cx = (int)it_mx[i] << 4; cy = (int)it_my[i] << 4;
        if (px + 12 >= cx && px + 3 <= cx + 15 &&
            py + 23 >= cy && py + 1 <= cy + 15) {
            unsigned char k = it_kind[i];
            if (k == K_HINT && (hint_used || stun_t)) continue;
            it_taken[i] = 1;
            if (k == K_TELE) {                   /* in-level teleporter: back to Mars */
                sfx_play(SND_TELEPORT);
                tele_pending = 1;
                game_state = STATE_OW;
                return;
            }
            ovr_add(it_mx[i], it_my[i], it_restore[i]);
            nt_update_cell(it_mx[i], it_my[i]);
            if (k == K_HINT) {                   /* once per level: the statue's message */
                hint_used = 1;
                show_hint(hint_msg[cur_level]);
                return;
            }
            add_score(k);
            if (k == K_AMMO)      { inv_ammo += 5; sfx_play(SND_ITEM); }
            else if (k == K_POGO) { inv_pogo = 1;  sfx_play(SND_ITEM); }
            else if (k >= K_CARDY && k <= K_CARDB)
                { inv_keys |= 1 << (k - K_CARDY); sfx_play(SND_CARD); }
            else if (k >= K_JOYSTICK)
                { inv_parts |= 1 << (k - K_JOYSTICK); sfx_play(SND_PART); }
            else sfx_play(SND_BONUS);
        }
    }
    }

    /* exit door cells (tile 159): touch while grounded */
    if (on_ground && !pogoing && !jprep) {
        unsigned char ecx = (unsigned char)((unsigned int)(px + 8) >> 4);
        unsigned char ecy = (unsigned char)((unsigned int)(py + 12) >> 4);
        for (i = 0; i < n_exits; i++)
            if (ex_mx[i] == ecx && (ex_my[i] == ecy || ex_my[i] == ecy + 1)) {
                exiting = 1; seq_timer = 0;
                sfx_play(SND_LVLDONE);
                break;
            }
    }

    /* locked doors: bump into one holding the matching card to open it */
    if ((ks & (PORT_A_KEY_LEFT | PORT_A_KEY_RIGHT)) && n_doors) {
        int probe_x = (ks & PORT_A_KEY_RIGHT) ? (px + 14) : (px + 1);
        unsigned char dcx = (unsigned char)((unsigned int)probe_x >> 4);
        unsigned char dcy = (unsigned char)((unsigned int)(py + 12) >> 4);
        for (i = 0; i < n_doors; i++) {
            if (do_open[i] || do_mx[i] != dcx) continue;
            if (do_my[i] != dcy && do_my[i] != dcy - 1 && do_my[i] != dcy + 1)
                continue;
            if (inv_keys & (1 << do_col[i])) {
                unsigned char j, c = do_col[i];
                for (j = 0; j < n_doors; j++)      /* whole door (all cells) */
                    if (do_col[j] == c && !do_open[j]) {
                        do_open[j] = 1; n_open_doors++;
                        ovr_add(do_mx[j], do_my[j], do_restore[j]);
                        nt_update_cell(do_mx[j], do_my[j]);
                    }
                sfx_play(SND_DOOR);
            }
            break;
        }
    }

    /* animation */
    if (stun_t) return;                          /* frozen frame set above */
    if (jprep) return;
    if (!on_ground) {
        pframe = dir_right ? KF_FALL_R : KF_FALL_L;
        if (pogoing) pframe = dir_right ? KF_POGO_UP_R : KF_POGO_UP_L;
    } else if (shoot_timer) {
        pframe = dir_right ? KF_SHOOT_R : KF_SHOOT_L;
    } else if (!pogoing) {
        if (vx && !ice) {
            anim_t++;
            if (!(anim_t & 7)) {
                if (++anim_ph == 4) anim_ph = 0;
                sfx_play((anim_ph & 1) ? SND_WALK2 : SND_WALK1);
            }
            pframe = (dir_right ? KF_WALK_R : KF_WALK_L) + walk_seq[anim_ph];
        } else {
            pframe = dir_right ? KF_STAND_R : KF_STAND_L;
        }
    }
}

/* ---------------------------------------------------------------- bullet --- */
static void bullet_update(void) {
    unsigned char i;
    if (!b_active) return;
    if (b_hit) {
        if (++b_timer > 8) b_active = 0;
        return;
    }
    b_px += b_right ? (BULLET_V >> 8) : -(BULLET_V >> 8);
    /* out of view */
    if (b_px < (int)cam_x - 24 || b_px > (int)cam_x + 272) { b_active = 0; return; }
    /* wall */
    if (mflag(b_px + 8, b_py + 4) & (b_right ? F_BLKR : F_BLKL)) {
        b_hit = 1; b_timer = 0;
        rng = rng * 13 + 7;
        sfx_play(SND_SHOTHIT);
        return;
    }
    /* entities */
    for (i = 0; i < y_n; i++) {
        if (y_state[i] >= 2 || e_type[i] >= ET_CANNON0) continue;
        if (b_px + 12 >= y_px[i] && b_px + 4 <= y_px[i] + 15 &&
            b_py + 6  >= y_py[i] && b_py + 2 <= y_py[i] + 23) {
            b_hit = 1; b_timer = 0;
            sfx_play(SND_SHOTHIT);
            if (e_type[i] == ET_TANK || e_type[i] == ET_BUTLER)
                return;                          /* armored: zap absorbed */
            if (y_hp[i] > 1) { y_hp[i]--; return; }
            y_state[i] = 2; y_t[i] = 0;
            sfx_play((e_type[i] == ET_YORP) ? SND_YORPSCREAM :
                     (e_type[i] == ET_GARG) ? SND_GARGSCREAM :
                                              SND_VORTSCREAM);
            return;
        }
    }
}

/* ----------------------------------------------------------------- yorps --- */
/* move entity i with gravity + tile collision (16x24 body, 12px wide box) */
static void ent_move(unsigned char i, unsigned char gravity) {
    int d;
    unsigned char f;
    if (gravity) {
        y_vy[i] += 20;
        if (y_vy[i] > 768) y_vy[i] = 768;
    }
    y_sx[i] += y_vx[i];
    d = y_sx[i] >> 8; y_sx[i] -= d << 8;
    if (d > 0) {
        f = mflag(y_px[i] + 13 + d, y_py[i] + 8) | mflag(y_px[i] + 13 + d, y_py[i] + 20);
        if (f & F_BLKR) { y_vx[i] = -y_vx[i]; } else y_px[i] += d;
    } else if (d < 0) {
        f = mflag(y_px[i] + 2 + d, y_py[i] + 8) | mflag(y_px[i] + 2 + d, y_py[i] + 20);
        if (f & F_BLKL) { y_vx[i] = -y_vx[i]; } else y_px[i] += d;
    }
    y_pg[i] = (unsigned char)(((unsigned int)y_px[i]) >> 8);
    y_sy[i] += y_vy[i];
    d = y_sy[i] >> 8; y_sy[i] -= d << 8;
    if (d > 0) {
        int ny = y_py[i] + 23 + d;
        unsigned char crossed = ((y_py[i] + 23) >> 4) < (ny >> 4);
        f = mflag(y_px[i] + 4, ny) | mflag(y_px[i] + 11, ny);
        if ((f & F_UP) && ((f & F_CEIL) || crossed)) {
            y_py[i] = (ny & ~15) - 24; y_vy[i] = 0; y_sy[i] = 0;
        } else y_py[i] += d;
    } else if (d < 0) {
        int ny = y_py[i] + 1 + d;
        f = mflag(y_px[i] + 4, ny) | mflag(y_px[i] + 11, ny);
        if (f & F_CEIL) { y_vy[i] = 0; y_sy[i] = 0; }
        else y_py[i] += d;
    }
}

static unsigned char ent_grounded(unsigned char i) {
    return ((mflag(y_px[i] + 4, y_py[i] + 24) |
             mflag(y_px[i] + 11, y_py[i] + 24)) & F_UP) ? 1 : 0;
}

/* is a walk in direction dir (0=left 1=right) about to step off a ledge? */
static unsigned char ent_at_edge(unsigned char i, unsigned char right) {
    int ex = right ? (y_px[i] + 15) : y_px[i];
    return !(mflag(ex, y_py[i] + 26) & F_UP);
}

static unsigned char player_overlap(unsigned char i) {
    return (px + 12 >= y_px[i] && px + 3 <= y_px[i] + 15 &&
            py + 22 >= y_py[i] + 2 && py + 2 <= y_py[i] + 22);
}

static void spawn_proj(unsigned char kind, int x, int y, int vx8, int vy8) {
    unsigned char i;
    for (i = 0; i < 4; i++)
        if (!pj_on[i]) {
            pj_on[i] = kind;
            pj_x[i] = x; pj_y[i] = y; pj_vx[i] = vx8; pj_vy[i] = vy8;
            return;
        }
}

/* Rebuild the near-entity list: everything within x-pages cam_pg-1..
   cam_pg+2 (>=256px of slack around the AI wake window). Rebuilt every 8
   frames or when the camera page changes; entities and camera drift well
   under a page in that time, so nothing can slip in or out unseen. This
   lets the per-frame entity loops touch ~6 indices instead of MAX_ENT --
   SDCC's per-access array indexing makes even "filtered" iterations
   expensive, so shrinking N is worth more than any per-iteration trim. */
static void rebuild_near(void) {
    unsigned char i, n = 0;
    for (i = 0; i < y_n; i++) {
        if ((unsigned char)(y_pg[i] - cam_pg + 1) <= 3) {
            near_list[n++] = i;
        } else {
            /* far away: release any sprite slot it still holds and give
               stunned ones their recovery ticks for the skipped frames */
            if (y_slot[i] != 0xFF) { slot_owner[y_slot[i]] = 0xFF; y_slot[i] = 0xFF; }
            if (y_state[i] == 1) {
                y_t[i] += 8;
                if (y_t[i] > 240) { y_state[i] = 0; y_t[i] = 0; }
            }
        }
    }
    n_near = n;
}

static void ent_update(unsigned char i) {
    int dx;
    unsigned char t = e_type[i];

    if (y_state[i] == 3) return;                 /* dead */
    dx = y_px[i] - (int)cam_x;
    if (dx < -80 || dx > 320) {                  /* offscreen cull */
        if (y_state[i] == 1 && ++y_t[i] > 240) { y_state[i] = 0; y_t[i] = 0; }
        return;
    }
    if ((dx < -16 || dx > 256) && ((frame_ct ^ i) & 1)) {
        /* in the wake window but not on screen: think at half rate.
           On-screen behavior is untouched; offscreen walkers just cover
           ground a little slower, close to the original's sleep-until-
           seen behavior. */
        return;
    }

    if (t >= ET_CANNON0) {                       /* ice cannons: fire chunks */
        static const signed char cvx[4] = { 1, 0, 0, -1 };
        static const signed char cvy[4] = { -1, -1, 1, -1 };
        if (++y_t[i] > 110) {
            unsigned char v = t - ET_CANNON0;
            y_t[i] = 0;
            sfx_play(SND_CANNON);
            spawn_proj(1, y_px[i], y_py[i] + 8,
                       (int)cvx[v] * 384, (int)cvy[v] * 384);
        }
        return;
    }

    if (y_state[i] == 2) {                       /* dying */
        y_frame[i] = (t == ET_YORP) ? 10 : ((t == ET_GARG) ? 5 : 6);
        if (++y_t[i] > 12) {
            y_state[i] = 3;                      /* corpse stays visible */
            y_frame[i] = (t == ET_YORP) ? 11 : ((t == ET_GARG) ? 6 : 7);
        }
        return;
    }
    if (y_state[i] == 1) {                       /* stunned (yorp only) */
        y_frame[i] = 8 + ((y_t[i] >> 3) & 1);
        if (++y_t[i] > 240) { y_state[i] = 0; y_t[i] = 0; }
        return;
    }

    switch (t) {
    case ET_YORP:
        if (++y_hop[i] > 40) {
            if (ent_grounded(i)) y_vy[i] = -320;
            y_hop[i] = 0;
        }
        dx = px - y_px[i];
        if (dx < -20 || dx > 20) {
            if (dx < 0) { y_vx[i] -= 13; if (y_vx[i] < -64) y_vx[i] = -64; }
            else        { y_vx[i] += 13; if (y_vx[i] >  64) y_vx[i] =  64; }
        } else y_vx[i] = 0;
        ent_move(i, 1);
        /* head-bump stuns; side contact shoves keen away */
        if (!dying && player_overlap(i)) {
            if (vy > 0 && py + 24 <= y_py[i] + 10) {
                y_state[i] = 1; y_t[i] = 0;
                vy = -400; pogoing = 0;
                sfx_play(SND_YORPBOP);
            } else {
                vx = (px < y_px[i]) ? -600 : 600;
                sfx_play(SND_YORPBUMP);
            }
        }
        if (y_vx[i] > 0)      y_frame[i] = 6 + ((y_hop[i] >> 3) & 1);
        else if (y_vx[i] < 0) y_frame[i] = 4 + ((y_hop[i] >> 3) & 1);
        else                  y_frame[i] = 1 + ((y_hop[i] >> 4) & 1);
        break;

    case ET_GARG: {
        /* wander; charge when keen is roughly level and lined up */
        int dy = py - y_py[i];
        unsigned char charging = 0;
        dx = px - y_px[i];
        if (dy > -24 && dy < 24 && dx > -140 && dx < 140) {
            charging = 1;
            y_vx[i] = (dx < 0) ? -224 : 224;
        } else if (y_vx[i] > 96) y_vx[i] = 96;
        else if (y_vx[i] < -96) y_vx[i] = -96;
        else if (!y_vx[i]) y_vx[i] = 96;
        if (!charging && ent_grounded(i) && ent_at_edge(i, y_vx[i] > 0))
            y_vx[i] = -y_vx[i];                  /* don't walk off ledges */
        ent_move(i, 1);
        if (!dying && player_overlap(i)) player_die();
        y_frame[i] = (y_vx[i] > 0) ? (1 + (charging ? (y_hop[i] >> 2 & 1)
                                                    : (y_hop[i] >> 3 & 1)))
                                   : (3 + (charging ? (y_hop[i] >> 2 & 1)
                                                    : (y_hop[i] >> 3 & 1)));
        y_hop[i]++;
        break;
    }

    case ET_VORT:
        /* stalk keen; jump every so often */
        dx = px - y_px[i];
        y_vx[i] = (dx < 0) ? -128 : 128;
        if (++y_hop[i] > 70 && ent_grounded(i)) {
            y_vy[i] = -700; y_hop[i] = 0;
        }
        ent_move(i, 1);
        if (!dying && player_overlap(i)) player_die();
        if (!ent_grounded(i)) y_frame[i] = (y_vx[i] > 0) ? 4 : 5;
        else y_frame[i] = ((y_vx[i] > 0) ? 0 : 2) + ((y_hop[i] >> 3) & 1);
        break;

    case ET_BUTLER:
        /* patrols; turns at walls and ledges; shoves keen */
        if (ent_grounded(i) && ent_at_edge(i, y_vx[i] > 0))
            y_vx[i] = -y_vx[i];
        ent_move(i, 1);
        if (!dying && player_overlap(i)) {
            vx = (px < y_px[i]) ? -700 : 700;
            sfx_play(SND_YORPBUMP);
        }
        y_hop[i]++;
        y_frame[i] = ((y_vx[i] > 0) ? 0 : 2) + ((y_hop[i] >> 3) & 1);
        break;

    case ET_TANK:
        /* patrols; stops to fire at keen; bulletproof */
        if (y_t[i] > 100 && y_t[i] < 140) {      /* stopped, aiming */
            y_vx[i] = 0;
            if (y_t[i] == 120) {
                unsigned char right = (px > y_px[i]);
                spawn_proj(2, y_px[i] + (right ? 14 : -14), y_py[i] + 10,
                           right ? 512 : -512, 0);
                sfx_play(SND_TANKFIRE);
                y_hop[i] = right ? 1 : 0;
            }
        } else {
            if (!y_vx[i]) y_vx[i] = (y_hop[i] & 1) ? 96 : -96;
            if (ent_grounded(i) && ent_at_edge(i, y_vx[i] > 0))
                y_vx[i] = -y_vx[i];
        }
        if (++y_t[i] > 140) y_t[i] = 0;
        ent_move(i, 1);
        if (!dying && player_overlap(i)) {
            vx = (px < y_px[i]) ? -700 : 700;
            sfx_play(SND_YORPBUMP);
        }
        y_hop[i] = (y_vx[i] > 0) ? 1 : ((y_vx[i] < 0) ? 0 : y_hop[i]);
        y_frame[i] = ((y_hop[i] & 1) ? 0 : 2) + ((unsigned char)y_px[i] >> 3 & 1);
        break;
    }
}

/* projectiles: 1 = ice chunk (stuns), 2 = enemy ray (kills) */
static void proj_update(void) {
    unsigned char i;
    for (i = 0; i < 4; i++) {
        int nx, ny;
        if (!pj_on[i]) continue;
        nx = pj_x[i] + (pj_vx[i] >> 8);
        ny = pj_y[i] + (pj_vy[i] >> 8);
        pj_vx[i] += (pj_vx[i] & 0xFF) ? 0 : 0;   /* integer-ish speeds */
        pj_x[i] = nx; pj_y[i] = ny;
        if (nx < (int)cam_x - 32 || nx > (int)cam_x + 288 ||
            ny < -16 || ny > (int)mapPH + 16) { pj_on[i] = 0; continue; }
        if (mflag(nx + 8, ny + 4) & (F_BLKR | F_BLKL)) { pj_on[i] = 0; continue; }
        if (!dying &&
            nx + 12 >= px + 3 && nx + 4 <= px + 12 &&
            ny + 8  >= py + 2 && ny     <= py + 22) {
            if (pj_on[i] == 1) { if (!stun_t) { stun_t = 250; pogoing = 0; jprep = 0; } sfx_play(SND_KEENCICLE); }
            else player_die();
            pj_on[i] = 0;
        }
    }
}

/* ------------------------------------------------------------ sprite draw -- */
/* SMSlib's sprite buffers (global in the lib, just not in the header).
   Format per SMS_addSprite_f: Y[i] = y-1, XN[2i] = x, XN[2i+1] = tile.
   Writing them directly skips six function calls per entity. Safe here
   because the fast path's y is at most 168+16+... < 0xD1 (the one value
   addSprite must reject) and we bounds-check SpriteNextFree ourselves. */
extern unsigned char SpriteTableY[64];
extern unsigned char SpriteTableXN[128];
extern unsigned char SpriteNextFree;

static void draw_16x24(int sx, int sy, unsigned char base) {
    if ((unsigned int)sx <= 240 && (unsigned int)sy <= 168) {
        /* fully on-screen: write the sprite tables directly */
        unsigned char x = (unsigned char)sx, y1 = (unsigned char)sy - 1;
        unsigned char n = SpriteNextFree;
        unsigned char *ty; unsigned char *xn;
        if (n > 58) return;
        ty = SpriteTableY + n;
        xn = SpriteTableXN + ((unsigned char)(n << 1));
        ty[0] = y1;      ty[1] = y1;
        ty[2] = y1 + 8;  ty[3] = y1 + 8;
        ty[4] = y1 + 16; ty[5] = y1 + 16;
        xn[0] = x;     xn[1]  = base;
        xn[2] = x + 8; xn[3]  = base + 1;
        xn[4] = x;     xn[5]  = base + 2;
        xn[6] = x + 8; xn[7]  = base + 3;
        xn[8] = x;     xn[9]  = base + 4;
        xn[10] = x + 8; xn[11] = base + 5;
        SpriteNextFree = n + 6;
        return;
    }
    SMS_addSpriteClipping(sx,     sy,      base);
    SMS_addSpriteClipping(sx + 8, sy,      base + 1);
    SMS_addSpriteClipping(sx,     sy + 8,  base + 2);
    SMS_addSpriteClipping(sx + 8, sy + 8,  base + 3);
    SMS_addSpriteClipping(sx,     sy + 16, base + 4);
    SMS_addSpriteClipping(sx + 8, sy + 16, base + 5);
}

/* 24x32 body (garg, vorticon): 12 hardware sprites, row-major 3x4 */
static void draw_24x32(int sx, int sy, unsigned char base) {
    unsigned char r, c;
    for (r = 0; r < 4; r++)
        for (c = 0; c < 3; c++)
            SMS_addSpriteClipping(sx + c * 8, sy + r * 8, base + r * 3 + c);
}

static unsigned char upload_slot;               /* slot needing stream, or 0xFF */
static unsigned char upload_part;
static unsigned char upload_frame;              /* (type<<4)|frame key */
static unsigned char upload_ent;

/* per-type sprite sheet base (bank2 array) resolved at upload time */
static const unsigned char * const ent_art[5] =
    { 0, 0, 0, 0, 0 };                 /* filled in main() (banked consts) */
#define ET_BIG(t) ((t) == ET_GARG || (t) == ET_VORT)
static const unsigned char *ent_art_ptr(unsigned char t) {
    switch (t) {
    case ET_YORP:   return spr_yorp;
    case ET_GARG:   return spr_garg;
    case ET_VORT:   return spr_vort;
    case ET_BUTLER: return spr_butler;
    default:        return spr_tank;
    }
}

static void build_sprites_level(void) {
    unsigned char i, s, k;
    SMS_initSprites();
    /* player */
    draw_16x24(px - (int)cam_x, py - (int)cam_y, (unsigned char)(VT_PLAYER - 256));

    /* entities: acquire/release streaming slots by visibility.
       Only the near list is walked; rebuild_near releases the slots of
       entities that dropped out of it. */
    upload_slot = 0xFF;
    for (k = 0; k < n_near; k++) {
        int sx, sy;
        unsigned char vis, key;
        i = near_list[k];
        if (e_type[i] >= ET_CANNON0) continue;
        sx = y_px[i] - (int)cam_x;
        if (sx <= -16 || sx >= 256) {            /* x-invisible: skip sy math */
            if (y_slot[i] != 0xFF) { slot_owner[y_slot[i]] = 0xFF; y_slot[i] = 0xFF; }
            continue;
        }
        sy = y_py[i] - (int)cam_y;
        vis = (sy > -24 && sy < 192);
        if (!vis) {
            if (y_slot[i] != 0xFF) { slot_owner[y_slot[i]] = 0xFF; y_slot[i] = 0xFF; }
            continue;
        }
        if (y_slot[i] == 0xFF) {
            for (s = 0; s < N_SLOTS; s++)
                if (slot_owner[s] == 0xFF) {
                    slot_owner[s] = i; y_slot[i] = s; slot_vframe[s] = 0xFE;
                    break;
                }
            if (y_slot[i] == 0xFF && y_state[i] < 3) {
                /* no free slot: a live enemy may steal a corpse's slot */
                for (s = 0; s < N_SLOTS; s++)
                    if (slot_owner[s] != 0xFF && y_state[slot_owner[s]] == 3) {
                        y_slot[slot_owner[s]] = 0xFF;
                        slot_owner[s] = i; y_slot[i] = s; slot_vframe[s] = 0xFE;
                        break;
                    }
            }
            if (y_slot[i] == 0xFF) continue;     /* still none: skip */
        }
        s = y_slot[i];
        key = (e_type[i] << 4) | y_frame[i];
        if (slot_vframe[s] != key && upload_slot == 0xFF) {
            if (slot_part[s] && slot_pkey[s] != key) slot_part[s] = 0;
            upload_slot = s; upload_frame = key; upload_ent = i;
            upload_part = slot_part[s];
        }
        if (slot_vframe[s] != 0xFE) {
            if (ET_BIG(e_type[i]))               /* 24x32, bottom-anchored */
                draw_24x32(sx - 4, sy - 8, (unsigned char)(VT_SLOT0 - 256 + s * 12));
            else
                draw_16x24(sx, sy, (unsigned char)(VT_SLOT0 - 256 + s * 12));
        }
    }
    /* keen's zap */
    if (b_active) {
        int sx = b_px - (int)cam_x, sy = b_py - (int)cam_y;
        unsigned char base = (unsigned char)(VT_BULLET - 256)
                           + ((b_hit ? (1 + (rng & 1)) : 0) << 2);
        SMS_addSpriteClipping(sx,     sy,     base);
        SMS_addSpriteClipping(sx + 8, sy,     base + 1);
        SMS_addSpriteClipping(sx,     sy + 8, base + 2);
        SMS_addSpriteClipping(sx + 8, sy + 8, base + 3);
    }
    /* projectiles */
    for (i = 0; i < 4; i++) {
        int sx, sy;
        if (!pj_on[i]) continue;
        sx = pj_x[i] - (int)cam_x; sy = pj_y[i] - (int)cam_y;
        if (pj_on[i] == 1) {                      /* ice chunk 16x16 */
            SMS_addSpriteClipping(sx,     sy,     (unsigned char)(VT_CHUNK - 256));
            SMS_addSpriteClipping(sx + 8, sy,     (unsigned char)(VT_CHUNK - 256 + 1));
            SMS_addSpriteClipping(sx,     sy + 8, (unsigned char)(VT_CHUNK - 256 + 2));
            SMS_addSpriteClipping(sx + 8, sy + 8, (unsigned char)(VT_CHUNK - 256 + 3));
        } else {                                   /* enemy ray 16x8 */
            SMS_addSpriteClipping(sx,     sy, (unsigned char)(VT_ERAY - 256));
            SMS_addSpriteClipping(sx + 8, sy, (unsigned char)(VT_ERAY - 256 + 1));
        }
    }
}

/* ------------------------------------------------------------- level loop -- */
static void run_level(void) {
    unsigned int ks, kp;

    load_level(cur_level);
    prev_ks = SMS_getKeysStatus();       /* suppress buttons held on entry */
    jump_buf = 0;

    while (game_state == STATE_LEVEL) {
        SMS_waitForVBlank();

        /* --- VBlank window (~70 lines, ~16k cycles). The UNSAFE_*
           OUTI bursts MUST finish inside it: past the end of VBlank the
           VDP drops over-fast writes (corrupted SAT positions / sprite
           art). So they go first, worst-case ~7k cycles. The strip blits
           (precomputed in RAM at the end of the previous frame) follow:
           the entering column has to land in the same VBlank that moves
           the scroll register or 1-3 stale pixels show at the right
           edge; with the left-column blank, a swap inside VBlank is
           invisible at both edges. Blits are plain paced writes, so even
           an extreme spill cannot corrupt anything. --- */
        UNSAFE_SMS_copySpritestoSAT();
        SMS_setBGScrollX((unsigned char)(0 - cam_x));
        SMS_setBGScrollY(scroll_y);

        map2(BANK_SPRITES);
        if (col_ready && row_ready)
            upload_slot = 0xFF;   /* diagonal-crossing frame: both strips
                                     blit this vblank, defer art streaming
                                     (build_sprites re-requests it) */
        else if (pframe != cur_pframe) {
            UNSAFE_SMS_loadNTiles(spr_keen + (unsigned int)pframe * 192, VT_PLAYER, 6);
            cur_pframe = pframe;
            upload_slot = 0xFF;   /* one 192B burst per frame */
        }
        else if (upload_slot == 0xFF && ab_n) anim_upload();
        if (upload_slot != 0xFF) {
            unsigned char ut = upload_frame >> 4;
            if (ET_BIG(ut)) {
                map2(BANK_SPR2);
                UNSAFE_SMS_loadNTiles(ent_art_ptr(ut)
                                        + (unsigned int)(upload_frame & 15) * 384
                                        + (unsigned int)upload_part * 192,
                                      VT_SLOT0 + upload_slot * 12 + upload_part * 6, 6);
                if (upload_part == 0) {
                    slot_part[upload_slot] = 1; slot_pkey[upload_slot] = upload_frame;
                } else {
                    slot_part[upload_slot] = 0;
                    slot_vframe[upload_slot] = upload_frame;
                }
            } else {
                UNSAFE_SMS_loadNTiles(ent_art_ptr(ut)
                                        + (unsigned int)(upload_frame & 15) * 192,
                                      VT_SLOT0 + upload_slot * 12, 6);
                slot_vframe[upload_slot] = upload_frame;
            }
            upload_slot = 0xFF;
        }
        map2(ld->map_bank);
        if (col_ready) { blit_col(); col_ready = 0; }
        if (row_ready) { blit_row(); row_ready = 0; }

        if (SMS_queryPauseRequested()) { SMS_resetPauseRequest(); show_status_box(); }
        ks = SMS_getKeysStatus();
        kp = ks & ~prev_ks;
        prev_ks = ks;

        player_update(ks, kp);
        bullet_update();
        proj_update();
        {
            unsigned char k;
            frame_ct++;
            cam_pg = (unsigned char)(((unsigned int)cam_x) >> 8);
            if (cam_pg != near_pg || ++near_timer >= 8) {
                near_pg = cam_pg; near_timer = 0;
                rebuild_near();
            }
            for (k = 0; k < n_near; k++) ent_update(near_list[k]);
            anim_scan();
        }
        sfx_update();

        if (!dying)
            camera_follow(px + 8 - 124, py + 12 - 92);
        /* bake queued strips into RAM now; next VBlank just blits them */
        if (pend_col != 0xFFFF) { render_col(pend_col); pend_col = 0xFFFF; }
        if (pend_row != 0xFFFF) { render_row(pend_row); pend_row = 0xFFFF; }

        build_sprites_level();
    }
    psg_tone_off(); psg_noise_off();
}

/* -------------------------------------------------------------- overworld -- */
static unsigned char ow_dir;            /* 0=down 1=right 2=up 3=left */
static unsigned char ow_moving;

static void build_sprites_ow(void) {
    static const unsigned char stand_f[4] = { 4, 0, 12, 8 };
    unsigned char f;
    int sx = px - (int)cam_x, sy = py - (int)cam_y;
    if (ow_moving) {
        static const unsigned char walk_f[4] = { 4, 0, 12, 8 };
        f = walk_f[ow_dir] + ((anim_t >> 3) & 3);
    } else f = stand_f[ow_dir];
    pframe = f;
    SMS_initSprites();
    SMS_addSpriteClipping(sx,     sy,     (unsigned char)(VT_OWKEEN - 256));
    SMS_addSpriteClipping(sx + 8, sy,     (unsigned char)(VT_OWKEEN - 256 + 1));
    SMS_addSpriteClipping(sx,     sy + 8, (unsigned char)(VT_OWKEEN - 256 + 2));
    SMS_addSpriteClipping(sx + 8, sy + 8, (unsigned char)(VT_OWKEEN - 256 + 3));
}

/* map pause menu: Status / Save Game.  Returns 1 when the screen was
   handed to the save overlay (the map must be reloaded). */
static void call_ovl(unsigned char bank, void (*fn)(void));
static unsigned char ow_pause_menu(void) {
    unsigned char sel = 0, i, done = 0, go = 0;
    unsigned int kp, ks;
    dlg_tiles_msg();
    dlg_box(13, 4, t_f0);
    dlg_str(2, 1, "Status", t_f0);
    dlg_str(2, 2, "Save Game", t_f0);
    dlg_str(2, 3, "Resume", t_f0);
    prev_ks = SMS_getKeysStatus();
    while (!done) {
        for (i = 0; i < 3; i++) dlg_put(1, 1 + i, t_f0 + (i == sel ? '>' - 32 : 0));
        SMS_waitForVBlank(); sfx_update();
        ks = SMS_getKeysStatus(); kp = ks & ~prev_ks; prev_ks = ks;
        if (kp & PORT_A_KEY_UP)   sel = sel ? sel - 1 : 2;
        if (kp & PORT_A_KEY_DOWN) sel = sel < 2 ? sel + 1 : 0;
        if (kp & PORT_A_KEY_2) { sel = 2; done = 1; }
        if (kp & PORT_A_KEY_1) done = 1;
    }
    dlg_close();
    if (sel == 0) show_status_box();
    else if (sel == 1) {
        ow_px = px; ow_py = py;
        call_ovl(BANK_OVL_FRONT, ovl_save);
        go = 1;
    }
    return go;
}

/* a solid probe point is forgiven if its cell is a gate whose level is done */
static unsigned char ow_blocked(int nx, int ny) {
    /* 12x14 box inside the 16x16 frame */
    return ((mflag(nx + 2,  ny + 2)  | mflag(nx + 13, ny + 2) |
             mflag(nx + 2,  ny + 15) | mflag(nx + 13, ny + 15)) & (F_BLKR | F_BLKL)) ? 1 : 0;
}

static unsigned char ow_cx, ow_cy;       /* cell Keen is standing on */

/* snap the camera to (tx,ty) and redraw the whole screen */
static void cam_warp(int tx, int ty) {
    cam_x = cam_y = 0; scroll_y = 0;
    camera_follow(tx, ty);
    cam_c8 = cam_x >> 3; cam_r8 = cam_y >> 3;
    set_scroll_y_full();
    full_redraw();
    SMS_setBGScrollX((unsigned char)(0 - cam_x));
    SMS_setBGScrollY(scroll_y);
}

/* put Keen on a map position, snap the camera and redraw the screen */
static void ow_warp(int nx, int ny) {
    px = nx; py = ny;
    if (px > (int)mapPW - 16) px = (int)mapPW - 16;
    if (py > (int)mapPH - 16) py = (int)mapPH - 16;
    cam_warp(px + 8 - 124, py + 8 - 92);
    ow_cx = (unsigned char)((unsigned int)(px + 8) >> 4);
    ow_cy = (unsigned char)((unsigned int)(py + 8) >> 4);
}

static void run_overworld(void) {
    unsigned int ks, kp;
    unsigned char i;

    if (inv_parts == 0x0F) { game_state = STATE_WIN; return; }   /* all four parts: home! */
    cur_level = 0;
    load_level(0);
    ow_dir = 0; ow_moving = 0; cur_pframe = 0xFF;
    prev_ks = SMS_getKeysStatus();
    ow_cx = (unsigned char)((unsigned int)(px + 8) >> 4);
    ow_cy = (unsigned char)((unsigned int)(py + 8) >> 4);
    if (tele_pending) {                        /* came out of the in-level teleporter */
        tele_pending = 0;
        ow_warp((int)TELE_DX << 4, (int)TELE_DY << 4);
        sfx_play(SND_TELEPORT);
    } else if (ow_px >= 0) {
        ow_warp(ow_px, ow_py);                 /* back where Keen entered the level */
    }
    if (show_lives) {
        show_lives = 0;
        build_sprites_ow();
        show_keens_left();
    }

    while (game_state == STATE_OW) {
        SMS_waitForVBlank();
        /* UNSAFE bursts first, precomputed strip blits after
           (see run_level for the rationale) */
        UNSAFE_SMS_copySpritestoSAT();
        SMS_setBGScrollX((unsigned char)(0 - cam_x));
        SMS_setBGScrollY(scroll_y);

        map2(BANK_SPRITES);
        if (pframe != cur_pframe) {
            UNSAFE_SMS_loadNTiles(spr_owk + (unsigned int)pframe * 128, VT_OWKEEN, 4);
            cur_pframe = pframe;
        } else if (ab_n) anim_upload();
        map2(ld->map_bank);
        if (col_ready) { blit_col(); col_ready = 0; }
        if (row_ready) { blit_row(); row_ready = 0; }

        if (SMS_queryPauseRequested()) {
            SMS_resetPauseRequest();
            if (ow_pause_menu()) return;         /* map reloads on re-entry */
        }
        ks = SMS_getKeysStatus();
        kp = ks & ~prev_ks;
        prev_ks = ks;

        ow_moving = 0;
        if (ks & PORT_A_KEY_LEFT)  { if (!ow_blocked(px - 2, py)) px -= 2; ow_dir = 3; ow_moving = 1; }
        else if (ks & PORT_A_KEY_RIGHT) { if (!ow_blocked(px + 2, py)) px += 2; ow_dir = 1; ow_moving = 1; }
        if (ks & PORT_A_KEY_UP)    { if (!ow_blocked(px, py - 2)) py -= 2; ow_dir = 2; ow_moving = 1; }
        else if (ks & PORT_A_KEY_DOWN) { if (!ow_blocked(px, py + 2)) py += 2; ow_dir = 0; ow_moving = 1; }
        if (ow_moving) {
            anim_t++;
            if (!(anim_t & 7)) sfx_play(SND_WLDWALK);
        }
        if (px < 0) px = 0; if (py < 0) py = 0;
        if (px > (int)mapPW - 16) px = (int)mapPW - 16;
        if (py > (int)mapPH - 16) py = (int)mapPH - 16;

        /* stepping onto a new cell: teleporter pad, level entrance or the ship
           (the original triggers on entering the object's cell, no button) */
        {
            unsigned char pcx = (unsigned char)((unsigned int)(px + 8) >> 4);
            unsigned char pcy = (unsigned char)((unsigned int)(py + 8) >> 4);
            if (pcx != ow_cx || pcy != ow_cy) {
                ow_cx = pcx; ow_cy = pcy;
                for (i = 0; i < n_teleports; i++) {
                    if (tp_sx[i] != pcx || tp_sy[i] != pcy) continue;
                    sfx_play(SND_TELEPORT);
                    wait_frames(20);
                    ow_warp((int)tp_dx[i] << 4, (int)tp_dy[i] << 4);
                    break;
                }
                for (i = 0; i < n_entries; i++) {
                    if (en_mx[i] != pcx || en_my[i] != pcy) continue;
                    if (en_lvl[i] == 100) {                 /* the ship */
                        show_ship_box();
                        ow_moving = 0; build_sprites_ow();
                    } else if (!level_done[en_lvl[i]]) {
                        sfx_play(SND_WLDENTER);
                        wait_frames(30);
                        cur_level = en_lvl[i];
                        ow_px = px; ow_py = py;
                        game_state = STATE_LEVEL;
                    }
                    break;
                }
            }
        }

        sfx_update();
        camera_follow(px + 8 - 124, py + 8 - 92);
        if (pend_col != 0xFFFF) { render_col(pend_col); pend_col = 0xFFFF; }
        if (pend_row != 0xFFFF) { render_row(pend_row); pend_row = 0xFFFF; }
        frame_ct++;
        anim_scan();
        build_sprites_ow();
    }
    psg_tone_off(); psg_noise_off();
}

/* ------------------------------------------------- bank / cart-RAM helpers -- */
/* (shared with the code overlays, which live in slot 2 themselves) */
void bank_copy(unsigned char bank, const unsigned char *src, unsigned char *dst, unsigned int n) {
    unsigned char ob = b2_shadow;
    SMS_mapROMBank(bank);
    while (n--) *dst++ = *src++;
    SMS_mapROMBank(ob);
}

void bank_tiles(unsigned char bank, const unsigned char *src, unsigned int tile, unsigned int ntiles) {
    unsigned char ob = b2_shadow;
    SMS_mapROMBank(bank);
    SMS_loadTiles(src, tile, ntiles * 32);
    SMS_mapROMBank(ob);
}

/* cartridge RAM replaces slot 2 while enabled, so this has to run from the
   fixed ROM */
void sram_rw(unsigned char write, unsigned int off, unsigned char *buf, unsigned char n) {
    unsigned char *m = (unsigned char *)(0x8000u + off);
    SMS_enableSRAM();
    if (write) while (n--) *m++ = *buf++;
    else       while (n--) *buf++ = *m++;
    SMS_disableSRAM();
}

static void call_ovl(unsigned char bank, void (*fn)(void)) {
    unsigned char ob = b2_shadow;
    map2(bank);
    fn();
    map2(ob);
}

void new_game(void) {
    unsigned char i;
    score_hi = score_lo = 0; lives = 3; next_life = 2; ow_px = -1; tele_pending = 0;
    inv_parts = 0; inv_ammo = 0; inv_pogo = 0; inv_keys = 0;
    for (i = 0; i < NLEVELS; i++) level_done[i] = 0;
    game_state = STATE_OW;
}

/* ---------------------------------------------------------------- ending -- */
/* CShipFlySys: the original's scripted flights over LEVEL81.  Speeds are
   its units (SHIPSPD 16 = 1/2 px per tick) in 8.8; each command lasts
   t+1 ticks, instant commands one tick -- exactly its queue semantics. */
#define SC_MOVE   0
#define SC_WAIT   1
#define SC_MARK   2
#define SC_SCROLL 3
#define SC_FADE   4
#define SC_END    5
typedef struct { unsigned char cmd, arg; unsigned int t; } ShipCmd;
/* MOVE args: 0 up, 1 down, 2 left, 3 right, 4 down-right, 5 up-left */
static const int ship_dx[6] = { 0, 0, -128, 128, 256, -256 };
static const int ship_dy[6] = { -128, 64, 0, 0, 102, -102 };
static const ShipCmd ship_a[] = {            /* Mars -> Earth (EP1_ESEQ) */
    { SC_MOVE, 0, 60 }, { SC_WAIT, 0, 12 }, { SC_MOVE, 4, 673 }, { SC_WAIT, 0, 25 },
    { SC_MOVE, 1, 120 }, { SC_WAIT, 0, 18 }, { SC_MARK, 1, 0 }, { SC_SCROLL, 0, 0 },
    { SC_WAIT, 0, 88 }, { SC_MARK, 0, 0 }, { SC_WAIT, 0, 12 }, { SC_MOVE, 2, 175 },
    { SC_WAIT, 0, 38 }, { SC_MARK, 2, 0 }, { SC_WAIT, 0, 125 }, { SC_MARK, 0, 0 },
    { SC_WAIT, 0, 12 }, { SC_MOVE, 3, 175 }, { SC_WAIT, 0, 6 }, { SC_SCROLL, 1, 0 },
    { SC_MOVE, 1, 116 }, { SC_FADE, 0, 0 } };
static const ShipCmd ship_b[] = {            /* Earth -> Vorticon mothership */
    { SC_MOVE, 0, 58 }, { SC_SCROLL, 0, 0 }, { SC_WAIT, 0, 13 }, { SC_MOVE, 2, 208 },
    { SC_FADE, 0, 0 } };

static void pal_restore(void) {
    SMS_loadBGPalette(bg_palette);
    SMS_loadSpritePalette(spr_palette);
}

static void fade_out(void) {
    unsigned char k, i, c, r, g, b;
    for (k = 1; k <= 3; k++) {
        for (i = 0; i < 16; i++) {
            c = bg_palette[i]; r = c & 3; g = (c >> 2) & 3; b = (c >> 4) & 3;
            r = r > k ? r - k : 0; g = g > k ? g - k : 0; b = b > k ? b - k : 0;
            SMS_setBGPaletteColor(i, r | (g << 2) | (b << 4));
            c = spr_palette[i]; r = c & 3; g = (c >> 2) & 3; b = (c >> 4) & 3;
            r = r > k ? r - k : 0; g = g > k ? g - k : 0; b = b > k ? b - k : 0;
            SMS_setSpritePaletteColor(i, r | (g << 2) | (b << 4));
        }
        wait_frames(8);
    }
    wait_frames(20);
}

static void run_shipfly(const ShipCmd *sc, int sx, int sy, int camx, int camy) {
    unsigned char scroll_on = 1, mark = 0, left = 0, ci = 0, base;
    unsigned int t = sc->t;
    int fx = 0, fy = 0;
    load_level(LVL_FLY);
    px = sx; py = sy;
    cam_warp(camx, camy);
    for (;;) {
        SMS_waitForVBlank();
        UNSAFE_SMS_copySpritestoSAT();
        SMS_setBGScrollX((unsigned char)(0 - cam_x));
        SMS_setBGScrollY(scroll_y);
        map2(ld->map_bank);
        if (col_ready) { blit_col(); col_ready = 0; }
        if (row_ready) { blit_row(); row_ready = 0; }

        switch (sc[ci].cmd) {
        case SC_MOVE: {
            unsigned char d = sc[ci].arg;
            fx += ship_dx[d]; fy += ship_dy[d];
            px += fx >> 8; fx &= 255;
            py += fy >> 8; fy &= 255;
            left = (d == 2 || d == 5);
            break; }
        case SC_MARK:   mark = sc[ci].arg; break;
        case SC_SCROLL: scroll_on = sc[ci].arg; break;
        case SC_FADE:   fade_out(); SMS_initSprites(); SMS_copySpritestoSAT(); return;
        }
        if (t) t--; else { ci++; t = sc[ci].t; }
        sfx_update();
        if (scroll_on) camera_follow(px + 8 - 124, py + 4 - 92);
        if (pend_col != 0xFFFF) { render_col(pend_col); pend_col = 0xFFFF; }
        if (pend_row != 0xFFFF) { render_row(pend_row); pend_row = 0xFFFF; }

        base = (unsigned char)(VT_SHIP - 256) + (left ? 2 : 0);
        SMS_initSprites();
        SMS_addSpriteClipping(px - (int)cam_x,     py - (int)cam_y, base);
        SMS_addSpriteClipping(px - (int)cam_x + 8, py - (int)cam_y, base + 1);
        if (mark) {
            base = (unsigned char)(VT_SHIP - 256) + 2 + mark * 2;
            SMS_addSpriteClipping(px - (int)cam_x + 4, py - (int)cam_y - 17, base);
            SMS_addSpriteClipping(px - (int)cam_x + 4, py - (int)cam_y - 9,  base + 1);
        }
    }
}

/* the whole ending, in CEndingEp1's order */
static void run_win(void) {
    unsigned char i;
    /* Keen at his ship on Mars */
    cur_level = 0;
    load_level(0);
    for (i = 0; i < n_entries; i++)
        if (en_lvl[i] == 100) { ow_warp((int)en_mx[i] << 4, (int)en_my[i] << 4); break; }
    ow_dir = 3; ow_moving = 0; cur_pframe = 0xFF;
    build_sprites_ow();
    wait_frames(30);
    show_text_box(TXT_E1);
    dlg_wait(); dlg_close();
    run_shipfly(ship_a, 6 * 16, 5 * 16, 0, 0);
    pal_restore();
    call_ovl(BANK_OVL_FRONT, ovl_end_scene);
    pal_restore();
    run_shipfly(ship_b, 48 * 16, 23 * 16, 48 * 16 - 100, 23 * 16 - 160);
    pal_restore();
    call_ovl(BANK_OVL_FRONT, ovl_ending_text);
    game_state = STATE_TITLE;
}

/* Keen finished dying: back to Mars, or game over when out of lives */
static void run_dead(void) {
    psg_tone_off(); psg_noise_off();
    if (lives == 0) {
        call_ovl(BANK_OVL_FRONT, ovl_gameover);
    } else {
        show_lives = 1;
        game_state = STATE_OW;
    }
}

static void run_title(void) {
    call_ovl(BANK_OVL_FRONT, ovl_title);
}

/* ------------------------------------------------------------------- main -- */
void main(void) {
    PSGPort = 0x9F; PSGPort = 0xBF; PSGPort = 0xDF; PSGPort = 0xFF;
    SMS_setSpriteMode(SPRITEMODE_NORMAL);
    SMS_useFirstHalfTilesforSprites(0);
    SMS_VDPturnOnFeature(VDPFEATURE_LEFTCOLBLANK);
    SMS_loadBGPalette(bg_palette);
    SMS_loadSpritePalette(spr_palette);
    SMS_setBackdropColor(0);

    game_state = STATE_TITLE;
    for (;;) {
        switch (game_state) {
        case STATE_TITLE: run_title();     break;
        case STATE_OW:    run_overworld(); break;
        case STATE_LEVEL: run_level();     break;
        case STATE_WIN:   run_win();       break;
        case STATE_DEAD:  run_dead();      break;
        }
    }
}

SMS_EMBED_SEGA_ROM_HEADER(9999, 0);
SMS_EMBED_SDSC_HEADER_AUTO_DATE(1, 0,
    "keen-sms",
    "Commander Keen SMS",
    "Port of HTML5-Keen (melonJS) to the Sega Master System via devkitSMS");
