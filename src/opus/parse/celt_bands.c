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

int orec_pvqoff, orec_pvqrole, orec_frame;
int orec_pdep, orec_pB, orec_pb, orec_tfc;

#define FRAC_MUL16(a,b) ((16384+((opus_int32)(opus_int16)(a)*(opus_int16)(b)))>>15)

static opus_int16 bitexact_cos(opus_int16 x)
{
   opus_int32 tmp;
   opus_int16 x2;
   tmp = (4096+((opus_int32)(x)*(x)))>>13;
   x2 = tmp;
   x2 = (32767-x2) + FRAC_MUL16(x2, (-7651 + FRAC_MUL16(x2, (8277 + FRAC_MUL16(-626, x2)))));
   return 1+x2;
}

static int bitexact_log2tan(int isin,int icos)
{
   int lc;
   int ls;
   lc=EC_ILOG(icos);
   ls=EC_ILOG(isin);
   icos<<=15-lc;
   isin<<=15-ls;
   return (ls-lc)*(1<<11)
         +FRAC_MUL16(isin, FRAC_MUL16(isin, -2597) + 7932)
         -FRAC_MUL16(icos, FRAC_MUL16(icos, -2597) + 7932);
}

static int compute_qn(int N, int b, int offset, int pulse_cap, int stereo)
{
   static const opus_int16 exp2_table8[8] =
      {16384, 17866, 19483, 21247, 23170, 25267, 27554, 30048};
   int qn, qb;
   int N2 = 2*N-1;
   if (stereo && N==2)
      N2--;
   qb = celt_sudiv(b+N2*offset, N2);
   qb = IMIN(b-pulse_cap-(4<<BITRES), qb);

   qb = IMIN(8<<BITRES, qb);

   if (qb<(1<<BITRES>>1)) {
      qn = 1;
   } else {
      qn = exp2_table8[qb&0x7]>>(14-(qb>>BITRES));
      qn = (qn+1)>>1<<1;
   }
   celt_assert(qn <= 256);
   return qn;
}

#define THETA_L2T_OFF (1 << 30)
static opus_int32 theta_l2t[16385];

struct band_ctx {
   const CELTMode *m;
   int i;
   int intensity;
   int tf_change;
   ec_dec *ec;
   opus_int32 remaining_bits;
};

struct split_ctx {
   int delta;
   int itheta;
   int qalloc;
};

static void compute_theta(struct band_ctx *ctx, struct split_ctx *sctx,
      int N, int *b, int B, int B0, int LM, int stereo)
{
   int qn;
   int itheta=0;
   int delta;
   int imid, iside;
   int qalloc;
   int pulse_cap;
   int offset;
   opus_int32 tell;
   const CELTMode *m;
   int i;
   int intensity;
   ec_dec *ec;

   m = ctx->m;
   i = ctx->i;
   intensity = ctx->intensity;
   ec = ctx->ec;
   (void) B;

   pulse_cap = m->logN[i]+LM*(1<<BITRES);
   offset = (pulse_cap>>1) - (stereo&&N==2 ? QTHETA_OFFSET_TWOPHASE : QTHETA_OFFSET);
   qn = compute_qn(N, *b, offset, pulse_cap, stereo);
   if (stereo && i>=intensity)
      qn = 1;
   tell = ec_tell_frac(ec);
   if (qn!=1)
   {
      itheta = orec_dec_theta(ec, qn, stereo, N, B0);
      celt_assert(itheta>=0);
      itheta = celt_udiv((opus_int32)itheta*16384, qn);
   } else if (stereo) {
      if (*b>2<<BITRES && ctx->remaining_bits > 2<<BITRES)
         (void) ec_dec_bit_logp(ec, 2, OREC_S_CELT_INV);
      itheta = 0;
   }
   qalloc = ec_tell_frac(ec) - tell;
   *b -= qalloc;

   if (itheta == 0)
   {
      delta = -16384;
   } else if (itheta == 16384)
   {
      delta = 16384;
   } else {
      opus_int32 *l2t = theta_l2t + itheta;
      if (!*l2t)
      {
         imid = bitexact_cos((opus_int16)itheta);
         iside = bitexact_cos((opus_int16)(16384-itheta));
         *l2t = bitexact_log2tan(iside,imid) + THETA_L2T_OFF;
      }
      delta = FRAC_MUL16((N-1)<<7,*l2t - THETA_L2T_OFF);
   }

   sctx->delta = delta;
   sctx->itheta = itheta;
   sctx->qalloc = qalloc;
}

