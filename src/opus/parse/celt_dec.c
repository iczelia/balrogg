/* Copyright (c) 2007-2008 CSIRO
   Copyright (c) 2007-2009 Xiph.Org Foundation
   Copyright (c) 2007-2009 Timothy B. Terriberry
   Written by Timothy B. Terriberry and Jean-Marc Valin */
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

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

#include "celt.h"

int orec_band, orec_ch, orec_LM, orec_intra, orec_band1, orec_bLM;
int orec_pvqN, orec_pvqK, orec_ftb;
int orec_cframe;
int orec_spread, orec_trans;
int orec_fstart, orec_flen, orec_C, orec_trim, orec_bstart, orec_bend;
int orec_fq[32], orec_fpri[32];

static const unsigned char trim_icdf[11] = {126, 124, 119, 109, 87, 41, 19, 9, 4, 2, 0};
static const unsigned char spread_icdf[4] = {25, 23, 2, 0};
static const unsigned char tapset_icdf[3]={2,1,0};

static const signed char tf_select_table[4][8] = {
      {0, -1, 0, -1,    0,-1, 0,-1},
      {0, -1, 0, -2,    1, 0, 1,-1},
      {0, -2, 0, -3,    2, 0, 1,-1},
      {0, -2, 0, -3,    3, 0, 1,-1},
};

static void init_caps(const CELTMode *m,int *cap,int LM,int C)
{
   int i;
   for (i=0;i<m->nbEBands;i++)
   {
      int N;
      N=(m->eBands[i+1]-m->eBands[i])<<LM;
      cap[i] = (m->cache.caps[m->nbEBands*(2*LM+C-1)+i]+64)*C*N>>2;
   }
}

static void tf_decode(int start, int end, int isTransient, int *tf_res, int LM, ec_dec *dec)
{
   int i, curr, tf_select;
   int tf_select_rsv;
   int tf_changed;
   int logp;
   opus_uint32 budget;
   opus_uint32 tell;

   budget = dec->storage*8;
   tell = ec_tell(dec);
   logp = isTransient ? 2 : 4;
   tf_select_rsv = LM>0 && tell+logp+1<=budget;
   budget -= tf_select_rsv;
   tf_changed = curr = 0;
   for (i=start;i<end;i++)
   {
      orec_band = i;
      if (tell+logp<=budget)
      {
         curr ^= ec_dec_bit_logp(dec, logp, OREC_S_CELT_TF);
         tell = ec_tell(dec);
         tf_changed |= curr;
      }
      tf_res[i] = curr;
      logp = isTransient ? 4 : 5;
   }
   tf_select = 0;
   if (tf_select_rsv &&
     tf_select_table[LM][4*isTransient+0+tf_changed] !=
     tf_select_table[LM][4*isTransient+2+tf_changed])
   {
      tf_select = ec_dec_bit_logp(dec, 1, OREC_S_CELT_TF_SELECT);
   }
   for (i=start;i<end;i++)
   {
      tf_res[i] = tf_select_table[LM][4*isTransient+2*tf_select+tf_res[i]];
   }
}

unsigned char celt_b2p[CELT_B2P_ROWS][256];
int celt_split[CELT_B2P_ROWS];

static void celt_b2p_build(const CELTMode *m)
{
   static int built;
   int r, b, j, n, t;
   if (built) return;
   celt_assert((m->maxLM+2)*m->nbEBands <= CELT_B2P_ROWS);
   for (r=0;r<(m->maxLM+2)*m->nbEBands;r++)
   {
      const unsigned char *cache;
      if (m->cache.index[r] < 0) continue;
      cache = m->cache.bits + m->cache.index[r];
      n = cache[0];
      celt_split[r] = cache[n]+12;
      if (n < 1 || n > 1<<LOG_MAX_PSEUDO)
      {
         for (b=0;b<256;b++)
            celt_b2p[r][b] = (unsigned char) bits2pulses_search(m, r%m->nbEBands, r/m->nbEBands-1, b);
         continue;
      }
      for (b=0,j=1;b<256;b++)
      {
         t = b-1;
         while (j < n && (int)cache[j] < t) j++;
         celt_b2p[r][b] = (unsigned char) (t-(j == 1 ? -1 : (int)cache[j-1]) <= (int)cache[j]-t ? j-1 : j);
      }
   }
   built = 1;
}

void celt_decoder_init(CELTDecoder *st, int channels)
{
   st->mode = &celt_mode48000_960_120;
   st->stream_channels = st->channels = channels;
   st->start = 0;
   st->end = st->mode->effEBands;
   st->rng = 0;
}

