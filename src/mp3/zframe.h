#ifndef MP3_ZFRAME_H
#define MP3_ZFRAME_H

#include "mp3/zbase.h"
#include "mp3/ztab.h"

#define MP3_LINES        576
#define MP3_HEADER_BYTES 4
#define MP3_CRC_BYTES    2
#define MP3_SAMPLES_MPEG1 1152
#define MP3_SAMPLES_LSF   576
#define MP3_SCF_COUNT    78
#define SCF_SHORT 39

#define BLOCK_LONG  0
#define BLOCK_START 1
#define BLOCK_SHORT 2
#define BLOCK_STOP  3

typedef struct {
  i32 part2_length;
  i32 raw_part2_3_length;
  i32 part2_3_length;
  i32 big_values;
  i32 global_gain;
  i32 scalefac_compress;
  i32 window_switching;
  i32 block_type;
  i32 mixed_block;
  i32 table_select[3];
  i32 subblock_gain[3];
  i32 region0_count;
  i32 region1_count;
  i32 preflag;
  i32 scalefac_scale;
  i32 count1table_select;
} granule;

typedef struct {
  u32 header;
  i32 main_data_begin;
  i32 main_data_begin_used;
  i32 private_bits;
  i32 scfsi[2][4];
  i32 raw;
  granule gr[2][2];
  u16 scalefac[2][2][MP3_SCF_COUNT];
  i16 coef[2][2][MP3_LINES];
  u16 crc_computed;
} mp3_frame;

#define HDR_SYNC_MASK      0xffe00000u
#define HDR_LAYER_III      1
#define HDR_MODE_MONO      3
#define HDR_BITRATE_BAD    15
#define HDR_RATE_RESERVED  3
#define HDR_MODE_EXT_IS    1

static inline u32 hdr_emphasis(u32 h) { return h & 3; }
static inline u32 hdr_original(u32 h) { return (h >> 2) & 1; }
static inline u32 hdr_copyright(u32 h) { return (h >> 3) & 1; }
static inline u32 hdr_mode_ext(u32 h) { return (h >> 4) & 3; }
static inline u32 hdr_mode(u32 h) { return (h >> 6) & 3; }
static inline u32 hdr_private(u32 h) { return (h >> 8) & 1; }
static inline u32 hdr_padding(u32 h) { return (h >> 9) & 1; }
static inline u32 hdr_samplerate(u32 h) { return (h >> 10) & 3; }
static inline u32 hdr_bitrate(u32 h) { return (h >> 12) & 15; }
static inline u32 hdr_no_crc(u32 h) { return (h >> 16) & 1; }
static inline u32 hdr_layer(u32 h) { return (h >> 17) & 3; }
static inline u32 hdr_mpeg1(u32 h) { return (h >> 19) & 1; }
static inline u32 hdr_not_mpeg25(u32 h) { return (h >> 20) & 1; }
static inline int hdr_channels(u32 h) {
  return hdr_mode(h) == HDR_MODE_MONO ? 1 : 2;
}
static inline int hdr_granules(u32 h) { return hdr_mpeg1(h) ? 2 : 1; }

static inline int mp3_header_valid(u32 h) {
  if ((h & HDR_SYNC_MASK) != HDR_SYNC_MASK) return 0;
  if (hdr_mpeg1(h) && !hdr_not_mpeg25(h)) return 0;
  if (hdr_layer(h) != HDR_LAYER_III) return 0;
  return hdr_bitrate(h) != HDR_BITRATE_BAD &&
         hdr_samplerate(h) != HDR_RATE_RESERVED;
}

#define FREE_MAX_BYTES 1441

static inline i32 hdr_bitrate_bps(u32 h) {
  return mp3_bitrate[hdr_mpeg1(h) * 15 + hdr_bitrate(h)];
}

static inline i32 hdr_samplerate_hz(u32 h) {
  u32 rate = hdr_not_mpeg25(h) << 2 | hdr_samplerate(h);
  return mp3_rate_hz[hdr_mpeg1(h)][rate];
}

static inline i32 hdr_samples(u32 h) {
  return hdr_mpeg1(h) ? MP3_SAMPLES_MPEG1 : MP3_SAMPLES_LSF;
}

static inline i32 mp3_frame_bytes(u32 h) {
  return hdr_samples(h) / 8 * hdr_bitrate_bps(h) / hdr_samplerate_hz(h) +
         (i32) hdr_padding(h);
}

static inline int mp3_side_info_bytes(u32 h) {
  return mp3_side_len[(hdr_channels(h) == 2) * 2 + hdr_mpeg1(h)];
}

static inline i32 header_bytes(u32 h) {
  return MP3_HEADER_BYTES + (hdr_no_crc(h) ? 0 : MP3_CRC_BYTES) +
         mp3_side_info_bytes(h);
}

#define MP3_LONG_BANDS 22

static inline int hdr_rate_index(u32 h) {
  return (int) (hdr_samplerate(h) + (!hdr_mpeg1(h) + !hdr_not_mpeg25(h)) * 3);
}

static inline const u16 * long_band_bounds(u32 h) {
  return sfb_bounds + hdr_rate_index(h) * (MP3_LONG_BANDS + 1);
}

static inline i32 part2_3_total(const mp3_frame * f) {
  u32 s = 0;
  int i, j;
  Fi(hdr_granules(f->header),
    Fj(hdr_channels(f->header), s += (u32) f->gr[i][j].part2_3_length));
  return (i32) s;
}

typedef struct {
  i32 end0, end1, big_end;
} regions;

static inline regions granule_regions(u32 h, const granule * g) {
  const u16 * bounds = long_band_bounds(h);
  int band = MIN(g->region0_count + 2 + g->region1_count, MP3_LONG_BANDS);
  regions r;
  r.big_end = g->big_values * 2;
  r.end0 = MIN(bounds[g->region0_count + 1], r.big_end);
  r.end1 = MIN(bounds[band], r.big_end);
  return r;
}

#endif
