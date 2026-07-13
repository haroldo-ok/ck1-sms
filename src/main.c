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

__sfr __at 0x7F PSGPort;

/* ---------------------------------------------------------------- consts -- */
#define STATE_TITLE  0
#define STATE_OW     1
#define STATE_LEVEL  2
#define STATE_WIN    3

/* VRAM sprite tile slots (absolute tile numbers; pass n-256 to addSprite) */
#define VT_PLAYER   256            /* 6 tiles, streamed every frame          */
#define VT_SLOT0    262            /* 8 x 6 tiles, entity streaming slots    */
#define VT_BULLET   312            /* 12 tiles, loaded once per level        */
#define VT_ERAY     324            /* 2 tiles, enemy ray                     */
#define VT_CHUNK    326            /* 4 tiles, ice chunk                     */
#define VT_OWKEEN   444            /* 4 tiles, overworld player              */
#define N_SLOTS     8

/* physics, 8.8 fixed point (units: 1/256 px per frame) */
#define WALK_ACC     96
#define WALK_MAX    576
#define AIR_ACC      96
#define GRAV         38
#define JUMP_V    (-1024)
#define FALL_MAX   1024
#define POGO_STEER   77
#define BULLET_V    640

#define MAX_ENT     30
#define MAX_OVR    200
#define MAX_DOORS    8
#define MAX_ENTRIES 48
#define MAX_EXITS    4
#define NLEVELS     17

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
#define KF_POGO_DN_R 4
#define KF_POGO_UP_R 5
#define KF_STAND_L  6
#define KF_WALK_L   7     /* 7..9 */
#define KF_POGO_DN_L 10
#define KF_POGO_UP_L 11
#define KF_FALL_R   17
#define KF_FALL_L   23
#define KF_SHOOT_R  24
#define KF_SHOOT_L  25
#define KF_DIE0     26
#define KF_DIE1     27

/* yorp frames */
#define YF_LOOK0    1
#define YF_WALK_L0  4
#define YF_WALK_R0  6
#define YF_CRY0     8
#define YF_DIE      10
#define YF_DEAD     11

/* ---------------------------------------------------------------- state --- */
static const LevelDesc *ld;
unsigned char cur_level;                /* 0=overworld 1..3 */
unsigned char game_state;
unsigned char level_done[NLEVELS];
static unsigned int  score_hi;          /* score = hi*10000 + lo (BCD-ish)   */
static unsigned int  score_lo;

/* inventory */
static unsigned char inv_ammo, inv_pogo;
static unsigned char inv_keys;          /* bit 0..3 = card Y R G B, per level */
unsigned char inv_parts;                /* bit 0..3 = joystick/battery/vacuum/fuel */
static unsigned char stun_t;            /* icy-chunk stun timer */

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
static int  vx, vy;                     /* 8.8                               */
static int  sx_acc, sy_acc;             /* subpixel accumulators             */
static unsigned char dir_right, on_ground, jumping, pogoing, pogo_squat;
static unsigned char shoot_timer, exiting, dying;
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
static unsigned int prev_ks;
static unsigned char jump_buf;          /* buffered jump press, frames */
#define JUMP_BUF_FRAMES 5

/* ------------------------------------------------------------------ sfx --- */
/* tiny procedural PSG driver: one tone effect (ch2) + one noise (ch3)        */
static unsigned char sfx_id, sfx_t, nfx_id, nfx_t;
#define SFX_NONE 0
#define SFX_JUMP 1
#define SFX_COLLECT 2
#define SFX_KEYCARD 3
#define SFX_DIE 4
#define SFX_EXIT 5
#define SFX_BUMP 6
#define SFX_ZAP 7
#define SFX_ENTER 8
#define SFX_CLICK 9
#define NFX_SHOOT 1
#define NFX_LAND 2

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

static void sfx_play(unsigned char id)  { sfx_id = id; sfx_t = 0; }
static void nfx_play(unsigned char id)  { nfx_id = id; nfx_t = 0; }

