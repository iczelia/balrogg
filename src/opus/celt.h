/* Copyright (c) 2007-2008 CSIRO
   Copyright (c) 2007-2010 Xiph.Org Foundation
   Copyright (c) 2008 Gregory Maxwell
   Written by Jean-Marc Valin and Gregory Maxwell */
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
   OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*  CELT parser from libopus with signal processing removed. Symbol-reading
    control flow retains the upstream structure.  */

#ifndef BLR_CELT_H
#define BLR_CELT_H

#include "arch.h"
#include "entdec.h"
#include "entenc.h"
#include "opusent.h"

#define MAX_PSEUDO 40
#define LOG_MAX_PSEUDO 6
#define MAX_FINE_BITS 8
#define FINE_OFFSET 21
#define QTHETA_OFFSET 4
#define QTHETA_OFFSET_TWOPHASE 16
#define SPREAD_NONE       (0)
#define SPREAD_LIGHT      (1)
#define SPREAD_NORMAL     (2)
#define SPREAD_AGGRESSIVE (3)

typedef struct {
   int size;
   const opus_int16 *index;
   const unsigned char *bits;
   const unsigned char *caps;
} PulseCache;

typedef struct {
   opus_int32 Fs;
   int nbEBands;
   int effEBands;
   const opus_int16 *eBands;
   int maxLM;
   int nbShortMdcts;
   int shortMdctSize;
   int nbAllocVectors;
   const unsigned char *allocVectors;
   const opus_int16 *logN;
   PulseCache cache;
} CELTMode;

/*  The one mode Opus uses: 48 kHz, 960-sample frames.  */
extern const CELTMode celt_mode48000_960_120;

/*  The decoder-state words the model reads, orec_* in opusent.h, are
    written by this parser at the points libopus's decoder knew them.  */

/*  celt_rate.c  */
static OPUS_INLINE int get_pulses(int i)
{
   return i<8 ? i : (8 + (i&7)) << ((i>>3)-1);
}

static OPUS_INLINE int bits2pulses(const CELTMode *m, int band, int LM, int bits)
{
   int i;
   int lo, hi;
   const unsigned char *cache;

   LM++;
   cache = m->cache.bits + m->cache.index[LM*m->nbEBands+band];

   lo = 0;
   hi = cache[0];
   bits--;
   for (i=0;i<LOG_MAX_PSEUDO;i++)
   {
      int mid = (lo+hi+1)>>1;
      if ((int)cache[mid] >= bits)
         hi = mid;
      else
         lo = mid;
   }
   if (bits- (lo == 0 ? -1 : (int)cache[lo]) <= (int)cache[hi]-bits)
      return lo;
   else
      return hi;
}

static OPUS_INLINE int pulses2bits(const CELTMode *m, int band, int LM, int pulses)
{
   const unsigned char *cache;

   LM++;
   cache = m->cache.bits + m->cache.index[LM*m->nbEBands+band];
   return pulses == 0 ? 0 : cache[pulses]+1;
}

int clt_compute_allocation(const CELTMode *m, int start, int end, const int *offsets, const int *cap, int alloc_trim, int *intensity, int *dual_stereo,
      opus_int32 total, opus_int32 *balance, int *pulses, int *ebits, int *fine_priority, int C, int LM, ec_dec *ec);

/*  celt_cwrs.c: read the PVQ index of an N-dimensional, K-pulse vector and
    unpack it into y.  */
void decode_pulses(int _n,int _k,ec_dec *_dec);

/*  celt_energy.c  */
void unquant_coarse_energy(const CELTMode *m, int start, int end, int intra, ec_dec *dec, int C, int LM);
void unquant_fine_energy(const CELTMode *m, int start, int end, const int *fine_quant, ec_dec *dec, int C);
void unquant_energy_finalise(const CELTMode *m, int start, int end, const int *fine_quant, const int *fine_priority, int bits_left, ec_dec *dec, int C);

/*  celt_laplace.c  */
void ec_laplace_encode(ec_enc *enc, int *value, unsigned fs, int decay);
int ec_laplace_decode(ec_dec *dec, unsigned fs, int decay);
int ec_laplace_decode_raw(ec_dec *dec, unsigned fs, int decay);

/*  celt_bands.c  */
void quant_all_bands(const CELTMode *m, int start, int end, int C,
      const int *pulses, int shortBlocks, int spread, int dual_stereo, int intensity,
      const int *tf_res, opus_int32 total_bits, opus_int32 balance, ec_dec *ec, int LM,
      int codedBands);

/*  celt_dec.c: the frame parser.  */
typedef struct {
   const CELTMode *mode;
   int channels;
   int stream_channels;
   int start, end;
   opus_uint32 rng;
} CELTDecoder;

void celt_decoder_init(CELTDecoder *st, int channels);
void celt_decoder_reset(CELTDecoder *st);
/*  Parse one frame of `len` bytes.  `dec` is the shared range decoder in
    hybrid mode, or NULL to open one over `data`.  Returns OPUS_OK or a
    negative error.  */
int celt_decode_with_ec(CELTDecoder *st, const unsigned char *data, int len, int frame_size, ec_dec *dec);

#endif
