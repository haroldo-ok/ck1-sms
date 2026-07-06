#ifndef GAME_DATA_H
#define GAME_DATA_H
#define F_SOLID  1
#define F_PLAT   2
#define F_DEADLY 4
#define BANK_SPRITES 2
#define BANK_TITLE 3
typedef struct {
  const unsigned char *tiles; unsigned int tiles_size;
  const unsigned int  *mtdef;
  const unsigned char *mtflags;
  const unsigned char *map;
  const unsigned char *items;   unsigned char nitems;
  const unsigned char *doors;   unsigned char ndoors;
  const unsigned char *ents;    unsigned char nents;
  const unsigned char *entries; unsigned char nentries;
  const unsigned char *exits;   unsigned char nexits;
  unsigned char bank, map_bank, nmt, W2;
  unsigned int W, H;
  int spawn_x, spawn_y;
} LevelDesc;
extern const LevelDesc level_descs[17];
extern const unsigned char bg_palette[16], spr_palette[16];
extern const unsigned char font_1bpp[472];
extern const unsigned char spr_keen[],spr_yorp[],spr_owk[],spr_blt[],spr_garg[],spr_vort[],spr_butler[],spr_tank[],spr_eray[],spr_chunk[];
extern const unsigned char title_tiles[];
extern const unsigned int title_map[], title_ntiles;
extern const unsigned char lvl0_tiles[],lvl0_mtflags[],lvl0_map[],lvl0_items[],lvl0_doors[],lvl0_ents[],lvl0_entries[],lvl0_exits[];
extern const unsigned int lvl0_mtdef[];
extern const unsigned char lvl1_tiles[],lvl1_mtflags[],lvl1_map[],lvl1_items[],lvl1_doors[],lvl1_ents[],lvl1_entries[],lvl1_exits[];
extern const unsigned int lvl1_mtdef[];
extern const unsigned char lvl2_tiles[],lvl2_mtflags[],lvl2_map[],lvl2_items[],lvl2_doors[],lvl2_ents[],lvl2_entries[],lvl2_exits[];
extern const unsigned int lvl2_mtdef[];
extern const unsigned char lvl3_tiles[],lvl3_mtflags[],lvl3_map[],lvl3_items[],lvl3_doors[],lvl3_ents[],lvl3_entries[],lvl3_exits[];
extern const unsigned int lvl3_mtdef[];
extern const unsigned char lvl4_tiles[],lvl4_mtflags[],lvl4_map[],lvl4_items[],lvl4_doors[],lvl4_ents[],lvl4_entries[],lvl4_exits[];
extern const unsigned int lvl4_mtdef[];
extern const unsigned char lvl5_tiles[],lvl5_mtflags[],lvl5_map[],lvl5_items[],lvl5_doors[],lvl5_ents[],lvl5_entries[],lvl5_exits[];
extern const unsigned int lvl5_mtdef[];
extern const unsigned char lvl6_tiles[],lvl6_mtflags[],lvl6_map[],lvl6_items[],lvl6_doors[],lvl6_ents[],lvl6_entries[],lvl6_exits[];
extern const unsigned int lvl6_mtdef[];
extern const unsigned char lvl7_tiles[],lvl7_mtflags[],lvl7_map[],lvl7_items[],lvl7_doors[],lvl7_ents[],lvl7_entries[],lvl7_exits[];
extern const unsigned int lvl7_mtdef[];
extern const unsigned char lvl8_tiles[],lvl8_mtflags[],lvl8_map[],lvl8_items[],lvl8_doors[],lvl8_ents[],lvl8_entries[],lvl8_exits[];
extern const unsigned int lvl8_mtdef[];
extern const unsigned char lvl9_tiles[],lvl9_mtflags[],lvl9_map[],lvl9_items[],lvl9_doors[],lvl9_ents[],lvl9_entries[],lvl9_exits[];
extern const unsigned int lvl9_mtdef[];
extern const unsigned char lvl10_tiles[],lvl10_mtflags[],lvl10_map[],lvl10_items[],lvl10_doors[],lvl10_ents[],lvl10_entries[],lvl10_exits[];
extern const unsigned int lvl10_mtdef[];
extern const unsigned char lvl11_tiles[],lvl11_mtflags[],lvl11_map[],lvl11_items[],lvl11_doors[],lvl11_ents[],lvl11_entries[],lvl11_exits[];
extern const unsigned int lvl11_mtdef[];
extern const unsigned char lvl12_tiles[],lvl12_mtflags[],lvl12_map[],lvl12_items[],lvl12_doors[],lvl12_ents[],lvl12_entries[],lvl12_exits[];
extern const unsigned int lvl12_mtdef[];
extern const unsigned char lvl13_tiles[],lvl13_mtflags[],lvl13_map[],lvl13_items[],lvl13_doors[],lvl13_ents[],lvl13_entries[],lvl13_exits[];
extern const unsigned int lvl13_mtdef[];
extern const unsigned char lvl14_tiles[],lvl14_mtflags[],lvl14_map[],lvl14_items[],lvl14_doors[],lvl14_ents[],lvl14_entries[],lvl14_exits[];
extern const unsigned int lvl14_mtdef[];
extern const unsigned char lvl15_tiles[],lvl15_mtflags[],lvl15_map[],lvl15_items[],lvl15_doors[],lvl15_ents[],lvl15_entries[],lvl15_exits[];
extern const unsigned int lvl15_mtdef[];
extern const unsigned char lvl16_tiles[],lvl16_mtflags[],lvl16_map[],lvl16_items[],lvl16_doors[],lvl16_ents[],lvl16_entries[],lvl16_exits[];
extern const unsigned int lvl16_mtdef[];
#endif
