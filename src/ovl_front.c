/*
 * Front end overlay: intro logos, title + main menu, the story / help /
 * ordering / previews pages, high scores (cartridge RAM), game over and the
 * saved-game slots.  Every picture and every line of text comes from the
 * original data files (see tools/ck1front.py).
 */
#include "SMSlib.h"
#include "game_data.h"
#include "engine.h"

#define BTN (PORT_A_KEY_1 | PORT_A_KEY_2)

static unsigned int  wbuf[32];          /* one name-table row            */
static unsigned char cbuf[32];
static unsigned int  okeys;             /* previous key state (edges)    */
static unsigned int  uitb;              /* tile base of the dialog font  */
static char          nbuf[12];

/* ---------------------------------------------------------------- basics -- */
static void scr_begin(void) {
    SMS_displayOff();
    SMS_VDPturnOffFeature(VDPFEATURE_LEFTCOLBLANK);
    SMS_initSprites();
    SMS_copySpritestoSAT();
    SMS_VRAMmemsetW(0, 0x0000, 16384);
    SMS_setBGScrollX(0); SMS_setBGScrollY(0);
}

/* next key press (edge) -> bit mask; keeps sound running */
static unsigned int get_edge(void) {
    unsigned int ks, kp;
    SMS_waitForVBlank(); sfx_update();
    ks = SMS_getKeysStatus();
    kp = ks & ~okeys;
    okeys = ks;
    return kp;
}

static unsigned int wait_key(void) {
    unsigned int kp;
    okeys = SMS_getKeysStatus();
    do kp = get_edge(); while (!kp);
    return kp;
}

/* wait up to n frames for a button; returns 1 if pressed */
static unsigned char wait_or_key(unsigned int n) {
    okeys = SMS_getKeysStatus();
    while (n--) if (get_edge() & BTN) return 1;
    return 0;
}

/* tile image from the original screens: tiles at tb, map at cell (x,y) */
static void img_map(unsigned char id, unsigned int tb, unsigned char x, unsigned char y) {
    const FeImg *im = &fe_img[id];
    unsigned char r, c;
    for (r = 0; r < im->h; r++) {
        bank_copy(im->bank, (const unsigned char *)(im->map + (unsigned int)r * im->w),
                  (unsigned char *)wbuf, (unsigned int)im->w * 2);
        for (c = 0; c < im->w; c++) wbuf[c] += tb;
        SMS_VRAMmemcpy(XYtoADDR(x, y + r), wbuf, (unsigned int)im->w * 2);
    }
}

static void img_show(unsigned char id, unsigned int tb, unsigned char x, unsigned char y) {
    const FeImg *im = &fe_img[id];
    bank_tiles(im->bank, im->tiles, tb, im->ntiles);
    img_map(id, tb, x, y);
}

/* ------------------------------------------------------- dialog look ------ */
/* the black-on-white font of the original message boxes, loaded glyph by
   glyph on first use (the title picture leaves only ~80 tile slots) */
static unsigned int gtile[96];
static unsigned int gnext;

static void ui_load(unsigned int tb) {
    unsigned char i;
    uitb = tb;
    bank_tiles(BANK_TXT, dlg_frame, tb, 8);
    gnext = tb + 8;
    for (i = 0; i < 96; i++) gtile[i] = 0xFFFF;
}

static unsigned int glyph(char c) {
    unsigned char i = (unsigned char)(c - 32);
    if (gtile[i] == 0xFFFF) {
        bank_tiles(BANK_TXT, dlg_f0 + (unsigned int)i * 32, gnext, 1);
        gtile[i] = gnext++;
    }
    return gtile[i];
}

static void ui_str(unsigned char x, unsigned char y, const char *s) {
    unsigned int t;
    for (; *s; s++, x++)
        if (*s >= 32) { t = glyph(*s); SMS_setTileatXY(x, y, t); }
}

