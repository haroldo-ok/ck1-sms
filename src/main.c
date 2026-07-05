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

/* VRAM sprite tile slots (absolute tile numbers; pass n-256 to addSprite) */
#define VT_PLAYER   256            /* 6 tiles, streamed every frame          */
#define VT_YORP0    262            /* 3 x 6 tiles, streamed on frame change  */
#define VT_BULLET   280            /* 12 tiles, loaded once per level        */
#define VT_OWKEEN   440            /* 4 tiles, overworld player (BG<=384)    */

/* physics, 8.8 fixed point (units: 1/256 px per frame) */
#define WALK_ACC     96
#define WALK_MAX    576
#define AIR_ACC      96
#define GRAV         38
#define JUMP_V    (-1024)
#define FALL_MAX   1024
#define POGO_STEER   77
#define BULLET_V    640

#define MAX_YORPS    8
#define MAX_OVR     32

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
unsigned char level_done[4];
static unsigned int  score_hi;          /* score = hi*10000 + lo (BCD-ish)   */
static unsigned int  score_lo;

/* inventory (persists across levels like the JS original) */
static unsigned char inv_ammo, inv_pogo;
static unsigned char inv_keys;          /* bit 0..3 = keycards A..D          */

/* map access (valid while ld->map_bank mapped) */
static const unsigned char *map_rom;
static const unsigned int  *mtdef_rom;
static unsigned char flags_ram[256];
static unsigned int  mapW, mapH;        /* in metatiles */
static unsigned int  mapPW, mapPH;      /* in pixels    */

/* item overrides: cells whose metatile changed after pickup */
static unsigned char ovr_mx[MAX_OVR], ovr_my[MAX_OVR], ovr_mt[MAX_OVR];
static unsigned char n_ovr;

/* items copied to RAM at load */
static unsigned char it_mx[MAX_OVR], it_my[MAX_OVR], it_kind[MAX_OVR],
                     it_restore[MAX_OVR], it_taken[MAX_OVR];
static unsigned char n_items;

/* overworld entries */
static unsigned char en_mx[8], en_my[8], en_mw[8], en_mh[8], en_lvl[8];
static unsigned char n_entries;

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

/* yorps */
static unsigned char y_n;
static int  y_px[MAX_YORPS], y_py[MAX_YORPS];
static int  y_vx[MAX_YORPS], y_vy[MAX_YORPS];
static int  y_sx[MAX_YORPS], y_sy[MAX_YORPS];
static unsigned char  y_state[MAX_YORPS];   /* 0 alive 1 stunned 2 dying 3 dead */
static unsigned int   y_t[MAX_YORPS];
static unsigned char  y_hop[MAX_YORPS];
static unsigned char  y_frame[MAX_YORPS];
static unsigned char  y_slot[MAX_YORPS];    /* 0..2 or 0xFF */
static unsigned char  slot_owner[3];        /* yorp idx or 0xFF */
static unsigned char  slot_vframe[3];       /* frame resident in VRAM (0xFE=none) */

static unsigned char rng;
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
static unsigned int row_off[72];

/* --------------------------------------------------------------- map query - */
static unsigned char cell_mt(unsigned int mx, unsigned int my) {
    unsigned char m = map_rom[row_off[my] + mx];
    if (n_ovr) {
        unsigned char i;
        for (i = 0; i < n_ovr; i++)
            if (ovr_mx[i] == mx && ovr_my[i] == my) { m = ovr_mt[i]; break; }
    }
    return m;
}

