#include "mp3/znames.h"
#include "mp3/zcodec.c"
#include "mp3/zhufftab.c"
#include "mp3/zhuff.c"
#include "mp3/zcoef.c"
#include "mp3/zmframe.c"
#undef RATES
#undef BIAS
#include "mp3/zlsf.c"
#include "mp3/zpart23.c"
#undef RATES
#undef BIAS
#include "mp3/zscf.c"
#define code_field side_code_field
#include "mp3/zside.c"
#undef code_field
#undef RATES
#undef BIAS
#define code_regions frame_code_regions
#define code_granule frame_code_granule
#include "mp3/zframe.c"
#undef code_regions
#undef code_granule
#include "mp3/zread.c"
#include "mp3/zwrite.c"
#include "mp3/zhdr.c"
#include "mp3/zrc.c"
#include "mp3/zstdtab.c"
#include "mp3/ztab.c"
#include "mp3/zapi.c"