static void sfx_update(void) {
    unsigned char t = sfx_t;
    switch (sfx_id) {
    case SFX_JUMP:      /* quick pitch drop, 12 frames */
        if (t >= 12) { psg_tone_off(); sfx_id = 0; break; }
        psg_tone(180 + ((unsigned int)t << 4), t >> 1);
        break;
    case SFX_COLLECT:   /* two rising blips */
        if (t >= 8) { psg_tone_off(); sfx_id = 0; break; }
        psg_tone(t < 4 ? 220 : 165, (t & 3) << 1);
        break;
    case SFX_KEYCARD:   /* 4-step arpeggio */
        if (t >= 16) { psg_tone_off(); sfx_id = 0; break; }
        psg_tone(280 - ((unsigned int)(t >> 2) * 50), 2);
        break;
    case SFX_DIE:       /* long slide down */
        if (t >= 48) { psg_tone_off(); sfx_id = 0; break; }
        psg_tone(150 + ((unsigned int)t * 14), t >> 3);
        break;
    case SFX_EXIT:      /* slide up */
        if (t >= 32) { psg_tone_off(); sfx_id = 0; break; }
        psg_tone(700 - ((unsigned int)t * 20), 2);
        break;
    case SFX_BUMP:
        if (t >= 5) { psg_tone_off(); sfx_id = 0; break; }
        psg_tone(900, 4);
        break;
    case SFX_ZAP:       /* alternating buzz */
        if (t >= 10) { psg_tone_off(); sfx_id = 0; break; }
        psg_tone((t & 1) ? 90 : 140, 3);
        break;
    case SFX_ENTER:     /* rising arpeggio, longer */
        if (t >= 30) { psg_tone_off(); sfx_id = 0; break; }
        psg_tone(500 - ((unsigned int)(t / 6) * 80), 2);
        break;
    case SFX_CLICK:
        if (t >= 2) { psg_tone_off(); sfx_id = 0; break; }
        psg_tone(1000, 6);
        break;
    default: break;
    }
    if (sfx_id) sfx_t++;
    t = nfx_t;
    switch (nfx_id) {
    case NFX_SHOOT:
        if (t >= 7) { psg_noise_off(); nfx_id = 0; break; }
        psg_noise(0x04, t);              /* white noise, fading */
        break;
    case NFX_LAND:
        if (t >= 3) { psg_noise_off(); nfx_id = 0; break; }
        psg_noise(0x06, 5);
        break;
    default: break;
    }
    if (nfx_id) nfx_t++;
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
    if (wx < 0 || wy < 0) return F_SOLID;
    mx = (unsigned int)wx >> 4;
    my = (unsigned int)wy >> 4;
    if (mx >= mapW) return F_SOLID;
    if (my >= mapH) return 0;
    {
        unsigned char f = flags_ram[map_rom[row_off[my] + mx]];
        if ((f & F_SOLID) && n_open_doors &&
            door_open_at((unsigned char)mx, (unsigned char)my))
            return 0;
        return f;
    }
}

/* -------------------------------------------------------------- NT strips -- */
static void draw_col_now(unsigned int wc) {
    unsigned char r, nty, sub;
    unsigned int wr, addr;
    const unsigned char *mrow;
    const unsigned int  *mtsub;
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
        SMS_setAddr(addr);
        SMS_setTile(mtsub[(unsigned int)m << 2]);
        if (++nty == 28) { nty = 0; addr -= 28u * 64u; }
        addr += 64;
        if (wr & 1) { mrow += mapW; mtsub -= 2; }
        else        { mtsub += 2; }
    }
}

static void draw_row_now(unsigned int wr) {
    unsigned char c, ntx, nty, sub;
    unsigned int wc, rowbase;
    const unsigned char *mrow;
    const unsigned int  *mtsub;
    if (wr >= (mapH << 1)) return;
    nty     = (unsigned char)(wr % 28);
    rowbase = SMS_PNTAddress | ((unsigned int)nty << 6);
    wc      = cam_c8 + 1;
    ntx     = wc & 31;
    sub     = (wr & 1) << 1;
    mrow    = map_rom + row_off[wr >> 1];
    mtsub   = mtdef_rom + sub;
    SMS_setAddr(rowbase + ((unsigned int)ntx << 1));
    for (c = 0; c < 32; c++, wc++) {
        unsigned char m;
        if (wc >= (mapW << 1)) break;
        m = mrow[wc >> 1];
        if (n_ovr) m = cell_mt(wc >> 1, wr >> 1);
        SMS_setTile(mtsub[((unsigned int)m << 2) + (wc & 1)]);
        ntx = (ntx + 1) & 31;
        if (!ntx) SMS_setAddr(rowbase);     /* NT wraps to column 0 */
    }
}

