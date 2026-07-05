#ifndef GAME_DATA_H
#define GAME_DATA_H
#define F_SOLID  1
#define F_PLAT   2
#define F_DEADLY 4
#define BANK_SPRITES 2
#define BANK_TITLE   3
typedef struct {
  const unsigned char *tiles;   unsigned int  tiles_size;
  const unsigned int  *mtdef;   const unsigned char *mtflags;
  const unsigned char *map;
  const unsigned char *items;   unsigned char nitems;
  const unsigned int  *yorps;   unsigned char nyorps;
  const unsigned char *entries; unsigned char nentries;
  unsigned char bank, map_bank, W, H, nmt;
  unsigned int  spawn_x, spawn_y;
  unsigned char exit_mx, exit_my, exit_mw, exit_mh;
} LevelDesc;
extern const LevelDesc level_descs[4];   /* [0]=overworld, [1..3]=levels */
extern const unsigned char bg_palette[16];
extern const unsigned char spr_palette[16];
extern const unsigned char font_1bpp[];
extern const unsigned char item_score_hi[11], item_score_lo[11];
extern const unsigned char lvl1_tiles[],lvl1_mtflags[],lvl1_map[],lvl1_items[],lvl1_entries[];
extern const unsigned int lvl1_mtdef[];
extern const unsigned int lvl1_yorps[];
extern const unsigned char lvl2_tiles[],lvl2_mtflags[],lvl2_map[],lvl2_items[],lvl2_entries[];
extern const unsigned int lvl2_mtdef[];
extern const unsigned int lvl2_yorps[];
extern const unsigned char lvl3_tiles[],lvl3_mtflags[],lvl3_map[],lvl3_items[],lvl3_entries[];
extern const unsigned int lvl3_mtdef[];
extern const unsigned int lvl3_yorps[];
extern const unsigned char lvl0_tiles[],lvl0_mtflags[],lvl0_map[],lvl0_items[],lvl0_entries[];
extern const unsigned int lvl0_mtdef[];
extern const unsigned int lvl0_yorps[];
extern const unsigned char spr_keen[],spr_yorp[],spr_owk[],spr_blt[];
extern const unsigned char title_tiles[]; extern const unsigned int title_map[768]; extern const unsigned int title_ntiles;
#endif
