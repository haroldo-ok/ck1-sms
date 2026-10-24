#include "game_data.h"
const FeTxt fe_txt[6] = {
  { 17, fe_pg_story, 161 },
  { 18, fe_pg_help, 81 },
  { 18, fe_pg_preview, 95 },
  { 18, fe_pg_endtext, 56 },
  { 18, fe_pg_order, 30 },
  { 18, fe_pg_menu, 7 },
};
const FeImg fe_img[13] = {
  { 19, fe_it_title, 363, fe_im_title, 32, 24 },
  { 20, fe_it_logo1, 75, fe_im_logo1, 32, 24 },
  { 20, fe_it_logo2, 54, fe_im_logo2, 32, 24 },
  { 20, fe_it_idabout, 55, fe_im_idabout, 32, 24 },
  { 20, fe_it_gameover, 35, fe_im_gameover, 32, 24 },
  { 21, fe_it_hiscore, 89, fe_im_hiscore, 32, 24 },
  { 21, fe_it_finale, 192, fe_im_finale, 32, 24 },
  { 22, fe_it_preview2, 154, fe_im_preview2, 32, 24 },
  { 22, fe_it_preview3, 202, fe_im_preview3, 32, 24 },
  { 22, fe_it_windon, 20, fe_im_windon, 7, 4 },
  { 22, fe_it_windoff, 20, fe_im_windoff, 7, 4 },
  { 23, fe_it_keenship, 50, fe_im_keenship, 15, 5 },
  { 23, fe_it_onemomen, 54, fe_im_onemomen, 11, 6 },
};
const unsigned int snd_off[41] = { 0,3,7,20,30,52,70,97,128,143,366,426,438,477,559,782,931,1068,1310,1421,1498,1720,1745,1822,1897,1995,2190,2342,2354,2401,2468,2587,2595,2876,2961,3134,3255,3316,3328,3478,3763 };
const unsigned char snd_pri[41] = { 128,128,128,200,128,128,160,180,150,255,200,200,190,190,255,240,230,255,255,250,250,200,200,250,250,255,200,250,200,128,128,200,255,255,200,250,250,250,255,255,255 };
const unsigned char bg_palette[16] = {
  0,32,8,40,2,34,6,42,21,53,29,61,23,55,31,63,
};
const unsigned char spr_palette[16] = {
  0,0,32,8,40,34,6,42,21,53,29,61,23,55,31,63,
};
const unsigned char font_1bpp[472] = {
  255,255,255,255,255,255,255,255,255,199,199,199,207,255,207,255,255,153,153,153,255,255,255,255,
  255,201,128,201,128,201,255,255,255,231,195,135,225,195,231,255,255,57,51,231,207,153,57,255,
  255,195,219,231,194,153,194,255,255,207,207,159,255,255,255,255,255,241,231,207,207,231,241,255,
  255,143,231,243,243,231,143,255,255,153,195,129,195,153,255,255,255,231,231,129,231,231,255,255,
  255,255,255,255,255,231,231,207,255,255,255,131,255,255,255,255,255,255,255,255,255,231,231,255,
  255,249,243,231,207,159,255,255,255,195,153,153,153,153,195,255,255,231,199,231,231,231,195,255,
  255,195,153,249,195,159,129,255,255,131,249,195,249,249,131,255,255,153,153,153,129,249,249,255,
  255,129,159,131,249,249,131,255,255,193,159,131,153,153,195,255,255,129,249,243,231,207,207,255,
  255,195,153,195,153,153,195,255,255,195,153,153,193,249,195,255,255,255,207,255,255,207,255,255,
  255,255,207,255,255,207,207,159,255,243,231,207,207,231,243,255,255,255,131,255,131,255,255,255,
  255,207,231,243,243,231,207,255,255,195,153,243,231,255,231,255,255,145,107,11,107,107,255,255,
  255,195,153,153,129,153,153,255,255,131,153,131,153,153,131,255,255,193,159,159,159,159,193,255,
  255,135,147,153,153,147,135,255,255,129,159,131,159,159,129,255,255,129,159,131,159,159,159,255,
  255,193,159,145,153,153,193,255,255,153,153,129,153,153,153,255,255,195,231,231,231,231,195,255,
  255,249,249,249,153,153,195,255,255,153,147,135,135,147,153,255,255,159,159,159,159,159,129,255,
  255,189,153,129,129,153,153,255,255,153,137,129,145,153,153,255,255,195,153,153,153,153,195,255,
  255,131,153,153,131,159,159,255,255,195,153,153,137,145,195,249,255,131,153,153,131,147,153,255,
  255,193,159,195,249,249,195,255,255,129,231,231,231,231,231,255,255,153,153,153,153,153,195,255,
  255,153,153,153,153,195,231,255,255,153,153,129,129,153,189,255,255,153,195,231,195,153,153,255,
  255,153,153,195,231,231,231,255,255,129,243,231,207,159,129,255,
};
const LevelDesc level_descs[18] = {
  { lvl0_tiles,10400, lvl0_mtdef,lvl0_mtflags,lvl0_map, lvl0_items,0, lvl0_doors,0, lvl0_ents,0, lvl0_entries,42, lvl0_exits,0, lvl0_teleports,3, lvl0_anim,5, 4,5,123,0, 70,73, 192,592 },
  { lvl1_tiles,5376, lvl1_mtdef,lvl1_mtflags,lvl1_map, lvl1_items,27, lvl1_doors,0, lvl1_ents,8, lvl1_entries,0, lvl1_exits,2, lvl1_teleports,0, lvl1_anim,28, 10,10,65,0, 120,21, 48,240 },
  { lvl2_tiles,1248, lvl2_mtdef,lvl2_mtflags,lvl2_map, lvl2_items,1, lvl2_doors,0, lvl2_ents,0, lvl2_entries,0, lvl2_exits,2, lvl2_teleports,0, lvl2_anim,8, 13,13,21,0, 24,30, 64,384 },
  { lvl3_tiles,5184, lvl3_mtdef,lvl3_mtflags,lvl3_map, lvl3_items,133, lvl3_doors,4, lvl3_ents,13, lvl3_entries,0, lvl3_exits,2, lvl3_teleports,0, lvl3_anim,34, 11,11,58,0, 81,53, 64,752 },
  { lvl4_tiles,4512, lvl4_mtdef,lvl4_mtflags,lvl4_map, lvl4_items,80, lvl4_doors,4, lvl4_ents,10, lvl4_entries,0, lvl4_exits,2, lvl4_teleports,0, lvl4_anim,18, 13,13,47,0, 120,24, 80,288 },
  { lvl5_tiles,2304, lvl5_mtdef,lvl5_mtflags,lvl5_map, lvl5_items,2, lvl5_doors,0, lvl5_ents,1, lvl5_entries,0, lvl5_exits,2, lvl5_teleports,0, lvl5_anim,20, 7,7,29,0, 24,37, 48,496 },
  { lvl6_tiles,1824, lvl6_mtdef,lvl6_mtflags,lvl6_map, lvl6_items,1, lvl6_doors,0, lvl6_ents,0, lvl6_entries,0, lvl6_exits,2, lvl6_teleports,0, lvl6_anim,24, 13,13,27,0, 24,40, 48,544 },
  { lvl7_tiles,7936, lvl7_mtdef,lvl7_mtflags,lvl7_map, lvl7_items,148, lvl7_doors,0, lvl7_ents,22, lvl7_entries,0, lvl7_exits,2, lvl7_teleports,0, lvl7_anim,72, 5,6,91,0, 128,64, 64,800 },
  { lvl8_tiles,6688, lvl8_mtdef,lvl8_mtflags,lvl8_map, lvl8_items,86, lvl8_doors,2, lvl8_ents,17, lvl8_entries,0, lvl8_exits,2, lvl8_teleports,0, lvl8_anim,7, 7,7,74,0, 80,60, 1088,864 },
  { lvl9_tiles,2368, lvl9_mtdef,lvl9_mtflags,lvl9_map, lvl9_items,17, lvl9_doors,2, lvl9_ents,1, lvl9_entries,0, lvl9_exits,2, lvl9_teleports,0, lvl9_anim,20, 11,11,31,0, 44,40, 48,544 },
  { lvl10_tiles,4192, lvl10_mtdef,lvl10_mtflags,lvl10_map, lvl10_items,31, lvl10_doors,2, lvl10_ents,4, lvl10_entries,0, lvl10_exits,2, lvl10_teleports,0, lvl10_anim,22, 10,10,53,0, 54,40, 48,544 },
  { lvl11_tiles,2304, lvl11_mtdef,lvl11_mtflags,lvl11_map, lvl11_items,3, lvl11_doors,0, lvl11_ents,1, lvl11_entries,0, lvl11_exits,2, lvl11_teleports,0, lvl11_anim,22, 12,12,32,0, 44,40, 48,544 },
  { lvl12_tiles,2496, lvl12_mtdef,lvl12_mtflags,lvl12_map, lvl12_items,46, lvl12_doors,0, lvl12_ents,18, lvl12_entries,0, lvl12_exits,2, lvl12_teleports,0, lvl12_anim,20, 6,6,33,0, 44,40, 48,544 },
  { lvl13_tiles,5696, lvl13_mtdef,lvl13_mtflags,lvl13_map, lvl13_items,124, lvl13_doors,8, lvl13_ents,23, lvl13_entries,0, lvl13_exits,2, lvl13_teleports,0, lvl13_anim,30, 4,9,72,0, 128,77, 80,800 },
  { lvl14_tiles,3264, lvl14_mtdef,lvl14_mtflags,lvl14_map, lvl14_items,173, lvl14_doors,0, lvl14_ents,30, lvl14_entries,0, lvl14_exits,2, lvl14_teleports,0, lvl14_anim,48, 14,14,36,0, 80,80, 48,1184 },
  { lvl15_tiles,5152, lvl15_mtdef,lvl15_mtflags,lvl15_map, lvl15_items,59, lvl15_doors,6, lvl15_ents,12, lvl15_entries,0, lvl15_exits,4, lvl15_teleports,0, lvl15_anim,24, 12,12,62,0, 142,24, 64,112 },
  { lvl16_tiles,6624, lvl16_mtdef,lvl16_mtflags,lvl16_map, lvl16_items,74, lvl16_doors,2, lvl16_ents,15, lvl16_entries,0, lvl16_exits,2, lvl16_teleports,0, lvl16_anim,24, 8,8,79,0, 110,72, 48,864 },
  { lvl17_tiles,2464, lvl17_mtdef,lvl17_mtflags,lvl17_map, lvl17_items,0, lvl17_doors,0, lvl17_ents,0, lvl17_entries,0, lvl17_exits,0, lvl17_teleports,0, lvl17_anim,4, 9,9,29,0, 62,34, 32,32 },
};