void celt_decoder_reset(CELTDecoder *st)
{
   st->rng = 0;
}

#define CELT_MAX_BANDS 21

int celt_decode_with_ec(CELTDecoder *st, const unsigned char *data, int len, int frame_size, ec_dec *dec)
{
   int i;
   int spread_decision;
   opus_int32 bits;
   int fine_quant[CELT_MAX_BANDS];
   int pulses[CELT_MAX_BANDS];
   int cap[CELT_MAX_BANDS];
   int offsets[CELT_MAX_BANDS];
   int fine_priority[CELT_MAX_BANDS];
   int tf_res[CELT_MAX_BANDS];
   int shortBlocks;
   int isTransient;
   int intra_ener;
   int LM, M;
   int start;
   int end;
   int codedBands;
   int alloc_trim;
   int intensity=0;
   int dual_stereo=0;
   opus_int32 total_bits;
   opus_int32 balance;
   opus_int32 tell;
   int dynalloc_logp;
   int anti_collapse_rsv;
   int silence;
   int C = st->stream_channels;
   const CELTMode *mode;
   const opus_int16 *eBands;

   mode = st->mode;
   celt_b2p_build(mode);
   eBands = mode->eBands;
   start = st->start;
   end = st->end;

   for (LM=0;LM<=mode->maxLM;LM++)
      if (mode->shortMdctSize<<LM==frame_size)
         break;
   if (LM>mode->maxLM)
      return OPUS_BAD_ARG;
   M=1<<LM;

   if (len<0 || len>1275)
      return OPUS_BAD_ARG;

   if (data == NULL || len<=1)
      return OPUS_OK;
   orec_fstart++;  orec_flen = len;  orec_C = C;

   if (dec == NULL)
   {
      ec_dec_init(&st->dec,(unsigned char*)data,len);
      dec = &st->dec;
   }

   total_bits = len*8;
   tell = ec_tell(dec);

   if (tell >= total_bits)
      silence = 1;
   else if (tell==1)
      silence = ec_dec_bit_logp(dec, 15, OREC_S_CELT_SILENCE);
   else
      silence = 0;
   if (silence)
   {
      tell = len*8;
      dec->nbits_total+=tell-ec_tell(dec);
   }

   if (start==0 && tell+16 <= total_bits)
   {
      if(ec_dec_bit_logp(dec, 1, OREC_S_CELT_PF_FLAG))
      {
         int octave;
         octave = ec_dec_uint(dec, 6, OREC_S_CELT_PF_OCTAVE);
         (void) ec_dec_bits(dec, 4+octave, OREC_S_CELT_PF_PITCH);
         (void) ec_dec_bits(dec, 3, OREC_S_CELT_PF_GAIN);
         if (ec_tell(dec)+2<=total_bits)
            (void) ec_dec_icdf(dec, tapset_icdf, 2, OREC_S_CELT_PF_TAPSET);
      }
      tell = ec_tell(dec);
   }

   if (LM > 0 && tell+3 <= total_bits)
   {
      isTransient = ec_dec_bit_logp(dec, 3, OREC_S_CELT_TRANSIENT);
      tell = ec_tell(dec);
   }
   else
      isTransient = 0;

   orec_LM = LM;  orec_cframe++;  orec_trans = isTransient;
   if (isTransient)
      shortBlocks = M;
   else
      shortBlocks = 0;

   intra_ener = tell+3<=total_bits ? ec_dec_bit_logp(dec, 3, OREC_S_CELT_INTRA) : 0;
   orec_bstart = start;  orec_bend = end;
   unquant_coarse_energy(mode, start, end, intra_ener, dec, C, LM);

   tf_decode(start, end, isTransient, tf_res, LM, dec);

   tell = ec_tell(dec);
   spread_decision = SPREAD_NORMAL;
   if (tell+4 <= total_bits)
      spread_decision = ec_dec_icdf(dec, spread_icdf, 5, OREC_S_CELT_SPREAD);
   orec_spread = spread_decision;

   init_caps(mode,cap,LM,C);

   dynalloc_logp = 6;
   total_bits<<=BITRES;
   tell = ec_tell_frac(dec);
   for (i=start;i<end;i++)
   {
      int width, quanta;
      int dynalloc_loop_logp;
      int boost;
      width = C*(eBands[i+1]-eBands[i])<<LM;
      quanta = IMIN(width<<BITRES, IMAX(6<<BITRES, width));
      dynalloc_loop_logp = dynalloc_logp;
      boost = 0;
      orec_band = i;
      while (tell+(dynalloc_loop_logp<<BITRES) < total_bits && boost < cap[i])
      {
         int flag;
         flag = ec_dec_bit_logp(dec, dynalloc_loop_logp, OREC_S_CELT_DYNALLOC);
         tell = ec_tell_frac(dec);
         if (!flag)
            break;
         boost += quanta;
         total_bits -= quanta;
         dynalloc_loop_logp = 1;
      }
      offsets[i] = boost;
      if (boost>0)
         dynalloc_logp = IMAX(2, dynalloc_logp-1);
   }

   alloc_trim = tell+(6<<BITRES) <= total_bits ?
         ec_dec_icdf(dec, trim_icdf, 7, OREC_S_CELT_TRIM) : 5;
   orec_trim = alloc_trim;

   bits = (((opus_int32)len*8)<<BITRES) - (opus_int32)ec_tell_frac(dec) - 1;
   anti_collapse_rsv = isTransient&&LM>=2&&bits>=((LM+2)<<BITRES) ? (1<<BITRES) : 0;
   bits -= anti_collapse_rsv;

   codedBands = clt_compute_allocation(mode, start, end, offsets, cap,
         alloc_trim, &intensity, &dual_stereo, bits, &balance, pulses,
         fine_quant, fine_priority, C, LM, dec);
   for (i=0;i<end;i++) { orec_fq[i] = fine_quant[i];  orec_fpri[i] = fine_priority[i]; }

   unquant_fine_energy(mode, start, end, fine_quant, dec, C);

   quant_all_bands(mode, start, end, C, pulses, shortBlocks, spread_decision,
         dual_stereo, intensity, tf_res, len*(8<<BITRES)-anti_collapse_rsv,
         balance, dec, LM, codedBands);

   if (anti_collapse_rsv > 0)
      (void) ec_dec_bits(dec, 1, OREC_S_CELT_ANTICOLLAPSE);
   unquant_energy_finalise(mode, start, end, fine_quant, fine_priority,
         len*8-ec_tell(dec), dec, C);

   st->rng = dec->rng;

   if (ec_tell(dec) > 8*len)
      return OPUS_INTERNAL_ERROR;
   return OPUS_OK;
}

