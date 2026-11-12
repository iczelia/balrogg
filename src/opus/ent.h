/*  Copyright (C) 2026 Kamila Szewczyk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 3.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef BLR_OPUSENT_H
#define BLR_OPUSENT_H

#include "opus/parse/opus_types.h"

#define OREC_OFF      0
#define OREC_ANALYZE  1
#define OREC_SYNTH    2

enum {
  OREC_S_NONE = 0,
  OREC_S_SILK_VAD = 1,
  OREC_S_SILK_LBRR_FLAG = 2,
  OREC_S_SILK_LBRR_SYM = 3,
  OREC_S_SILK_STEREO_PRED = 4,
  OREC_S_SILK_STEREO_IX0 = 5,
  OREC_S_SILK_STEREO_IX1 = 6,
  OREC_S_SILK_MID_ONLY = 7,
  OREC_S_SILK_TYPE_VAD = 8,
  OREC_S_SILK_TYPE_NOVAD = 9,
  OREC_S_SILK_GAIN_DELTA0 = 10,
  OREC_S_SILK_GAIN_MSB = 11,
  OREC_S_SILK_GAIN_LSB = 12,
  OREC_S_SILK_GAIN_DELTA = 13,
  OREC_S_SILK_NLSF_CB1 = 14,
  OREC_S_SILK_NLSF_RES = 15,
  OREC_S_SILK_NLSF_EXT_LO = 16,
  OREC_S_SILK_NLSF_EXT_HI = 17,
  OREC_S_SILK_NLSF_INTERP = 18,
  OREC_S_SILK_LAG_DELTA = 19,
  OREC_S_SILK_LAG_ABS = 20,
  OREC_S_SILK_LAG_LOW = 21,
  OREC_S_SILK_CONTOUR = 22,
  OREC_S_SILK_PER_INDEX = 23,
  OREC_S_SILK_LTP_GAIN = 24,
  OREC_S_SILK_LTP_SCALE = 25,
  OREC_S_SILK_SEED = 26,
  OREC_S_SILK_RATE_LEVEL = 27,
  OREC_S_SILK_PULSES = 28,
  OREC_S_SILK_PULSES_MORE = 29,
  OREC_S_SILK_LSB = 30,
  OREC_S_SILK_SHELL = 31,
  OREC_S_SILK_SIGN = 32,
  OREC_S_CELT_SILENCE = 40,
  OREC_S_CELT_PF_FLAG = 41,
  OREC_S_CELT_PF_OCTAVE = 42,
  OREC_S_CELT_PF_PITCH = 43,
  OREC_S_CELT_PF_GAIN = 44,
  OREC_S_CELT_PF_TAPSET = 45,
  OREC_S_CELT_TRANSIENT = 46,
  OREC_S_CELT_INTRA = 47,
  OREC_S_CELT_COARSE = 48,
  OREC_S_CELT_COARSE_SMALL = 49,
  OREC_S_CELT_COARSE_BIT = 50,
  OREC_S_CELT_TF = 51,
  OREC_S_CELT_TF_SELECT = 52,
  OREC_S_CELT_SPREAD = 53,
  OREC_S_CELT_DYNALLOC = 54,
  OREC_S_CELT_TRIM = 55,
  OREC_S_CELT_SKIP = 56,
  OREC_S_CELT_INTENSITY = 57,
  OREC_S_CELT_DUAL = 58,
  OREC_S_CELT_FINE = 59,
  OREC_S_CELT_THETA = 60,
  OREC_S_CELT_INV = 61,
  OREC_S_CELT_SIGN1 = 62,
  OREC_S_CELT_SIDE_SIGN = 63,
  OREC_S_CELT_PVQ = 64,
  OREC_S_CELT_ANTICOLLAPSE = 65,
  OREC_S_CELT_FINALISE = 66,
  OREC_S_OPUS_REDUNDANCY = 70,
  OREC_S_OPUS_CELT_TO_SILK = 71,
  OREC_S_OPUS_RED_BYTES = 72,
  OREC_S_COUNT = 73,
};

enum {
  OP_ICDF = 0,
  OP_ICDF16,
  OP_LOGP,
  OP_BITS,
  OP_UINT,
  OP_LAPLACE,
  OP_THETA,
  OP_NKINDS
};

typedef opus_uint64 okey;

typedef struct {
  int kind;
  int site;
  const void * pdf;
  opus_uint32 ftb;
  opus_uint32 nsym;
  opus_uint32 aux;
  opus_int32 v;
} oprec;

extern int orec_mode;
extern int orec_log, orec_lost;

extern int orec_band, orec_ch, orec_LM, orec_intra;
extern int orec_band1, orec_bLM;
extern int orec_pvqN, orec_pvqK, orec_ftb;
extern int orec_pvqoff, orec_pvqrole, orec_frame;
extern int orec_cframe;
extern int orec_spread, orec_trans;
extern int orec_thst;
extern int orec_th1;

extern int orec_fstart, orec_flen, orec_C, orec_trim;
extern int orec_bstart, orec_bend;
extern int orec_fq[], orec_fpri[];
extern int orec_pdep, orec_pB, orec_pb, orec_tfc;

opus_int32 om_icdf(oprec * op);
opus_int32 om_logp(oprec * op);
opus_int32 om_uint(oprec * op);
opus_int32 om_bits(oprec * op);
opus_int32 om_laplace(oprec * op);
opus_int32 om_angle(oprec * op);

struct ec_ctx;
int orec_laplace_decode(struct ec_ctx * ec, unsigned fs, int decay);
int orec_dec_theta(struct ec_ctx * ec, int qn, int stereo, int N, int B0);

void om_frame(unsigned char * buf, const unsigned char * mir, opus_uint32 n);

void oe_begin(void);
void oe_end(void);

void oe_reset(void);

#endif
