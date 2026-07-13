#include "game_data.h"
const unsigned char bg_palette[16] = {
  0,32,8,40,2,34,6,42,21,53,29,61,23,55,31,63,
};
const unsigned char spr_palette[16] = {
  0,0,32,8,40,34,6,42,21,53,29,61,23,55,31,63,
};
const unsigned char font_1bpp[472] = {
  0,0,0,0,0,0,0,0,0,64,64,64,64,64,64,0,0,96,96,96,0,0,0,0,
  0,40,40,32,120,48,120,80,16,56,124,84,112,56,20,84,0,228,172,168,80,20,42,106,
  0,56,64,68,62,100,68,68,0,128,128,128,0,0,0,0,0,32,64,64,64,64,64,64,
  0,128,64,64,64,64,64,64,0,0,0,0,16,16,56,40,0,0,0,16,16,124,16,16,
  0,0,0,0,0,0,0,0,0,0,0,0,0,192,0,0,0,0,0,0,0,0,0,0,
  0,32,32,0,64,64,64,128,0,112,216,136,136,136,136,216,0,48,112,16,16,16,16,16,
  0,112,72,8,8,16,32,64,0,112,200,8,48,8,136,136,0,8,24,40,40,72,124,8,
  0,120,64,128,240,200,8,136,0,112,200,136,240,136,136,136,0,248,24,16,16,32,32,64,
  0,112,136,136,112,136,136,136,0,112,136,136,136,120,136,152,0,0,0,128,0,0,0,0,
  0,0,0,128,0,0,0,128,0,0,0,0,24,96,96,16,0,0,0,0,120,0,120,0,
  0,0,0,0,96,24,24,32,0,96,144,16,16,32,32,0,0,14,49,46,90,82,86,79,
  0,48,56,168,168,248,196,68,0,120,68,68,64,124,68,68,0,120,68,132,128,128,132,68,
  0,120,68,66,66,66,66,68,0,124,64,64,64,120,64,64,0,124,64,64,64,120,64,64,
  0,120,76,132,128,156,132,76,0,66,66,66,66,126,66,66,0,64,64,64,64,64,64,64,
  0,8,8,8,8,8,72,72,0,68,72,88,112,112,88,72,0,64,64,64,64,64,64,64,
  0,99,99,99,85,85,85,93,0,98,98,82,82,74,74,70,0,120,68,130,130,130,130,68,
  0,120,68,68,68,120,64,64,0,120,68,130,130,130,130,68,0,120,68,68,68,120,76,68,
  0,56,68,64,48,28,4,68,0,124,16,16,16,16,16,16,0,68,68,68,68,68,68,68,
  0,196,68,68,72,40,40,48,0,140,204,76,84,87,83,51,0,196,200,168,176,48,40,72,
  0,68,68,40,56,16,16,16,0,124,12,8,16,16,32,96,
};
const LevelDesc level_descs[17] = {
  { lvl0_tiles,10336, lvl0_mtdef,lvl0_mtflags,lvl0_map, lvl0_items,0, lvl0_doors,0, lvl0_ents,0, lvl0_entries,40, lvl0_exits,0, lvl0_teleports,2, 4,5,123,0, 70,73, 192,592 },
  { lvl1_tiles,4992, lvl1_mtdef,lvl1_mtflags,lvl1_map, lvl1_items,27, lvl1_doors,0, lvl1_ents,8, lvl1_entries,0, lvl1_exits,2, lvl1_teleports,0, 5,5,62,0, 120,21, 48,240 },
  { lvl2_tiles,1120, lvl2_mtdef,lvl2_mtflags,lvl2_map, lvl2_items,0, lvl2_doors,0, lvl2_ents,0, lvl2_entries,0, lvl2_exits,2, lvl2_teleports,0, 11,11,20,0, 24,30, 64,384 },
  { lvl3_tiles,4544, lvl3_mtdef,lvl3_mtflags,lvl3_map, lvl3_items,133, lvl3_doors,4, lvl3_ents,13, lvl3_entries,0, lvl3_exits,2, lvl3_teleports,0, 11,11,53,0, 81,53, 64,752 },
  { lvl4_tiles,4384, lvl4_mtdef,lvl4_mtflags,lvl4_map, lvl4_items,80, lvl4_doors,4, lvl4_ents,10, lvl4_entries,0, lvl4_exits,2, lvl4_teleports,0, 12,12,46,0, 120,24, 80,288 },
  { lvl5_tiles,1920, lvl5_mtdef,lvl5_mtflags,lvl5_map, lvl5_items,2, lvl5_doors,0, lvl5_ents,1, lvl5_entries,0, lvl5_exits,2, lvl5_teleports,0, 6,6,26,0, 24,37, 48,496 },
  { lvl6_tiles,1312, lvl6_mtdef,lvl6_mtflags,lvl6_map, lvl6_items,0, lvl6_doors,0, lvl6_ents,0, lvl6_entries,0, lvl6_exits,2, lvl6_teleports,0, 12,12,23,0, 24,40, 48,544 },
  { lvl7_tiles,6528, lvl7_mtdef,lvl7_mtflags,lvl7_map, lvl7_items,148, lvl7_doors,0, lvl7_ents,22, lvl7_entries,0, lvl7_exits,2, lvl7_teleports,0, 7,7,80,0, 128,64, 64,800 },
  { lvl8_tiles,6688, lvl8_mtdef,lvl8_mtflags,lvl8_map, lvl8_items,86, lvl8_doors,2, lvl8_ents,17, lvl8_entries,0, lvl8_exits,2, lvl8_teleports,0, 6,6,74,0, 80,60, 1088,864 },
  { lvl9_tiles,1984, lvl9_mtdef,lvl9_mtflags,lvl9_map, lvl9_items,16, lvl9_doors,2, lvl9_ents,1, lvl9_entries,0, lvl9_exits,2, lvl9_teleports,0, 11,11,28,0, 44,40, 48,544 },
  { lvl10_tiles,3808, lvl10_mtdef,lvl10_mtflags,lvl10_map, lvl10_items,30, lvl10_doors,2, lvl10_ents,4, lvl10_entries,0, lvl10_exits,2, lvl10_teleports,0, 10,10,50,0, 54,40, 48,544 },
  { lvl11_tiles,1856, lvl11_mtdef,lvl11_mtflags,lvl11_map, lvl11_items,2, lvl11_doors,0, lvl11_ents,1, lvl11_entries,0, lvl11_exits,2, lvl11_teleports,0, 12,12,28,0, 44,40, 48,544 },
  { lvl12_tiles,2112, lvl12_mtdef,lvl12_mtflags,lvl12_map, lvl12_items,45, lvl12_doors,0, lvl12_ents,18, lvl12_entries,0, lvl12_exits,2, lvl12_teleports,0, 9,9,30,0, 44,40, 48,544 },
  { lvl13_tiles,5312, lvl13_mtdef,lvl13_mtflags,lvl13_map, lvl13_items,122, lvl13_doors,8, lvl13_ents,23, lvl13_entries,0, lvl13_exits,2, lvl13_teleports,0, 4,9,69,0, 128,77, 80,800 },
  { lvl14_tiles,2624, lvl14_mtdef,lvl14_mtflags,lvl14_map, lvl14_items,173, lvl14_doors,0, lvl14_ents,30, lvl14_entries,0, lvl14_exits,2, lvl14_teleports,0, 13,13,31,0, 80,80, 48,1184 },
  { lvl15_tiles,4640, lvl15_mtdef,lvl15_mtflags,lvl15_map, lvl15_items,58, lvl15_doors,6, lvl15_ents,12, lvl15_entries,0, lvl15_exits,4, lvl15_teleports,0, 10,10,58,0, 142,24, 64,112 },
  { lvl16_tiles,6240, lvl16_mtdef,lvl16_mtflags,lvl16_map, lvl16_items,74, lvl16_doors,2, lvl16_ents,15, lvl16_entries,0, lvl16_exits,2, lvl16_teleports,0, 8,8,76,0, 110,72, 48,864 },
};