static void quant_band_n1(struct band_ctx *ctx, int stereo)
{
   int c;
   c=0; do {
      if (ctx->remaining_bits>=1<<BITRES)
      {
         (void) ec_dec_bits(ctx->ec, 1, OREC_S_CELT_SIGN1);
         ctx->remaining_bits -= 1<<BITRES;
      }
   } while (++c<1+stereo);
}

static void quant_partition(struct band_ctx *ctx, int N, int b, int B, int LM)
{
   int q;
   int curr_bits;
   int B0=B;
   const CELTMode *m;
   int i;

   m = ctx->m;
   i = ctx->i;

   if (LM != -1 && b > celt_split[(LM+1)*m->nbEBands+i] && N>2)
   {
      int mbits, sbits, delta;
      int itheta;
      int qalloc;
      struct split_ctx sctx;
      opus_int32 rebalance;
      int d0 = orec_pdep;

      N >>= 1;
      LM -= 1;
      B = (B+1)>>1;

      orec_pB = B0;  orec_pb = b;
      compute_theta(ctx, &sctx, N, &b, B, B0, LM, 0);
      orec_pdep = d0 + 1;
      delta = sctx.delta;
      itheta = sctx.itheta;
      qalloc = sctx.qalloc;

      if (B0>1 && (itheta&0x3fff))
      {
         if (itheta > 8192)
            delta -= delta>>(4-LM);
         else
            delta = IMIN(0, delta + (N<<BITRES>>(5-LM)));
      }
      mbits = IMAX(0, IMIN(b, (b-delta)/2));
      sbits = b-mbits;
      ctx->remaining_bits -= qalloc;

      rebalance = ctx->remaining_bits;
      {
      int off0 = orec_pvqoff;
      if (mbits >= sbits)
      {
         orec_pvqoff = off0;
         quant_partition(ctx, N, mbits, B, LM);
         rebalance = mbits - (rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta!=0)
            sbits += rebalance - (3<<BITRES);
         orec_pvqoff = off0 + N;
         quant_partition(ctx, N, sbits, B, LM);
      } else {
         orec_pvqoff = off0 + N;
         quant_partition(ctx, N, sbits, B, LM);
         rebalance = sbits - (rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta!=16384)
            mbits += rebalance - (3<<BITRES);
         orec_pvqoff = off0;
         quant_partition(ctx, N, mbits, B, LM);
      }
      orec_pvqoff = off0;
      }
      orec_pdep = d0;
   } else {
      q = bits2pulses(m, i, LM, b);
      curr_bits = pulses2bits(m, i, LM, q);
      ctx->remaining_bits -= curr_bits;

      while (ctx->remaining_bits < 0 && q > 0)
      {
         ctx->remaining_bits += curr_bits;
         q--;
         curr_bits = pulses2bits(m, i, LM, q);
         ctx->remaining_bits -= curr_bits;
      }

      if (q!=0)
      {
         int K = get_pulses(q);
         orec_pB = B;  orec_pb = b;
         decode_pulses(N, K, ctx->ec);
      }
   }
}

static void quant_band(struct band_ctx *ctx, int N, int b, int B, int LM)
{
   int N_B=N;
   (void) B;
   int B0=B;
   int recombine=0;
   int tf_change;

   tf_change = ctx->tf_change;

   N_B = celt_udiv(N_B, B);

   if (N==1)
   {
      quant_band_n1(ctx, 0);
      return;
   }

   if (tf_change>0)
      recombine = tf_change;
   B>>=recombine;
   N_B<<=recombine;

   while ((N_B&1) == 0 && tf_change<0)
   {
      B <<= 1;
      N_B >>= 1;
      tf_change++;
   }
   B0=B;
   (void) B0;

   orec_pdep = 0;
   quant_partition(ctx, N, b, B, LM);
}