static void nt_write_cell(unsigned int wc, unsigned int wr) {
    unsigned char m = cell_mt(wc >> 1, wr >> 1);
    unsigned int t = mtdef_rom[((unsigned int)m << 2) + ((wr & 1) << 1) + (wc & 1)];
    SMS_setTileatXY(wc & 31, wr % 28, t);
}

static void full_redraw(void) {
    unsigned int c;
    pend_col_camr8 = cam_r8;
    for (c = 0; c <= 32; c++)
        draw_col_now(cam_c8 + c);
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

static const unsigned int kind_score[15] =
    { 100, 200, 500, 1000, 5000, 0, 0, 500, 500, 500, 500, 0, 0, 0, 0 };
static void add_score(unsigned char kind) {
    score_lo += kind_score[kind];
    while (score_lo >= 10000) { score_lo -= 10000; score_hi++; }
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
    SMS_mapROMBank(ld->bank);
    SMS_loadTiles(ld->tiles, 0, ld->tiles_size);

    /* sprite art resident per level */
    SMS_mapROMBank(BANK_SPRITES);
    if (lvl) {
        SMS_loadTiles(spr_blt, VT_BULLET, 12 * 32);
        SMS_loadTiles(spr_eray, VT_ERAY, 2 * 32);
        SMS_loadTiles(spr_chunk, VT_CHUNK, 4 * 32);
    }

    /* map-side data */
    SMS_mapROMBank(ld->map_bank);
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
    for (i = 0; i < N_SLOTS; i++) { slot_owner[i] = 0xFF; slot_vframe[i] = 0xFE; }
    for (i = 0; i < 4; i++) pj_on[i] = 0;
    inv_keys = 0; stun_t = 0; n_open_doors = 0;
    n_entries = ld->nentries;
    p = ld->entries;
    for (i = 0; i < n_entries && i < MAX_ENTRIES; i++) {
        en_mx[i] = *p++; en_my[i] = *p++; en_lvl[i] = *p++; en_done[i] = *p++;
        /* completed cities render their "done" art and stop being entries */
        if (level_done[en_lvl[i]])
            ovr_add(en_mx[i], en_my[i], en_done[i]);
    }
    n_teleports = ld->nteleports;
    if (n_teleports > MAX_TELEPORTS) n_teleports = MAX_TELEPORTS;
    p = ld->teleports;
    for (i = 0; i < n_teleports; i++) {
        tp_sx[i] = *p++; tp_sy[i] = *p++; tp_dx[i] = *p++; tp_dy[i] = *p++;
    }

    px = (int)ld->spawn_x; py = (int)ld->spawn_y;
    if (lvl) py -= 8;                      /* sprite is 16x24, tile anchor */
    vx = vy = sx_acc = sy_acc = 0;
    on_ground = jumping = pogoing = pogo_squat = 0;
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
static void wait_frames(unsigned int n) {
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

static void show_status(const char *line1) {
    screen_text_begin();
    print_at(8, 6, line1);
    print_at(8, 10, "SCORE");
    print_num(16, 10, score_hi, score_lo);
    print_at(8, 12, "AMMO");
    numbuf[0] = '0' + inv_ammo / 10; numbuf[1] = '0' + inv_ammo % 10; numbuf[2] = 0;
    print_at(16, 12, numbuf);
    if (inv_pogo) print_at(8, 14, "POGO STICK OK");
    {
        static char pb[] = "SHIP PARTS 0";
        unsigned char n = 0, m = inv_parts;
        while (m) { n += m & 1; m >>= 1; }
        pb[11] = '0' + n;
        if (n) print_at(8, 16, pb);
    }
    SMS_displayOn();
    wait_frames(90);
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
        if (f & F_SOLID) {
            px = (((px + 12 + dx) & ~15) - 13);
            vx = 0; sx_acc = 0;
        } else px += dx;
    } else if (dx < 0) {
        unsigned char f = probe3_x(px + 3 + dx, py);
        if (f & F_SOLID) {
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
        if ((f & F_SOLID) || ((f & F_PLAT) && crossed)) {
            py = ((ny & ~15) - 24);
            vy = 0; sy_acc = 0;
            if (!on_ground) {                   /* landing */
                nfx_play(NFX_LAND);
                fall_snd_on = 0;
            }
            on_ground = 1; jumping = 0;
        } else { py += dy; on_ground = 0; }
    } else if (dy < 0) {
        int ny = py + 1 + dy;                  /* new head */
        unsigned char f = mflag(px + 4, ny) | mflag(px + 11, ny);
        if ((f & F_SOLID) && !(f & F_PLAT)) {
            py = ((((ny >> 4) + 1) << 4) - 1);
            vy = 0; sy_acc = 0;
            sfx_play(SFX_BUMP);
        } else py += dy;
        on_ground = 0;
    } else {
        /* recheck ground under feet */
        int fy = py + 24;
        unsigned char f = mflag(px + 4, fy) | mflag(px + 11, fy);
        on_ground = ((f & F_SOLID) || ((f & F_PLAT) && ((fy & 15) == 0))) ? 1 : 0;
        if (on_ground) jumping = 0;
    }
}

static void player_die(void) {
    if (dying) return;
    dying = 1; seq_timer = 0;
    sfx_play(SFX_DIE);
    vx = vy = 0;
}

static void spawn_bullet(void) {
    if (!inv_ammo) { sfx_play(SFX_CLICK); return; }
    if (b_active) return;
    inv_ammo--;
    b_active = 1; b_hit = 0; b_timer = 0;
    b_right = dir_right;
    b_px = px + (dir_right ? 12 : -12);
    b_py = py + 6;
    nfx_play(NFX_SHOOT);
}

static void player_update(unsigned int ks, unsigned int kp) {
    unsigned char i;

    if (dying) {
        seq_timer++;
        pframe = (seq_timer & 8) ? KF_DIE1 : KF_DIE0;
        if (seq_timer > 40 && seq_timer <= 130) py -= 2;   /* float away */
        if (seq_timer > 130) { game_state = STATE_OW; }
        return;
    }
    if (exiting) {
        seq_timer++;
        vx = 200; dir_right = 1;
        player_move_x();
        anim_t++;
        if (!(anim_t & 7) && ++anim_ph == 3) anim_ph = 0;
        pframe = KF_WALK_R + anim_ph;
        if (seq_timer > 70) {
            level_done[cur_level] = 1;
            game_state = (inv_parts == 0x0F) ? STATE_WIN : STATE_OW;
        }
        return;
    }

    /* icy-chunk stun: Keen can't act for a moment */
    if (stun_t) {
        stun_t--;
        ks = 0; kp = 0;
    }

    /* pogo toggle: DOWN + button 1 */
    if ((kp & PORT_A_KEY_1) && (ks & PORT_A_KEY_DOWN) && inv_pogo) {
        pogoing = !pogoing;
        pogo_squat = 0;
        if (pogoing && on_ground) { vy = JUMP_V; on_ground = 0; jumping = 1; sfx_play(SFX_JUMP); }
    }

    if (pogoing) {
        if (ks & PORT_A_KEY_LEFT)  { vx -= POGO_STEER; dir_right = 0; }
        if (ks & PORT_A_KEY_RIGHT) { vx += POGO_STEER; dir_right = 1; }
        if (vx >  WALK_MAX) vx =  WALK_MAX;
        if (vx < -WALK_MAX) vx = -WALK_MAX;
        if (on_ground) {
            if (!pogo_squat) pogo_squat = 1;
            else if (++pogo_squat > 6) {
                pogo_squat = 0;
                vy = JUMP_V; on_ground = 0; jumping = 1;
                sfx_play(SFX_JUMP);
            }
        }
        pframe = on_ground ? (dir_right ? KF_POGO_DN_R : KF_POGO_DN_L)
                           : (dir_right ? KF_POGO_UP_R : KF_POGO_UP_L);
    } else {
        /* walking */
        if (shoot_timer && on_ground) {
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
            if (on_ground) vx = 0;
            else vx = (vx * 3) >> 2;             /* air drag, like the JS     */
        }
        /* jump (with a small press buffer: a press a few frames before
           landing still triggers the jump on touchdown) */
        if ((kp & PORT_A_KEY_1) && !(ks & PORT_A_KEY_DOWN))
            jump_buf = JUMP_BUF_FRAMES;
        if (jump_buf && on_ground) {
            jump_buf = 0;
            vy = JUMP_V; on_ground = 0; jumping = 1;
            sfx_play(SFX_JUMP);
        }
        if (jump_buf) jump_buf--;
    }

    /* fire */
    if (kp & PORT_A_KEY_2) {
        spawn_bullet();
        shoot_timer = 14;
    }
    if (shoot_timer) shoot_timer--;

    /* gravity */
    if (!on_ground) {
        vy += GRAV;
        if (vy > FALL_MAX) vy = FALL_MAX;
    }

    player_move_x();
    player_move_y();

    /* falling sound trigger */
    if (!on_ground && vy > 512 && !jumping && !fall_snd_on) fall_snd_on = 1;

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
    /* fell out of the world */
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
            it_taken[i] = 1;
            ovr_add(it_mx[i], it_my[i], it_restore[i]);
            nt_update_cell(it_mx[i], it_my[i]);
            add_score(k);
            if (k == K_AMMO)      { inv_ammo += 5; sfx_play(SFX_KEYCARD); }
            else if (k == K_POGO) { inv_pogo = 1;  sfx_play(SFX_KEYCARD); }
            else if (k >= K_CARDY && k <= K_CARDB)
                { inv_keys |= 1 << (k - K_CARDY); sfx_play(SFX_KEYCARD); }
            else if (k >= K_JOYSTICK)
                { inv_parts |= 1 << (k - K_JOYSTICK); sfx_play(SFX_EXIT); }
            else sfx_play(SFX_COLLECT);
        }
    }
    }

    /* exit door cells (tile 159): touch while grounded */
    if (on_ground && !pogoing) {
        unsigned char ecx = (unsigned char)((unsigned int)(px + 8) >> 4);
        unsigned char ecy = (unsigned char)((unsigned int)(py + 12) >> 4);
        for (i = 0; i < n_exits; i++)
            if (ex_mx[i] == ecx && (ex_my[i] == ecy || ex_my[i] == ecy + 1)) {
                exiting = 1; seq_timer = 0;
                sfx_play(SFX_EXIT);
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
                sfx_play(SFX_KEYCARD);
            }
            break;
        }
    }

    /* animation */
    if (!on_ground) {
        pframe = dir_right ? KF_FALL_R : KF_FALL_L;
        if (pogoing) pframe = dir_right ? KF_POGO_UP_R : KF_POGO_UP_L;
    } else if (shoot_timer) {
        pframe = dir_right ? KF_SHOOT_R : KF_SHOOT_L;
    } else if (!pogoing) {
        if (vx) {
            anim_t++;
            if (!(anim_t & 7) && ++anim_ph == 3) anim_ph = 0;
            pframe = (dir_right ? KF_WALK_R : KF_WALK_L) + anim_ph;
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
    if (mflag(b_px + 8, b_py + 4) & F_SOLID) {
        b_hit = 1; b_timer = 0;
        rng = rng * 13 + 7;
        sfx_play(SFX_ZAP);
        return;
    }
    /* entities */
    for (i = 0; i < y_n; i++) {
        if (y_state[i] >= 2 || e_type[i] >= ET_CANNON0) continue;
        if (b_px + 12 >= y_px[i] && b_px + 4 <= y_px[i] + 15 &&
            b_py + 6  >= y_py[i] && b_py + 2 <= y_py[i] + 23) {
            b_hit = 1; b_timer = 0;
            sfx_play(SFX_ZAP);
            if (e_type[i] == ET_TANK || e_type[i] == ET_BUTLER)
                return;                          /* armored: zap absorbed */
            if (y_hp[i] > 1) { y_hp[i]--; return; }
            y_state[i] = 2; y_t[i] = 0;
            add_score(0);
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
        if (f & F_SOLID) { y_vx[i] = -y_vx[i]; } else y_px[i] += d;
    } else if (d < 0) {
        f = mflag(y_px[i] + 2 + d, y_py[i] + 8) | mflag(y_px[i] + 2 + d, y_py[i] + 20);
        if (f & F_SOLID) { y_vx[i] = -y_vx[i]; } else y_px[i] += d;
    }
    y_pg[i] = (unsigned char)(((unsigned int)y_px[i]) >> 8);
    y_sy[i] += y_vy[i];
    d = y_sy[i] >> 8; y_sy[i] -= d << 8;
    if (d > 0) {
        int ny = y_py[i] + 23 + d;
        unsigned char crossed = ((y_py[i] + 23) >> 4) < (ny >> 4);
        f = mflag(y_px[i] + 4, ny) | mflag(y_px[i] + 11, ny);
        if ((f & F_SOLID) || ((f & F_PLAT) && crossed)) {
            y_py[i] = (ny & ~15) - 24; y_vy[i] = 0; y_sy[i] = 0;
        } else y_py[i] += d;
    } else if (d < 0) {
        int ny = y_py[i] + 1 + d;
        f = mflag(y_px[i] + 4, ny) | mflag(y_px[i] + 11, ny);
        if ((f & F_SOLID) && !(f & F_PLAT)) { y_vy[i] = 0; y_sy[i] = 0; }
        else y_py[i] += d;
    }
}

static unsigned char ent_grounded(unsigned char i) {
    return ((mflag(y_px[i] + 4, y_py[i] + 24) |
             mflag(y_px[i] + 11, y_py[i] + 24)) & (F_SOLID | F_PLAT)) ? 1 : 0;
}

/* is a walk in direction dir (0=left 1=right) about to step off a ledge? */
static unsigned char ent_at_edge(unsigned char i, unsigned char right) {
    int ex = right ? (y_px[i] + 15) : y_px[i];
    return !(mflag(ex, y_py[i] + 26) & (F_SOLID | F_PLAT));
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
                sfx_play(SFX_BUMP);
            } else {
                vx = (px < y_px[i]) ? -600 : 600;
                sfx_play(SFX_BUMP);
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
            sfx_play(SFX_BUMP);
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
                nfx_play(NFX_SHOOT);
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
            sfx_play(SFX_BUMP);
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
        if (mflag(nx + 8, ny + 4) & F_SOLID) { pj_on[i] = 0; continue; }
        if (!dying &&
            nx + 12 >= px + 3 && nx + 4 <= px + 12 &&
            ny + 8  >= py + 2 && ny     <= py + 22) {
            if (pj_on[i] == 1) { stun_t = 90; sfx_play(SFX_BUMP); }
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

static unsigned char upload_slot;               /* slot needing stream, or 0xFF */
static unsigned char upload_frame;              /* (type<<4)|frame key */
static unsigned char upload_ent;

/* per-type sprite sheet base (bank2 array) resolved at upload time */
static const unsigned char * const ent_art[5] =
    { 0, 0, 0, 0, 0 };                 /* filled in main() (banked consts) */
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
            upload_slot = s; upload_frame = key; upload_ent = i;
        }
        if (slot_vframe[s] != 0xFE)
            draw_16x24(sx, sy, (unsigned char)(VT_SLOT0 - 256 + s * 6));
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

        /* --- VBlank window (~70 lines, ~16k cycles): everything here
           completes before the raster resumes, so strips drawn now can
           never be seen half-updated. Horizontally the 32-col name table
           has NO off-screen cushion (world col N and N+32 share NT col
           N&31), so the entering column must be in VRAM in the SAME
           vblank that moves the scroll register -- one frame late shows
           1-3 stale pixels at the right edge. Order: strips first, then
           SAT + scroll (cheap, guaranteed), sprite art last. --- */
        SMS_mapROMBank(ld->map_bank);
        if (pend_col != 0xFFFF) { draw_col_now(pend_col); pend_col = 0xFFFF; }
        if (pend_row != 0xFFFF) { draw_row_now(pend_row); pend_row = 0xFFFF; }

        UNSAFE_SMS_copySpritestoSAT();
        SMS_setBGScrollX((unsigned char)(0 - cam_x));
        SMS_setBGScrollY(scroll_y);

        SMS_mapROMBank(BANK_SPRITES);
        if (pframe != cur_pframe) {
            UNSAFE_SMS_loadNTiles(spr_keen + (unsigned int)pframe * 192, VT_PLAYER, 6);
            cur_pframe = pframe;
        }
        if (upload_slot != 0xFF) {
            UNSAFE_SMS_loadNTiles(ent_art_ptr(upload_frame >> 4)
                                    + (unsigned int)(upload_frame & 15) * 192,
                                  VT_SLOT0 + upload_slot * 6, 6);
            slot_vframe[upload_slot] = upload_frame;
            upload_slot = 0xFF;
        }
        SMS_mapROMBank(ld->map_bank);

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
        }
        sfx_update();

        if (!dying)
            camera_follow(px + 8 - 124, py + 12 - 92);

        build_sprites_level();
    }
    psg_tone_off(); psg_noise_off();
}

