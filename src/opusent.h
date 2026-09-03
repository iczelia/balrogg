/*  Copyright (C) 2026 Kamila Szewczyk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 3.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.  */

#ifndef BLR_OPUSENT_H
#define BLR_OPUSENT_H

/*  Interface between the driven Opus range decoder and model.  */

#include "opus_types.h"

#define OREC_OFF      0
#define OREC_ANALYZE  1
#define OREC_SYNTH    2

/*  Stable symbol-site numbers used in model contexts and archive decoding.  */
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

/*  Operation kinds, one per entry point libopus offers.  */
enum {
  OP_ICDF = 0,  /*  ec_dec_icdf     symbol index, 8-bit inverse cdf   */
  OP_ICDF16,    /*  ec_dec_icdf16   symbol index, 16-bit inverse cdf  */
  OP_LOGP,      /*  ec_dec_bit_logp one bit, P(1) = 2^-logp           */
  OP_BITS,      /*  ec_dec_bits     raw bits, packed from the end     */
  OP_UINT,      /*  ec_dec_uint     uniform in [0, nsym)              */
  OP_LAPLACE,   /*  ec_laplace_decode, coarse energy                  */
  OP_THETA,     /*  bands.c band angle, integer in [0, qn]            */
  OP_NKINDS
};

/*  Pointer-derived tagged model key.  */
typedef opus_uint64 okey;

typedef struct {
  int kind;
  int site;          /*  OREC_S_*, which read this is  */
  const void * pdf;  /*  icdf pointer, or NULL  */
  opus_uint32 ftb;   /*  icdf: ftb; logp: logp; bits: nbits; laplace: decay  */
  opus_uint32 nsym;  /*  alphabet size where known (uint/theta), else 0  */
  opus_uint32 aux;   /*  kind-specific extra (laplace fs, theta N)  */
  opus_int32 v;      /*  value: in for ANALYZE, out for SYNTH  */
} oprec;

extern int orec_mode;

/*  Live CELT decoder context. orec_C is currently unused by the model.  */
extern int orec_band, orec_ch, orec_LM, orec_C, orec_intra;
extern int orec_pvqN, orec_pvqK, orec_ftb;

/*  The model, in opusmode.c.  Returns the value; in ANALYZE it echoes op->v.  */
opus_int32 om_op(oprec * op);

/*  CELT hooks for Laplace and band-angle decoding.  */
struct ec_ctx;
int orec_laplace_decode(struct ec_ctx * ec, unsigned fs, int decay);
int orec_dec_theta(struct ec_ctx * ec, int qn, int stereo, int N, int B0);

/*  Store or apply differences between an input frame and its reconstruction.  */
void om_frame(unsigned char * buf, const unsigned char * mir, opus_uint32 n);

/*  Per-packet bracket. oe_end finalizes any live entropy context.  */
void oe_begin(void);
void oe_end(void);

/*  Reset state that outlives a packet.  */
void oe_reset(void);

#endif
