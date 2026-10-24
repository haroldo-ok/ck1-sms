/* Shared between the fixed-ROM engine (main.c) and the code overlays
 * (ovl_*.c, each compiled into its own switchable bank at 0x8000).
 *
 * Overlay rules
 *  - an overlay runs while its own bank is in slot 2, so it must NOT touch
 *    level data (map, sprites, sound) directly: that data lives in other
 *    banks.  It reaches any other bank through bank_copy()/bank_tiles(),
 *    which live in fixed ROM and restore the slot-2 shadow afterwards.
 *  - overlays are full-screen modes: they own VRAM while they run, and the
 *    engine reloads whatever it needs when they return.
 */
#ifndef ENGINE_H
#define ENGINE_H

#define STATE_TITLE  0
#define STATE_OW     1
#define STATE_LEVEL  2
#define STATE_WIN    3
#define STATE_DEAD   4

#define NLEVELS      17

#define SAVE_SLOTS   3
#define SAVE_SIZE    40            /* bytes per saved game in cartridge RAM */
#define SC_OFF       (SAVE_SLOTS * SAVE_SIZE)   /* high-score table follows */
#define SC_ENTRY     13            /* name[8] score[4] parts[1] */
#define SC_SIZE      (2 + 8 * SC_ENTRY + 1)

/* game state shared with the overlays */
extern unsigned char game_state, cur_level, lives, inv_ammo, inv_pogo, inv_parts;
extern unsigned char level_done[NLEVELS];
extern unsigned int  score_hi, score_lo;       /* score = hi * 10000 + lo */
extern unsigned int  next_life;
extern int           ow_px, ow_py;
extern unsigned int  prev_ks;

/* fixed-ROM services */
void bank_copy(unsigned char bank, const unsigned char *src, unsigned char *dst, unsigned int n);
void bank_tiles(unsigned char bank, const unsigned char *src, unsigned int tile, unsigned int ntiles);
void sram_rw(unsigned char write, unsigned int off, unsigned char *buf, unsigned char n);
void sfx_play(unsigned char id);
void sfx_update(void);
void wait_frames(unsigned int n);
void new_game(void);
void txt_get(unsigned char id);      /* original EXE string -> dlg_buf */
extern char dlg_buf[168];
extern unsigned char inv_keys;

/* overlay entry points (front.c / end.c) */
void ovl_title(void);                 /* logos, title, menu -> sets game_state */
void ovl_gameover(void);              /* game over + high-score entry          */
void ovl_save(void);                  /* "which game position" prompt          */
void ovl_ending_text(void);           /* ENDTEXT + to be continued             */
void ovl_end_scene(void);             /* finale picture + text boxes           */

#endif