/* -------------------------------------------------------------- overworld -- */
static unsigned char ow_dir;            /* 0=down 1=right 2=up 3=left */
static unsigned char ow_moving;

static void build_sprites_ow(void) {
    static const unsigned char stand_f[4] = { 0, 6, 8, 15 };
    unsigned char f;
    int sx = px - (int)cam_x, sy = py - (int)cam_y;
    if (ow_moving) {
        static const unsigned char walk_f[4] = { 0, 4, 8, 12 };
        f = walk_f[ow_dir] + ((anim_t >> 3) & 3);
    } else f = stand_f[ow_dir];
    pframe = f;
    SMS_initSprites();
    SMS_addSpriteClipping(sx,     sy,     (unsigned char)(VT_OWKEEN - 256));
    SMS_addSpriteClipping(sx + 8, sy,     (unsigned char)(VT_OWKEEN - 256 + 1));
    SMS_addSpriteClipping(sx,     sy + 8, (unsigned char)(VT_OWKEEN - 256 + 2));
    SMS_addSpriteClipping(sx + 8, sy + 8, (unsigned char)(VT_OWKEEN - 256 + 3));
}

/* a solid probe point is forgiven if its cell is a gate whose level is done */
static unsigned char ow_blocked(int nx, int ny) {
    /* 12x14 box inside the 16x16 frame */
    return ((mflag(nx + 2,  ny + 2)  | mflag(nx + 13, ny + 2) |
             mflag(nx + 2,  ny + 15) | mflag(nx + 13, ny + 15)) & F_SOLID) ? 1 : 0;
}