static void quant_band_stereo(struct band_ctx *ctx, int N, int b, int B, int LM)
{
   int mbits, sbits, delta;
   int itheta;
   int qalloc;
   struct split_ctx sctx;
   ec_dec *ec;

   ec = ctx->ec;

   if (N==1)
   {
      quant_band_n1(ctx, 1);
      return;
   }

   orec_pdep = 0;  orec_pB = B;  orec_pb = b;
   compute_theta(ctx, &sctx, N, &b, B, B, LM, 1);
   delta = sctx.delta;
   itheta = sctx.itheta;
   qalloc = sctx.qalloc;

   if (N==2)
   {
      mbits = b;
      sbits = 0;
      if (itheta != 0 && itheta != 16384)
         sbits = 1<<BITRES;
      mbits -= sbits;
      ctx->remaining_bits -= qalloc+sbits;

      if (sbits)
         (void) ec_dec_bits(ec, 1, OREC_S_CELT_SIDE_SIGN);
      orec_pvqrole = 0;
      quant_band(ctx, N, mbits, B, LM);
   } else {
      opus_int32 rebalance;

      mbits = IMAX(0, IMIN(b, (b-delta)/2));
      sbits = b-mbits;
      ctx->remaining_bits -= qalloc;

      rebalance = ctx->remaining_bits;
      if (mbits >= sbits)
      {
         orec_pvqrole = 0;
         quant_band(ctx, N, mbits, B, LM);
         rebalance = mbits - (rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta!=0)
            sbits += rebalance - (3<<BITRES);
         orec_pvqrole = 1;
         quant_band(ctx, N, sbits, B, LM);
      } else {
         orec_pvqrole = 1;
         quant_band(ctx, N, sbits, B, LM);
         rebalance = sbits - (rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta!=16384)
            mbits += rebalance - (3<<BITRES);
         orec_pvqrole = 0;
         quant_band(ctx, N, mbits, B, LM);
      }
   }
}

void quant_all_bands(const CELTMode *m, int start, int end, int C,
      const int *pulses, int shortBlocks, int spread, int dual_stereo, int intensity,
      const int *tf_res, opus_int32 total_bits, opus_int32 balance, ec_dec *ec, int LM,
      int codedBands)
{
   int i;
   opus_int32 remaining_bits;
   const opus_int16 * OPUS_RESTRICT eBands = m->eBands;
   int B;
   int M;
   struct band_ctx ctx;
   (void) spread;

   M = 1<<LM;
   B = shortBlocks ? M : 1;
   orec_frame++;

   ctx.ec = ec;
   ctx.intensity = intensity;
   ctx.m = m;

   for (i=start;i<end;i++)
   {
      opus_int32 tell;
      int b;
      int N;
      opus_int32 curr_balance;

      ctx.i = i;
      orec_band = orec_band1 = i; orec_LM = orec_bLM = LM;

      N = M*eBands[i+1]-M*eBands[i];
      celt_assert(N > 0);
      tell = ec_tell_frac(ec);

      if (i != start)
         balance -= tell;
      remaining_bits = total_bits-tell-1;
      ctx.remaining_bits = remaining_bits;
      if (i <= codedBands-1)
      {
         curr_balance = celt_sudiv(balance, IMIN(3, codedBands-i));
         b = IMAX(0, IMIN(16383, IMIN(remaining_bits+1,pulses[i]+curr_balance)));
      } else {
         b = 0;
      }

      ctx.tf_change = tf_res[i];
      orec_tfc = tf_res[i];

      if (dual_stereo && i==intensity)
      {
         dual_stereo = 0;
      }
      orec_pvqoff = 0;
      if (dual_stereo)
      {
         orec_pvqrole = 2;
         quant_band(&ctx, N, b/2, B, LM);
         orec_pvqrole = 3;
         quant_band(&ctx, N, b/2, B, LM);
      } else {
         orec_pvqrole = 0;
         if (C==2)
            quant_band_stereo(&ctx, N, b, B, LM);
         else
            quant_band(&ctx, N, b, B, LM);
      }
      balance += pulses[i] + tell;
   }
}