/* flags at world pixel; outside map: sides/top solid, bottom open */
static unsigned char mflag(int wx, int wy) {
    unsigned int mx, my;
    if (wx < 0 || wy < 0) return F_SOLID;
    mx = (unsigned int)wx >> 4;
    my = (unsigned int)wy >> 4;
    if (mx >= mapW) return F_SOLID;
    if (my >= mapH) return 0;
    return flags_ram[map_rom[row_off[my] + mx]];
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

static void add_score(unsigned char kind) {
    score_lo += ((unsigned int)item_score_hi[kind] << 8) + item_score_lo[kind];
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
    if (lvl) SMS_loadTiles(spr_blt, VT_BULLET, 12 * 32);

    /* map-side data */
    SMS_mapROMBank(ld->map_bank);
    map_rom   = ld->map;
    mtdef_rom = ld->mtdef;
    for (i = 0; i < ld->nmt; i++) flags_ram[i] = ld->mtflags[i];
    /* scroll_y recomputed by callers after camera setup via set_scroll_y_full */

    n_ovr = 0;
    n_items = ld->nitems;
    p = ld->items;
    for (i = 0; i < n_items; i++) {
        it_mx[i] = *p++; it_my[i] = *p++; it_kind[i] = *p++; it_restore[i] = *p++;
        it_taken[i] = 0;
    }
    y_n = ld->nyorps;
    for (i = 0; i < y_n; i++) {
        y_px[i] = (int)ld->yorps[i * 2];
        y_py[i] = (int)ld->yorps[i * 2 + 1];
        y_vx[i] = y_vy[i] = y_sx[i] = y_sy[i] = 0;
        y_state[i] = 0; y_t[i] = 0; y_hop[i] = (i * 13) & 31;
        y_frame[i] = YF_LOOK0; y_slot[i] = 0xFF;
    }
    for (i = 0; i < 3; i++) { slot_owner[i] = 0xFF; slot_vframe[i] = 0xFE; }
    n_entries = ld->nentries;
    p = ld->entries;
    for (i = 0; i < n_entries; i++) {
        en_mx[i] = *p++; en_my[i] = *p++; en_mw[i] = *p++; en_mh[i] = *p++; en_lvl[i] = *p++;
    }

    px = (int)ld->spawn_x; py = (int)ld->spawn_y;
    if (lvl) py -= 8;                      /* mainPlayer object is 16x32 anchor */
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
            game_state = STATE_OW;
        }
        return;
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

    /* items: cheap byte prefilter (cell distance) before the full AABB */
    {
    unsigned char pcx = (unsigned char)((unsigned int)(px + 8) >> 4);
    unsigned char pcy = (unsigned char)((unsigned int)(py + 12) >> 4);
    for (i = 0; i < n_items; i++) {
        int cx, cy;
        if (it_taken[i]) continue;
        if ((unsigned char)(it_mx[i] - pcx + 1) > 2) continue;
        if ((unsigned char)(it_my[i] - pcy + 1) > 2) continue;
        cx = (int)it_mx[i] << 4; cy = (int)it_my[i] << 4;
        if (px + 12 >= cx && px + 3 <= cx + 15 &&
            py + 23 >= cy && py + 1 <= cy + 15) {
            unsigned char k = it_kind[i];
            it_taken[i] = 1;
            if (n_ovr < MAX_OVR) {
                ovr_mx[n_ovr] = it_mx[i]; ovr_my[n_ovr] = it_my[i];
                ovr_mt[n_ovr] = it_restore[i]; n_ovr++;
            }
            nt_update_cell(it_mx[i], it_my[i]);
            add_score(k);
            if (k == 5)      { inv_ammo += 5; sfx_play(SFX_KEYCARD); }
            else if (k == 6) { inv_pogo = 1;  sfx_play(SFX_KEYCARD); }
            else if (k >= 7) { inv_keys |= 1 << (k - 7); inv_pogo = 1; sfx_play(SFX_KEYCARD); }
            else sfx_play(SFX_COLLECT);
        }
    }
    }

    /* exit zone */
    if (ld->exit_mw) {
        int ex0 = (int)ld->exit_mx << 4, ey0 = (int)ld->exit_my << 4;
        int ex1 = ex0 + ((int)ld->exit_mw << 4), ey1 = ey0 + ((int)ld->exit_mh << 4) + 14;
        if (px + 8 >= ex0 && px + 8 < ex1 && py + 23 >= ey0 && py + 23 < ey1 &&
            on_ground && !pogoing) {
            exiting = 1; seq_timer = 0;
            sfx_play(SFX_EXIT);
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
    /* yorps */
    for (i = 0; i < y_n; i++) {
        if (y_state[i] >= 2) continue;
        if (b_px + 12 >= y_px[i] && b_px + 4 <= y_px[i] + 15 &&
            b_py + 6  >= y_py[i] && b_py + 2 <= y_py[i] + 23) {
            y_state[i] = 2; y_t[i] = 0;
            add_score(0);                        /* 100 pts for a yorp */
            b_hit = 1; b_timer = 0;
            sfx_play(SFX_ZAP);
            return;
        }
    }
}

/* ----------------------------------------------------------------- yorps --- */
static void yorp_update(unsigned char i) {
    int dy, dx;
    unsigned char f;

    if (y_state[i] == 3) return;                 /* dead: corpse only */
    /* offscreen cull (like melonJS, which doesn't update offscreen
       entities): full AI + physics only within ~1.25 screens */
    dx = y_px[i] - (int)cam_x;
    if (dx < -80 || dx > 320) {
        if (y_state[i] == 1 && ++y_t[i] > 240) { y_state[i] = 0; y_t[i] = 0; }
        return;
    }
    if (y_state[i] == 2) {                       /* dying */
        y_frame[i] = YF_DIE;
        if (++y_t[i] > 12) { y_state[i] = 3; y_frame[i] = YF_DEAD; }
        return;
    }
    if (y_state[i] == 1) {                       /* stunned / crying */
        y_frame[i] = YF_CRY0 + ((y_t[i] >> 3) & 1);
        if (++y_t[i] > 240) { y_state[i] = 0; y_t[i] = 0; }
        return;
    }

    /* alive: hop periodically */
    if (++y_hop[i] > 40) {
        unsigned char g = (mflag(y_px[i] + 4, y_py[i] + 24) |
                           mflag(y_px[i] + 11, y_py[i] + 24)) & (F_SOLID | F_PLAT);
        if (g) { y_vy[i] = -320; }
        y_hop[i] = 0;
    }

    /* chase the player */
    dx = px - y_px[i];
    if (dx < -20 || dx > 20) {
        if (dx < 0) { y_vx[i] -= 13; if (y_vx[i] < -64) y_vx[i] = -64; }
        else        { y_vx[i] += 13; if (y_vx[i] >  64) y_vx[i] =  64; }
    } else y_vx[i] = 0;

    /* gravity */
    y_vy[i] += 20;
    if (y_vy[i] > 768) y_vy[i] = 768;

    /* X move */
    y_sx[i] += y_vx[i];
    dx = y_sx[i] >> 8; y_sx[i] -= dx << 8;
    if (dx > 0) {
        f = mflag(y_px[i] + 13 + dx, y_py[i] + 8) | mflag(y_px[i] + 13 + dx, y_py[i] + 20);
        if (f & F_SOLID) { y_vx[i] = 0; } else y_px[i] += dx;
    } else if (dx < 0) {
        f = mflag(y_px[i] + 2 + dx, y_py[i] + 8) | mflag(y_px[i] + 2 + dx, y_py[i] + 20);
        if (f & F_SOLID) { y_vx[i] = 0; } else y_px[i] += dx;
    }

    /* Y move */
    y_sy[i] += y_vy[i];
    dy = y_sy[i] >> 8; y_sy[i] -= dy << 8;
    if (dy > 0) {
        int ny = y_py[i] + 23 + dy;
        unsigned char crossed = ((y_py[i] + 23) >> 4) < (ny >> 4);
        f = mflag(y_px[i] + 4, ny) | mflag(y_px[i] + 11, ny);
        if ((f & F_SOLID) || ((f & F_PLAT) && crossed)) {
            y_py[i] = (ny & ~15) - 24; y_vy[i] = 0; y_sy[i] = 0;
        } else y_py[i] += dy;
    } else if (dy < 0) {
        int ny = y_py[i] + 1 + dy;
        f = mflag(y_px[i] + 4, ny) | mflag(y_px[i] + 11, ny);
        if ((f & F_SOLID) && !(f & F_PLAT)) { y_vy[i] = 0; y_sy[i] = 0; }
        else y_py[i] += dy;
    }

    /* head-bump: player lands on the yorp */
    if (!dying && vy > 0 &&
        px + 12 >= y_px[i] && px + 3 <= y_px[i] + 15 &&
        py + 24 >= y_py[i] && py + 24 <= y_py[i] + 10) {
        y_state[i] = 1; y_t[i] = 0;
        vy = -400; pogoing = 0;                 /* small bounce */
        sfx_play(SFX_BUMP);
    }

    /* animation */
    if (y_vx[i] > 0)      y_frame[i] = YF_WALK_R0 + ((y_hop[i] >> 3) & 1);
    else if (y_vx[i] < 0) y_frame[i] = YF_WALK_L0 + ((y_hop[i] >> 3) & 1);
    else                  y_frame[i] = YF_LOOK0 + ((y_hop[i] >> 4) & 1);
}

/* ------------------------------------------------------------ sprite draw -- */
static void draw_16x24(int sx, int sy, unsigned char base) {
    if ((unsigned int)sx <= 240 && (unsigned int)sy <= 168) {
        /* fully on-screen: skip the clipping math */
        unsigned char x = (unsigned char)sx, y = (unsigned char)sy;
        SMS_addSprite(x,     y,      base);
        SMS_addSprite(x + 8, y,      base + 1);
        SMS_addSprite(x,     y + 8,  base + 2);
        SMS_addSprite(x + 8, y + 8,  base + 3);
        SMS_addSprite(x,     y + 16, base + 4);
        SMS_addSprite(x + 8, y + 16, base + 5);
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
static unsigned char upload_frame;

static void build_sprites_level(void) {
    unsigned char i, s;
    SMS_initSprites();
    /* player */
    draw_16x24(px - (int)cam_x, py - (int)cam_y, (unsigned char)(VT_PLAYER - 256));

    /* yorps: acquire/release slots by visibility */
    upload_slot = 0xFF;
    for (i = 0; i < y_n; i++) {
        int sx = y_px[i] - (int)cam_x;
        int sy = y_py[i] - (int)cam_y;
        unsigned char vis = (sx > -16 && sx < 256 && sy > -24 && sy < 192);
        if (!vis) {
            if (y_slot[i] != 0xFF) { slot_owner[y_slot[i]] = 0xFF; y_slot[i] = 0xFF; }
            continue;
        }
        if (y_slot[i] == 0xFF) {
            for (s = 0; s < 3; s++)
                if (slot_owner[s] == 0xFF) {
                    slot_owner[s] = i; y_slot[i] = s; slot_vframe[s] = 0xFE;
                    break;
                }
            if (y_slot[i] == 0xFF) continue;     /* no free slot: skip */
        }
        s = y_slot[i];
        if (slot_vframe[s] != y_frame[i] && upload_slot == 0xFF) {
            upload_slot = s; upload_frame = y_frame[i];
        }
        if (slot_vframe[s] != 0xFE)          /* something resident: draw it */
            draw_16x24(sx, sy, (unsigned char)(VT_YORP0 - 256 + s * 6));
    }
    /* bullet */
    if (b_active) {
        int sx = b_px - (int)cam_x, sy = b_py - (int)cam_y;
        unsigned char base = (unsigned char)(VT_BULLET - 256)
                           + ((b_hit ? (1 + (rng & 1)) : 0) << 2);
        SMS_addSpriteClipping(sx,     sy,     base);
        SMS_addSpriteClipping(sx + 8, sy,     base + 1);
        SMS_addSpriteClipping(sx,     sy + 8, base + 2);
        SMS_addSpriteClipping(sx + 8, sy + 8, base + 3);
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

        /* --- VBlank window: SAT + streaming + scroll --- */
        UNSAFE_SMS_copySpritestoSAT();
        SMS_setBGScrollX((unsigned char)(0 - cam_x));
        SMS_setBGScrollY(scroll_y);

        SMS_mapROMBank(BANK_SPRITES);
        if (pframe != cur_pframe) {
            UNSAFE_SMS_loadNTiles(spr_keen + (unsigned int)pframe * 192, VT_PLAYER, 6);
            cur_pframe = pframe;
        }
        if (upload_slot != 0xFF) {
            UNSAFE_SMS_loadNTiles(spr_yorp + (unsigned int)upload_frame * 192,
                                  VT_YORP0 + upload_slot * 6, 6);
            slot_vframe[upload_slot] = upload_frame;
            upload_slot = 0xFF;
        }

        /* --- map bank for the rest of the frame --- */
        SMS_mapROMBank(ld->map_bank);
        if (pend_col != 0xFFFF) { draw_col_now(pend_col); pend_col = 0xFFFF; }
        if (pend_row != 0xFFFF) { draw_row_now(pend_row); pend_row = 0xFFFF; }

        ks = SMS_getKeysStatus();
        kp = ks & ~prev_ks;
        prev_ks = ks;

        player_update(ks, kp);
        bullet_update();
        {
            unsigned char i;
            for (i = 0; i < y_n; i++) yorp_update(i);
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

    /* all levels beaten? */
    if (level_done[1] && level_done[2] && level_done[3]) {
        screen_text_begin();
        print_at(10, 8,  "YOU MADE IT!");
        print_at(5, 11, "THE VORTICONS ARE BEATEN");
        print_at(8, 14, "FINAL SCORE");
        print_num(20, 14, score_hi, score_lo);
        SMS_displayOn();
        wait_frames(600);
        game_state = STATE_TITLE;
        return;
    }

    while (game_state == STATE_OW) {
        SMS_waitForVBlank();
        UNSAFE_SMS_copySpritestoSAT();
        SMS_setBGScrollX((unsigned char)(0 - cam_x));
        SMS_setBGScrollY(scroll_y);

        SMS_mapROMBank(BANK_SPRITES);
        if (pframe != cur_pframe) {
            UNSAFE_SMS_loadNTiles(spr_owk + (unsigned int)pframe * 128, VT_OWKEEN, 4);
            cur_pframe = pframe;
        }

        SMS_mapROMBank(ld->map_bank);
        if (pend_col != 0xFFFF) { draw_col_now(pend_col); pend_col = 0xFFFF; }
        if (pend_row != 0xFFFF) { draw_row_now(pend_row); pend_row = 0xFFFF; }

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

        /* enter a level? */
        if (kp & (PORT_A_KEY_1 | PORT_A_KEY_2)) {
            for (i = 0; i < n_entries; i++) {
                int ex0 = (int)en_mx[i] << 4, ey0 = (int)en_my[i] << 4;
                int ex1 = ex0 + ((int)en_mw[i] << 4), ey1 = ey0 + ((int)en_mh[i] << 4);
                if (px + 8 >= ex0 && px + 8 < ex1 && py + 8 >= ey0 && py + 8 < ey1) {
                    sfx_play(SFX_ENTER);
                    wait_frames(30);
                    cur_level = en_lvl[i];
                    {
                        static char m[] = "ENTERING LEVEL 0";
                        m[15] = '0' + cur_level;
                        show_status(m);
                    }
                    game_state = STATE_LEVEL;
                    break;
                }
            }
        }

        sfx_update();
        camera_follow(px + 8 - 124, py + 8 - 92);
        build_sprites_ow();
    }
    psg_tone_off(); psg_noise_off();
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
        }
    }
}

SMS_EMBED_SEGA_ROM_HEADER(9999, 0);
SMS_EMBED_SDSC_HEADER_AUTO_DATE(1, 0,
    "keen-sms",
    "Commander Keen SMS",
    "Port of HTML5-Keen (melonJS) to the Sega Master System via devkitSMS");