static void run_overworld(void) {
    unsigned int ks, kp;
    unsigned char i;

    cur_level = 0;
    load_level(0);
    ow_dir = 0; ow_moving = 0; cur_pframe = 0xFF;
    prev_ks = SMS_getKeysStatus();

    while (game_state == STATE_OW) {
        SMS_waitForVBlank();
        /* strips first inside vblank, before the scroll reveals them
           (see run_level for the rationale) */
        SMS_mapROMBank(ld->map_bank);
        if (pend_col != 0xFFFF) { draw_col_now(pend_col); pend_col = 0xFFFF; }
        if (pend_row != 0xFFFF) { draw_row_now(pend_row); pend_row = 0xFFFF; }

        UNSAFE_SMS_copySpritestoSAT();
        SMS_setBGScrollX((unsigned char)(0 - cam_x));
        SMS_setBGScrollY(scroll_y);

        SMS_mapROMBank(BANK_SPRITES);
        if (pframe != cur_pframe) {
            UNSAFE_SMS_loadNTiles(spr_owk + (unsigned int)pframe * 128, VT_OWKEEN, 4);
            cur_pframe = pframe;
        }
        SMS_mapROMBank(ld->map_bank);

        ks = SMS_getKeysStatus();
        kp = ks & ~prev_ks;
        prev_ks = ks;

        ow_moving = 0;
        if (ks & PORT_A_KEY_LEFT)  { if (!ow_blocked(px - 2, py)) px -= 2; ow_dir = 3; ow_moving = 1; }
        else if (ks & PORT_A_KEY_RIGHT) { if (!ow_blocked(px + 2, py)) px += 2; ow_dir = 1; ow_moving = 1; }
        if (ks & PORT_A_KEY_UP)    { if (!ow_blocked(px, py - 2)) py -= 2; ow_dir = 2; ow_moving = 1; }
        else if (ks & PORT_A_KEY_DOWN) { if (!ow_blocked(px, py + 2)) py += 2; ow_dir = 0; ow_moving = 1; }
        if (ow_moving) anim_t++;
        if (px < 0) px = 0; if (py < 0) py = 0;
        if (px > (int)mapPW - 16) px = (int)mapPW - 16;
        if (py > (int)mapPH - 16) py = (int)mapPH - 16;

        /* teleporter? (stand on a teleport pad, press a button) */
        if (kp & (PORT_A_KEY_1 | PORT_A_KEY_2)) {
            unsigned char pcx = (unsigned char)((unsigned int)(px + 8) >> 4);
            unsigned char pcy = (unsigned char)((unsigned int)(py + 8) >> 4);
            for (i = 0; i < n_teleports; i++) {
                if (tp_sx[i] != pcx || tp_sy[i] != pcy) continue;
                sfx_play(SFX_ENTER);
                /* warp: place Keen on the destination cell, snap camera */
                px = (int)tp_dx[i] << 4;
                py = (int)tp_dy[i] << 4;
                if (px > (int)mapPW - 16) px = (int)mapPW - 16;
                if (py > (int)mapPH - 16) py = (int)mapPH - 16;
                cam_x = cam_y = 0; scroll_y = 0;
                camera_follow(px + 8 - 124, py + 8 - 92);
                cam_c8 = cam_x >> 3; cam_r8 = cam_y >> 3;
                set_scroll_y_full();
                full_redraw();
                SMS_setBGScrollX((unsigned char)(0 - cam_x));
                SMS_setBGScrollY(scroll_y);
                prev_ks = SMS_getKeysStatus();   /* swallow the held button */
                break;
            }
        }

        /* enter a level? (stand on a city cell, press a button) */
        if (kp & (PORT_A_KEY_1 | PORT_A_KEY_2)) {
            unsigned char pcx = (unsigned char)((unsigned int)(px + 8) >> 4);
            unsigned char pcy = (unsigned char)((unsigned int)(py + 8) >> 4);
            for (i = 0; i < n_entries; i++) {
                if (en_mx[i] != pcx || en_my[i] != pcy) continue;
                if (level_done[en_lvl[i]]) break;      /* done: no re-entry */
                sfx_play(SFX_ENTER);
                wait_frames(30);
                cur_level = en_lvl[i];
                {
                    static char m[] = "ENTERING LEVEL 00";
                    m[15] = '0' + cur_level / 10;
                    m[16] = '0' + cur_level % 10;
                    if (cur_level < 10) { m[15] = '0' + cur_level; m[16] = 0; }
                    show_status(m);
                }
                game_state = STATE_LEVEL;
                break;
            }
        }

        sfx_update();
        camera_follow(px + 8 - 124, py + 8 - 92);
        build_sprites_ow();
    }
    psg_tone_off(); psg_noise_off();
}