#ifdef BLR_OPUS_TRANSITION
#include <stdarg.h>
struct OpusCustomMode;
struct OpusCustomMode *opus_custom_mode_create(opus_int32 Fs, int frame_size, int *error);
int celt_decoder_get_size(int channels);
int celt_decoder_get_size(int channels) { (void) channels;  return (int) sizeof(CELTDecoder); }
int celt_decoder_init_upstream(CELTDecoder *st, opus_int32 sampling_rate, int channels);
int celt_decoder_init_upstream(CELTDecoder *st, opus_int32 sampling_rate, int channels) {
   (void) sampling_rate;  celt_decoder_init(st, channels);  return OPUS_OK;
}
int celt_decode_with_ec_dred(CELTDecoder *st, const unsigned char *data, int len, void *pcm, int frame_size, ec_dec *dec, int accum);
int celt_decode_with_ec_dred(CELTDecoder *st, const unsigned char *data, int len, void *pcm, int frame_size, ec_dec *dec, int accum) {
   int r;
   (void) pcm; (void) accum;
   r = celt_decode_with_ec(st, data, len, frame_size, dec);
   return r < 0 ? r : frame_size;
}
int celt_decode_with_ec_upstream(CELTDecoder *st, const unsigned char *data, int len, void *pcm, int frame_size, ec_dec *dec, int accum);
int celt_decode_with_ec_upstream(CELTDecoder *st, const unsigned char *data, int len, void *pcm, int frame_size, ec_dec *dec, int accum) {
   return celt_decode_with_ec_dred(st, data, len, pcm, frame_size, dec, accum);
}
int opus_custom_decoder_ctl(CELTDecoder *st, int request, ...);
int opus_custom_decoder_ctl(CELTDecoder *st, int request, ...) {
   va_list ap;
   va_start(ap, request);
   switch (request) {
   case 10008: st->stream_channels = va_arg(ap, opus_int32);  break;
   case 10010: st->start = va_arg(ap, opus_int32);  break;
   case 10012: st->end = va_arg(ap, opus_int32);  break;
   case 10015: *va_arg(ap, struct OpusCustomMode **) = opus_custom_mode_create(48000, 960, NULL);  break;
   case 4031:  *va_arg(ap, opus_uint32 *) = st->rng;  break;
   case 4028:  celt_decoder_reset(st);  break;
   default: break;
   }
   va_end(ap);
   return OPUS_OK;
}
#endif