/* framed white box: interior w x h cells, top-left of the FRAME at (x,y) */
static void ui_box(unsigned char x, unsigned char y, unsigned char w, unsigned char h) {
    unsigned char i, j;
    unsigned int fill = glyph(' ');
    for (j = 0; j < h + 2; j++)
        for (i = 0; i < w + 2; i++) {
            unsigned int t = fill;
            if (j == 0)          t = uitb + (i == 0 ? 0 : (i == w + 1 ? 2 : 1));
            else if (j == h + 1) t = uitb + (i == 0 ? 5 : (i == w + 1 ? 7 : 6));
            else if (i == 0)     t = uitb + 3;
            else if (i == w + 1) t = uitb + 4;
            SMS_setTileatXY(x + i, y + j, t);
        }
}

static char *ul_str(unsigned long v) {
    unsigned char i = 11;
    nbuf[11] = 0;
    do { nbuf[--i] = '0' + (unsigned char)(v % 10); v /= 10; } while (v);
    return nbuf + i;
}

/* ----------------------------------------------------------- text pages -- */
/* CTextViewer: scrolls the re-flowed original text; up/down read, button 2
   leaves.  Tile code == character code in the original 8x8 set (loaded at
   tile 0), '~' headings are already red-on-grey glyphs. */
static void page_row(const FeTxt *t, unsigned int r) {
    unsigned char c;
    if (r < t->nrows) {
        bank_copy(t->bank, t->rows + r * 30u, cbuf, 30);
        wbuf[0] = wbuf[31] = 32;
        for (c = 0; c < 30; c++) wbuf[c + 1] = cbuf[c];
    } else {
        for (c = 0; c < 32; c++) wbuf[c] = 32;
    }
    SMS_VRAMmemcpy(XYtoADDR(0, r % 28), wbuf, 64);
}

static void view_page(unsigned char pg) {
    const FeTxt *t = &fe_txt[pg];
    unsigned int top = 0, maxtop, r;
    unsigned char rep = 0;
    scr_begin();
    bank_tiles(BANK_FE_T8, fe_t8, 0, 256);
    for (r = 0; r < 25; r++) page_row(t, r);
    maxtop = t->nrows > 22 ? t->nrows - 22 : 0;
    SMS_displayOn();
    okeys = SMS_getKeysStatus();
    for (;;) {
        unsigned int ks, kp;
        unsigned char moved = 0;
        SMS_waitForVBlank(); sfx_update();
        ks = SMS_getKeysStatus();
        kp = ks & ~okeys; okeys = ks;
        if (kp & PORT_A_KEY_2) break;
        if (rep) rep--;
        if (!rep) {
            if ((ks & (PORT_A_KEY_DOWN | PORT_A_KEY_1)) && top < maxtop) {
                top++; page_row(t, top + 24); moved = 1;
            } else if ((ks & PORT_A_KEY_UP) && top > 0) {
                top--; page_row(t, top); moved = 1;
            }
            if (moved) { rep = (ks & PORT_A_KEY_1) ? 1 : 2; }
        }
        if (moved) SMS_setBGScrollY((unsigned char)((top * 8u) % 224u));
    }
    SMS_displayOff();
}

static void show_img(unsigned char id) {
    scr_begin();
    img_show(id, 0, 0, 0);
    SMS_displayOn();
    wait_key();
}

/* ------------------------------------------------------ cartridge RAM ----- */
static unsigned char sc_tab[8 * SC_ENTRY];
static unsigned char svbuf[SAVE_SIZE];

static unsigned char sc_sum(const unsigned char *p, unsigned char n) {
    unsigned char s = 0x5A;
    while (n--) s += *p++;
    return s;
}

static void sc_load(void) {
    unsigned char hd[2], sm;
    sram_rw(0, SC_OFF, hd, 2);
    if (hd[0] == 'K' && hd[1] == 'S') {
        sram_rw(0, SC_OFF + 2, sc_tab, 8 * SC_ENTRY);
        sram_rw(0, SC_OFF + 2 + 8 * SC_ENTRY, &sm, 1);
        if (sm == sc_sum(sc_tab, 8 * SC_ENTRY)) return;
    }
    bank_copy(BANK_FE_SCORES, fe_scores_default, sc_tab, 8 * SC_ENTRY);
}