/* ---------------------------------------------------------------- ending -- */
static void run_win(void) {
    screen_text_begin();
    print_at(6, 5,  "WITH ALL FOUR PARTS");
    print_at(6, 7,  "THE BEAN-WITH-BACON");
    print_at(6, 9,  "MEGAROCKET IS WHOLE!");
    print_at(6, 12, "KEEN BLASTS OFF HOME");
    print_at(6, 14, "AND SAVES THE EARTH.");
    print_at(6, 18, "FINAL SCORE");
    print_num(18, 18, score_hi, score_lo);
    SMS_displayOn();
    sfx_play(SFX_EXIT);
    wait_frames(480);
    /* fresh game state for another run */
    {
        unsigned char i;
        for (i = 0; i < NLEVELS; i++) level_done[i] = 0;
    }
    inv_parts = 0; inv_ammo = 0; inv_pogo = 0;
    score_hi = score_lo = 0;
    game_state = STATE_TITLE;
}

/* ------------------------------------------------------------------ title -- */
static void run_title(void) {
    unsigned int kp;
    SMS_displayOff();
    SMS_VDPturnOffFeature(VDPFEATURE_LEFTCOLBLANK);
    SMS_initSprites();
    SMS_copySpritestoSAT();
    SMS_VRAMmemsetW(0, 0x0000, 16384);
    SMS_setBGScrollX(0); SMS_setBGScrollY(0);

    SMS_mapROMBank(BANK_TITLE);
    SMS_loadTiles(title_tiles, 0, title_ntiles * 32);
    SMS_loadTileMapArea(0, 0, title_map, 32, 24);
    SMS_displayOn();

    prev_ks = SMS_getKeysStatus();
    for (;;) {
        unsigned int ks;
        SMS_waitForVBlank();
        ks = SMS_getKeysStatus();
        kp = ks & ~prev_ks;
        prev_ks = ks;
        if (kp & PORT_A_KEY_1) {
            if (SMS_getKeysStatus() & PORT_A_KEY_2) {   /* cheat */
                inv_pogo = 1; inv_ammo = 99;
            }
            break;
        }
    }
    /* new game */
    score_hi = score_lo = 0;
    if (!(inv_ammo == 99 && inv_pogo)) { inv_ammo = 0; inv_pogo = 0; }
    inv_keys = 0;
    level_done[1] = level_done[2] = level_done[3] = 0;
    game_state = STATE_OW;
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
        }
    }
}

SMS_EMBED_SEGA_ROM_HEADER(9999, 0);
SMS_EMBED_SDSC_HEADER_AUTO_DATE(1, 0,
    "keen-sms",
    "Commander Keen SMS",
    "Port of HTML5-Keen (melonJS) to the Sega Master System via devkitSMS");
