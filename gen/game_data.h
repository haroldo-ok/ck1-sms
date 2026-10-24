#ifndef GAME_DATA_H
#define GAME_DATA_H
#define BANK_TXT 16
#define TXT_HINT2 0
#define TXT_HINT6 1
#define TXT_HINT9 2
#define TXT_HINT10 3
#define TXT_HINT11 4
#define TXT_HINT12 5
#define TXT_HINT15 6
#define TXT_SHIP 7
#define TXT_LIVES 8
#define TXT_STATUS 9
#define TXT_CHEAT 10
#define TXT_E1 11
#define TXT_E2 12
#define TXT_E3 13
#define TXT_E4 14
#define TXT_E5 15
#define TXT_E6 16
#define TXT_E7 17
#define TXT_E8 18
#define TXT_E9 19
#define TXT_TBC 20
extern const unsigned char dlg_f0[], dlg_f1[], dlg_frame[], dlg_icons[], dlg_keen[], txt_data[];
extern const unsigned int txt_off[];
#define BANK_SND 15
#define TELE_DX 62
#define TELE_DY 37
#define K_HINT 15
#define K_TELE 16
#define F_UP 1
#define F_CEIL 2
#define F_BLKR 4
#define F_BLKL 8
#define F_DEADLY 16
#define F_ICE 32
#define F_ICE3 64
#define BANK_SPRITES 2
#define BANK_SPR2 3
#define LVL_FLY 17
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
  const unsigned char *teleports; unsigned char nteleports;
  const unsigned char *anim; unsigned char nanim;
  unsigned char bank, map_bank, nmt, W2;
  unsigned int W, H;
  int spawn_x, spawn_y;
} LevelDesc;
extern const LevelDesc level_descs[18];
extern const unsigned char bg_palette[16], spr_palette[16];
extern const unsigned char font_1bpp[472];
#define SND_WALK1 0
#define SND_WALK2 1
#define SND_WLDWALK 2
#define SND_BLOK 3
#define SND_JUMP 4
#define SND_LAND 5
#define SND_POGO 6
#define SND_POGOJUMP 7
#define SND_BUMPHEAD 8
#define SND_DIE 9
#define SND_FIRE 10
#define SND_GUNCLICK 11
#define SND_BONUS 12
#define SND_ITEM 13
#define SND_PART 14
#define SND_CARD 15
#define SND_DOOR 16
#define SND_LVLDONE 17
#define SND_WLDENTER 18
#define SND_GOINDOOR 19
#define SND_TELEPORT 20
#define SND_SHOTHIT 21
#define SND_YORPBOP 22
#define SND_YORPSCREAM 23
#define SND_GARGSCREAM 24
#define SND_VORTSCREAM 25
#define SND_KEENCICLE 26
#define SND_YORPBUMP 27
#define SND_TANKFIRE 28
#define SND_CANNON 29
#define SND_PLUMMET 30
#define SND_WLDBLOCK 31
#define SND_GAMEOVER 32
#define SND_HISCORE 33
#define SND_CHUNKSMASH 34
#define SND_USEKEY 35
#define SND_SLAM 36
#define SND_CLICK 37
#define SND_CRYSTAL 38
#define SND_EXTRAMAN 39
#define SND_KEENSLEFT 40
extern const unsigned int snd_data[];
extern const unsigned int snd_off[];
extern const unsigned char snd_pri[];
extern const unsigned char spr_keen[],spr_yorp[],spr_owk[],spr_blt[],spr_garg[],spr_vort[],spr_butler[],spr_tank[],spr_eray[],spr_chunk[],spr_ship[],anim_art[];
extern const unsigned char lvl0_tiles[],lvl0_mtflags[],lvl0_map[],lvl0_items[],lvl0_doors[],lvl0_ents[],lvl0_entries[],lvl0_exits[],lvl0_teleports[],lvl0_anim[];
extern const unsigned int lvl0_mtdef[];
extern const unsigned char lvl1_tiles[],lvl1_mtflags[],lvl1_map[],lvl1_items[],lvl1_doors[],lvl1_ents[],lvl1_entries[],lvl1_exits[],lvl1_teleports[],lvl1_anim[];
extern const unsigned int lvl1_mtdef[];
extern const unsigned char lvl2_tiles[],lvl2_mtflags[],lvl2_map[],lvl2_items[],lvl2_doors[],lvl2_ents[],lvl2_entries[],lvl2_exits[],lvl2_teleports[],lvl2_anim[];
extern const unsigned int lvl2_mtdef[];
extern const unsigned char lvl3_tiles[],lvl3_mtflags[],lvl3_map[],lvl3_items[],lvl3_doors[],lvl3_ents[],lvl3_entries[],lvl3_exits[],lvl3_teleports[],lvl3_anim[];
extern const unsigned int lvl3_mtdef[];
extern const unsigned char lvl4_tiles[],lvl4_mtflags[],lvl4_map[],lvl4_items[],lvl4_doors[],lvl4_ents[],lvl4_entries[],lvl4_exits[],lvl4_teleports[],lvl4_anim[];
extern const unsigned int lvl4_mtdef[];
extern const unsigned char lvl5_tiles[],lvl5_mtflags[],lvl5_map[],lvl5_items[],lvl5_doors[],lvl5_ents[],lvl5_entries[],lvl5_exits[],lvl5_teleports[],lvl5_anim[];
extern const unsigned int lvl5_mtdef[];
extern const unsigned char lvl6_tiles[],lvl6_mtflags[],lvl6_map[],lvl6_items[],lvl6_doors[],lvl6_ents[],lvl6_entries[],lvl6_exits[],lvl6_teleports[],lvl6_anim[];
extern const unsigned int lvl6_mtdef[];
extern const unsigned char lvl7_tiles[],lvl7_mtflags[],lvl7_map[],lvl7_items[],lvl7_doors[],lvl7_ents[],lvl7_entries[],lvl7_exits[],lvl7_teleports[],lvl7_anim[];
extern const unsigned int lvl7_mtdef[];
extern const unsigned char lvl8_tiles[],lvl8_mtflags[],lvl8_map[],lvl8_items[],lvl8_doors[],lvl8_ents[],lvl8_entries[],lvl8_exits[],lvl8_teleports[],lvl8_anim[];
extern const unsigned int lvl8_mtdef[];
extern const unsigned char lvl9_tiles[],lvl9_mtflags[],lvl9_map[],lvl9_items[],lvl9_doors[],lvl9_ents[],lvl9_entries[],lvl9_exits[],lvl9_teleports[],lvl9_anim[];
extern const unsigned int lvl9_mtdef[];
extern const unsigned char lvl10_tiles[],lvl10_mtflags[],lvl10_map[],lvl10_items[],lvl10_doors[],lvl10_ents[],lvl10_entries[],lvl10_exits[],lvl10_teleports[],lvl10_anim[];
extern const unsigned int lvl10_mtdef[];
extern const unsigned char lvl11_tiles[],lvl11_mtflags[],lvl11_map[],lvl11_items[],lvl11_doors[],lvl11_ents[],lvl11_entries[],lvl11_exits[],lvl11_teleports[],lvl11_anim[];
extern const unsigned int lvl11_mtdef[];
extern const unsigned char lvl12_tiles[],lvl12_mtflags[],lvl12_map[],lvl12_items[],lvl12_doors[],lvl12_ents[],lvl12_entries[],lvl12_exits[],lvl12_teleports[],lvl12_anim[];
extern const unsigned int lvl12_mtdef[];
extern const unsigned char lvl13_tiles[],lvl13_mtflags[],lvl13_map[],lvl13_items[],lvl13_doors[],lvl13_ents[],lvl13_entries[],lvl13_exits[],lvl13_teleports[],lvl13_anim[];
extern const unsigned int lvl13_mtdef[];
extern const unsigned char lvl14_tiles[],lvl14_mtflags[],lvl14_map[],lvl14_items[],lvl14_doors[],lvl14_ents[],lvl14_entries[],lvl14_exits[],lvl14_teleports[],lvl14_anim[];
extern const unsigned int lvl14_mtdef[];
extern const unsigned char lvl15_tiles[],lvl15_mtflags[],lvl15_map[],lvl15_items[],lvl15_doors[],lvl15_ents[],lvl15_entries[],lvl15_exits[],lvl15_teleports[],lvl15_anim[];
extern const unsigned int lvl15_mtdef[];
extern const unsigned char lvl16_tiles[],lvl16_mtflags[],lvl16_map[],lvl16_items[],lvl16_doors[],lvl16_ents[],lvl16_entries[],lvl16_exits[],lvl16_teleports[],lvl16_anim[];
extern const unsigned int lvl16_mtdef[];
extern const unsigned char lvl17_tiles[],lvl17_mtflags[],lvl17_map[],lvl17_items[],lvl17_doors[],lvl17_ents[],lvl17_entries[],lvl17_exits[],lvl17_teleports[],lvl17_anim[];
extern const unsigned int lvl17_mtdef[];
extern const unsigned char fe_t8[], fe_font_grey[];
extern const unsigned char fe_pg_story[];
#define PG_STORY 0
extern const unsigned char fe_pg_help[];
#define PG_HELP 1
extern const unsigned char fe_pg_preview[];
#define PG_PREVIEW 2
extern const unsigned char fe_pg_endtext[];
#define PG_ENDTEXT 3
extern const unsigned char fe_pg_order[];
#define PG_ORDER 4
extern const unsigned char fe_pg_menu[];
#define PG_MENU 5
extern const unsigned char fe_scores_default[];
extern const unsigned char fe_it_title[];
extern const unsigned int fe_im_title[];
#define IMG_TITLE 0
extern const unsigned char fe_it_logo1[];
extern const unsigned int fe_im_logo1[];
#define IMG_LOGO1 1
extern const unsigned char fe_it_logo2[];
extern const unsigned int fe_im_logo2[];
#define IMG_LOGO2 2
extern const unsigned char fe_it_idabout[];
extern const unsigned int fe_im_idabout[];
#define IMG_IDABOUT 3
extern const unsigned char fe_it_gameover[];
extern const unsigned int fe_im_gameover[];
#define IMG_GAMEOVER 4
extern const unsigned char fe_it_hiscore[];
extern const unsigned int fe_im_hiscore[];
#define IMG_HISCORE 5
extern const unsigned char fe_it_finale[];
extern const unsigned int fe_im_finale[];
#define IMG_FINALE 6
extern const unsigned char fe_it_preview2[];
extern const unsigned int fe_im_preview2[];
#define IMG_PREVIEW2 7
extern const unsigned char fe_it_preview3[];
extern const unsigned int fe_im_preview3[];
#define IMG_PREVIEW3 8
extern const unsigned char fe_it_windon[];
extern const unsigned int fe_im_windon[];
#define IMG_WINDON 9
extern const unsigned char fe_it_windoff[];
extern const unsigned int fe_im_windoff[];
#define IMG_WINDOFF 10
extern const unsigned char fe_it_keenship[];
extern const unsigned int fe_im_keenship[];
#define IMG_KEENSHIP 11
extern const unsigned char fe_it_onemomen[];
extern const unsigned int fe_im_onemomen[];
#define IMG_ONEMOMEN 12
typedef struct { unsigned char bank; const unsigned char *rows; unsigned int nrows; } FeTxt;
typedef struct { unsigned char bank; const unsigned char *tiles; unsigned int ntiles; const unsigned int *map; unsigned char w, h; } FeImg;
extern const FeTxt fe_txt[6];
extern const FeImg fe_img[13];
#define BANK_FE_T8 17
#define BANK_FE_GREY 17
#define BANK_FE_SCORES 18
#define BANK_OVL_FRONT 24
#endif