static void sc_save(void) {
    unsigned char hd[2];
    hd[0] = 'K'; hd[1] = 'S';
    sram_rw(1, SC_OFF, hd, 2);
    sram_rw(1, SC_OFF + 2, sc_tab, 8 * SC_ENTRY);
    hd[0] = sc_sum(sc_tab, 8 * SC_ENTRY);
    sram_rw(1, SC_OFF + 2 + 8 * SC_ENTRY, hd, 1);
}

static unsigned long sc_val(unsigned char i) {
    const unsigned char *p = sc_tab + i * SC_ENTRY + 8;
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static unsigned long cur_score(void) {
    return (unsigned long)score_hi * 10000UL + score_lo;
}

/* the high-score screen: the id-frame backdrop from LEVEL90 */
static unsigned int SC_FONT, SC_ICON;     /* grey font + part icons follow the picture */

static void sc_draw(unsigned char hl, unsigned char cursor, unsigned char blink) {
    unsigned char i, k, r, c;
    for (i = 0; i < 8; i++) {
        const unsigned char *e = sc_tab + i * SC_ENTRY;
        unsigned long v = sc_val(i);
        char *s;
        r = 7 + i * 2;                        /* text on the lower row of each pair */
        for (c = 4; c < 28; c++) SMS_setTileatXY(c, r, SC_FONT);
        for (c = 0; c < 8; c++) {
            unsigned char ch = e[c];
            if (ch >= 32 && ch < 128) SMS_setTileatXY(4 + c, r, SC_FONT + (ch - 32));
        }
        if (i == hl && blink && cursor < 8) SMS_setTileatXY(4 + cursor, r, SC_FONT + ('_' - 32));
        s = ul_str(v);
        for (k = 0; *s; s++, k++) ;
        s -= k;
        for (c = 0; c < k; c++) SMS_setTileatXY(18 - k + c, r, SC_FONT + (s[c] - 32));
        for (k = 0; k < 4; k++) {            /* the ship parts that run collected */
            unsigned char on = (e[12] >> k) & 1;
            unsigned int b = SC_ICON + k * 4;
            SMS_setTileatXY(20 + k * 2,     r - 1, on ? b     : SC_FONT);
            SMS_setTileatXY(21 + k * 2,     r - 1, on ? b + 1 : SC_FONT);
            SMS_setTileatXY(20 + k * 2,     r,     on ? b + 2 : SC_FONT);
            SMS_setTileatXY(21 + k * 2,     r,     on ? b + 3 : SC_FONT);
        }
    }
}

static void sc_screen(void) {
    scr_begin();
    img_show(IMG_HISCORE, 0, 0, 0);
    SC_FONT = fe_img[IMG_HISCORE].ntiles; SC_ICON = SC_FONT + 96;
    bank_tiles(BANK_FE_GREY, fe_font_grey, SC_FONT, 96);
    bank_tiles(BANK_TXT, dlg_icons + 2 * 128, SC_ICON, 16);
}

void ovl_scores_show(void);
void ovl_scores_show(void) {
    sc_load();
    sc_screen();
    sc_draw(255, 0, 0);
    SMS_displayOn();
    wait_key();
    SMS_displayOff();
}

static const char charset[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!.-";

/* new high score: type the name with the pad (up/down = letter, right =
   next, left = back, button 2 = done) */
static void sc_enter(unsigned char place) {
    unsigned char i, pos = 0, idx = 1, blink = 0, tm = 0;
    unsigned char *e;
    const unsigned char nchars = sizeof(charset) - 1;
    unsigned char *dst;
    for (i = 7; i > place; i--)
        for (pos = 0; pos < SC_ENTRY; pos++) sc_tab[i * SC_ENTRY + pos] = sc_tab[(i - 1) * SC_ENTRY + pos];
    e = sc_tab + place * SC_ENTRY;
    for (pos = 0; pos < 8; pos++) e[pos] = 0;
    { unsigned long v = cur_score();
      e[8] = (unsigned char)v; e[9] = (unsigned char)(v >> 8);
      e[10] = (unsigned char)(v >> 16); e[11] = (unsigned char)(v >> 24); }
    e[12] = inv_parts;
    dst = e;
    pos = 0; dst[0] = charset[idx];
    sc_screen();
    sc_draw(place, pos, 1);
    SMS_displayOn();
    okeys = SMS_getKeysStatus();
    for (;;) {
        unsigned int kp = get_edge();
        if (++tm >= 20) { tm = 0; blink ^= 1; }
        if (kp & PORT_A_KEY_2) break;
        if (kp & PORT_A_KEY_UP)   { idx = idx + 1 >= nchars ? 0 : idx + 1; dst[pos] = charset[idx]; }
        if (kp & PORT_A_KEY_DOWN) { idx = idx == 0 ? nchars - 1 : idx - 1; dst[pos] = charset[idx]; }
        if ((kp & (PORT_A_KEY_RIGHT | PORT_A_KEY_1)) && pos < 7) { pos++; idx = 1; dst[pos] = charset[idx]; }
        if ((kp & PORT_A_KEY_LEFT) && pos > 0) {
            dst[pos] = 0; pos--;
            for (idx = 0; idx < nchars && charset[idx] != dst[pos]; idx++) ;
        }
        sc_draw(place, pos, blink);
    }
    for (i = 0; i < 8; i++) if (dst[i] == ' ') dst[i] = 0;
    sc_save();
    sc_draw(255, 0, 0);
    wait_key();
    SMS_displayOff();
}

static void sc_check(void) {
    unsigned char i;
    unsigned long v = cur_score();
    sc_load();
    for (i = 0; i < 8; i++) if (v > sc_val(i)) break;
    if (i < 8 && v > 0) {
        sfx_play(SND_HISCORE);
        sc_enter(i);
    }
}

/* ------------------------------------------------------------- game over -- */
void ovl_gameover(void) {
    scr_begin();
    img_show(IMG_GAMEOVER, 0, 0, 0);
    SMS_displayOn();
    sfx_play(SND_GAMEOVER);
    wait_or_key(240);
    sc_check();
    ovl_scores_show();
    game_state = STATE_TITLE;
}

/* ------------------------------------------------------------ saved games -- */
static unsigned char sv_read(unsigned char slot) {
    sram_rw(0, slot * SAVE_SIZE, svbuf, SAVE_SIZE);
    return svbuf[0] == 'K' && svbuf[1] == '1' && svbuf[34] == sc_sum(svbuf, 34);
}

static void sv_write(unsigned char slot) {
    unsigned char i;
    for (i = 0; i < SAVE_SIZE; i++) svbuf[i] = 0;
    svbuf[0] = 'K'; svbuf[1] = '1';
    for (i = 0; i < NLEVELS; i++) svbuf[2 + i] = level_done[i];
    svbuf[19] = (unsigned char)score_hi; svbuf[20] = (unsigned char)(score_hi >> 8);
    svbuf[21] = (unsigned char)score_lo; svbuf[22] = (unsigned char)(score_lo >> 8);
    svbuf[23] = lives; svbuf[24] = inv_parts; svbuf[25] = inv_ammo; svbuf[26] = inv_pogo;
    svbuf[27] = (unsigned char)next_life; svbuf[28] = (unsigned char)(next_life >> 8);
    svbuf[29] = (unsigned char)ow_px; svbuf[30] = (unsigned char)((unsigned int)ow_px >> 8);
    svbuf[31] = (unsigned char)ow_py; svbuf[32] = (unsigned char)((unsigned int)ow_py >> 8);
    svbuf[34] = sc_sum(svbuf, 34);
    sram_rw(1, slot * SAVE_SIZE, svbuf, SAVE_SIZE);
}

static void sv_apply(void) {
    unsigned char i;
    for (i = 0; i < NLEVELS; i++) level_done[i] = svbuf[2 + i];
    score_hi = svbuf[19] | ((unsigned int)svbuf[20] << 8);
    score_lo = svbuf[21] | ((unsigned int)svbuf[22] << 8);
    lives = svbuf[23]; inv_parts = svbuf[24]; inv_ammo = svbuf[25]; inv_pogo = svbuf[26];
    next_life = svbuf[27] | ((unsigned int)svbuf[28] << 8);
    ow_px = (int)(svbuf[29] | ((unsigned int)svbuf[30] << 8));
    ow_py = (int)(svbuf[31] | ((unsigned int)svbuf[32] << 8));
}

/* slot list in a framed box; returns 0..SAVE_SLOTS-1 or 255 = cancelled */
static unsigned char slot_pick(const char *l1, const char *l2, unsigned char saving) {
    unsigned char sel = 0, i, k, fresh = 1;
    char line[24];
    scr_begin();
    ui_load(0);
    ui_box(2, 5, 26, 13);
    ui_str(5, 7, l1);
    ui_str(5, 8, l2);
    for (i = 0; i < SAVE_SLOTS; i++) {
        char *s;
        line[0] = '1' + i; line[1] = ':'; line[2] = ' '; line[3] = 0;
        ui_str(6, 11 + i * 2, line);
        if (sv_read(i)) {
            unsigned long v = (unsigned long)(svbuf[19] | ((unsigned int)svbuf[20] << 8)) * 10000UL
                            + (svbuf[21] | ((unsigned int)svbuf[22] << 8));
            s = ul_str(v);
            ui_str(9, 11 + i * 2, "Score");
            ui_str(15, 11 + i * 2, s);
        } else ui_str(9, 11 + i * 2, "Empty");
    }
    ui_str(5, 17, saving ? "Button 2: cancel" : "Button 2: back");
    glyph('>');
    SMS_displayOn();
    okeys = SMS_getKeysStatus();
    for (;;) {
        unsigned int kp = get_edge();
        if (kp & PORT_A_KEY_2) { k = 255; break; }
        if (kp & PORT_A_KEY_UP)   { if (sel) sel--; else sel = SAVE_SLOTS - 1; fresh = 1; }
        if (kp & PORT_A_KEY_DOWN) { sel = sel + 1 >= SAVE_SLOTS ? 0 : sel + 1; fresh = 1; }
        if (kp & PORT_A_KEY_1) { k = sel; break; }
        if (fresh) {
            for (i = 0; i < SAVE_SLOTS; i++) {
                unsigned int t = glyph(i == sel ? '>' : ' ');
                SMS_setTileatXY(4, 11 + i * 2, t);
            }
            fresh = 0;
        }
    }
    SMS_displayOff();
    return k;
}

void ovl_save(void) {
    unsigned char s = slot_pick("Which game position", "do you want to save?", 1);
    if (s != 255) sv_write(s);
}

/* ------------------------------------------------------------ title menu -- */
static const char *const menu_x[1] = { 0 };

void ovl_title(void) {
    unsigned char sel = 0, i, again;
    unsigned int idle;
    const FeTxt *mt = &fe_txt[PG_MENU];
    /* AN APOGEE PRESENTATION / AN ID SOFTWARE PRODUCTION */
    scr_begin(); img_show(IMG_LOGO1, 0, 0, 0); SMS_displayOn();
    if (!wait_or_key(150)) {
        scr_begin(); img_show(IMG_LOGO2, 0, 0, 0); SMS_displayOn();
        wait_or_key(150);
    }
    (void)menu_x;
    for (;;) {
        unsigned int ntitle = fe_img[IMG_TITLE].ntiles;
        scr_begin();
        img_show(IMG_TITLE, 0, 0, 0);
        ui_load(ntitle);
        ui_box(5, 15, 21, 7);
        for (i = 0; i < 7; i++) {
            bank_copy(mt->bank, mt->rows + i * 30u, cbuf, 30);
            cbuf[16] = 0;
            for (again = 0; cbuf[again]; again++) {
                unsigned int t = glyph(cbuf[again]);
                SMS_setTileatXY(8 + again, 16 + i, t);
            }
        }
        glyph('>');
        SMS_displayOn();
        okeys = SMS_getKeysStatus();
        idle = 0;
        again = 0;
        while (!again) {
            unsigned int kp = get_edge();
            for (i = 0; i < 7; i++) {
                unsigned int t = glyph(i == sel ? '>' : ' ');
                SMS_setTileatXY(6, 16 + i, t);
            }
            if (kp) idle = 0;
            if (++idle > 60u * 24u) { ovl_scores_show(); again = 1; break; }
            if (kp & PORT_A_KEY_UP)   sel = sel ? sel - 1 : 6;
            if (kp & PORT_A_KEY_DOWN) sel = sel < 6 ? sel + 1 : 0;
            if (kp & BTN) {
                switch (sel) {
                case 0:
                    new_game();
                    if (SMS_getKeysStatus() & PORT_A_KEY_2) { inv_pogo = 1; inv_ammo = 99; }
                    SMS_displayOff();
                    return;
                case 1: {
                    unsigned char s;
                    SMS_displayOff();
                    s = slot_pick("  Continue Which Game?", "", 0);
                    if (s != 255 && sv_read(s)) {
                        new_game(); sv_apply();
                        game_state = STATE_OW;
                        return;
                    }
                    again = 1; break; }
                case 2: view_page(PG_STORY); again = 1; break;
                case 3: show_img(IMG_IDABOUT); again = 1; break;
                case 4: ovl_scores_show(); again = 1; break;
                case 5: view_page(PG_ORDER); again = 1; break;
                case 6:
                    view_page(PG_PREVIEW);
                    show_img(IMG_PREVIEW2);
                    show_img(IMG_PREVIEW3);
                    again = 1; break;
                }
            }
        }
    }
}

/* ----------------------------------------------------------------- ending -- */
/* CEndingEp1::BackAtHome: the FINALE backyard picture with the eight text
   pages (EXE strings E2..E9); the bedroom window lights come on for pages
   2..6 (WINDON) and go out for 6..8 (WINDOFF) at (80,0), as in the original. */
void ovl_end_scene(void) {
    unsigned char k, w, h, cur, x, y, ln;
    unsigned int nt, kp;
    const char *p;
    scr_begin();
    img_show(IMG_FINALE, 0, 0, 0);
    nt = fe_img[IMG_FINALE].ntiles;
    bank_tiles(fe_img[IMG_WINDON].bank, fe_img[IMG_WINDON].tiles, nt, 20);
    bank_tiles(fe_img[IMG_WINDOFF].bank, fe_img[IMG_WINDOFF].tiles, nt + 20, 20);
    ui_load(nt + 40);
    glyph(' ');
    SMS_displayOn();
    for (k = 0; k < 8; k++) {
        txt_get(TXT_E2 + k);
        w = 0; h = 1; cur = 0;
        for (p = dlg_buf; *p; p++) {
            if (*p == '\n') { if (cur > w) w = cur; cur = 0; h++; } else cur++;
        }
        if (cur > w) w = cur;
        SMS_displayOff();
        img_map(IMG_FINALE, 0, 0, 0);
        if (k >= 2 && k <= 6) img_map(IMG_WINDON, nt, 6, 0);
        if (k >= 6)           img_map(IMG_WINDOFF, nt + 20, 6, 0);
        for (p = dlg_buf; *p; p++) glyph(*p);          /* make sure every glyph is resident */
        ui_box((32 - (w + 4)) / 2, 24 - (h + 4), w + 2, h + 2);
        x = (32 - (w + 4)) / 2 + 2; y = 24 - (h + 4) + 2; ln = 0;
        {
            unsigned char cx = x;
            for (p = dlg_buf; *p; p++) {
                if (*p == '\n') { ln++; cx = x; continue; }
                kp = glyph(*p);
                SMS_setTileatXY(cx, y + ln, kp);
                cx++;
            }
        }
        SMS_displayOn();
        wait_key();
    }
    SMS_displayOff();
}

void ovl_ending_text(void) {
    view_page(PG_ENDTEXT);
    sc_check();
    ovl_scores_show();
}
