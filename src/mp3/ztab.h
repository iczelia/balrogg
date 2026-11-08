#ifndef MP3_ZTAB_H
#define MP3_ZTAB_H

#include "mp3/zbase.h"

extern const u16 sfb_bounds[209];

extern u32 lsf_slen_tab[512], lsf_slen_is_tab[256];

extern const u32 * const HUFF_CODE[34];
extern const u32 * const HUFF_LIST[34];
extern const u8 * const HUFF_LEN[34];

extern const u8 count1a_trees[36];
#define count1a_tree0 (count1a_trees + 0)
#define count1a_tree1 (count1a_trees + 5)
#define count1a_tree2 (count1a_trees + 13)

extern u16 count1_codes[2][256];

extern const u8 huff_candidates[32][3];

void tables_init(void);

#endif
