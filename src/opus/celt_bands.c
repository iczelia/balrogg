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

/*  CELT band parser with sample processing removed.  */

#include "celt.h"

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
   /*  Reserve enough bits for one side pulse.  */
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
   (void) B;   /* it chose the encoder's split noise; the decode reads B0 */

   /* Decide on the resolution to give to the split parameter theta */
   pulse_cap = m->logN[i]+LM*(1<<BITRES);
   offset = (pulse_cap>>1) - (stereo&&N==2 ? QTHETA_OFFSET_TWOPHASE : QTHETA_OFFSET);
   qn = compute_qn(N, *b, offset, pulse_cap, stereo);
   if (stereo && i>=intensity)
      qn = 1;
   tell = ec_tell_frac(ec);
   if (qn!=1)
   {
      /* Entropy coding of the angle: one call into the driven decoder, which
         either reads it or takes it from the model.  */
      itheta = orec_dec_theta(ec, qn, stereo, N, B0);
      celt_assert(itheta>=0);
      itheta = celt_udiv((opus_int32)itheta*16384, qn);
   } else if (stereo) {
      if (*b>2<<BITRES && ctx->remaining_bits > 2<<BITRES)
         (void) ec_dec_bit_logp(ec, 2, OREC_S_CELT_INV);   /* the inversion flag */
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
      imid = bitexact_cos((opus_int16)itheta);
      iside = bitexact_cos((opus_int16)(16384-itheta));
      /* This is the mid vs side allocation that minimizes squared error
         in that band. */
      delta = FRAC_MUL16((N-1)<<7,bitexact_log2tan(iside,imid));
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
         (void) ec_dec_bits(ctx->ec, 1, OREC_S_CELT_SIGN1);   /* the sign */
         ctx->remaining_bits -= 1<<BITRES;
      }
   } while (++c<1+stereo);
}

/* A mono partition: split in two when there are 1.5 more bits than a
   single PVQ can take, recursively, else read the PVQ index. */
static void quant_partition(struct band_ctx *ctx, int N, int b, int B, int LM)
{
   const unsigned char *cache;
   int q;
   int curr_bits;
   int B0=B;
   const CELTMode *m;
   int i;

   m = ctx->m;
   i = ctx->i;

   /* If we need 1.5 more bit than we can produce, split the band in two. */
   cache = m->cache.bits + m->cache.index[(LM+1)*m->nbEBands+i];
   if (LM != -1 && b > cache[cache[0]]+12 && N>2)
   {
      int mbits, sbits, delta;
      int itheta;
      int qalloc;
      struct split_ctx sctx;
      opus_int32 rebalance;

      N >>= 1;
      LM -= 1;
      B = (B+1)>>1;

      compute_theta(ctx, &sctx, N, &b, B, B0, LM, 0);
      delta = sctx.delta;
      itheta = sctx.itheta;
      qalloc = sctx.qalloc;

      /* Give more bits to low-energy MDCTs than they would otherwise deserve */
      if (B0>1 && (itheta&0x3fff))
      {
         if (itheta > 8192)
            /* Rough approximation for pre-echo masking */
            delta -= delta>>(4-LM);
         else
            /* Corresponds to a forward-masking slope of 1.5 dB per 10 ms */
            delta = IMIN(0, delta + (N<<BITRES>>(5-LM)));
      }
      mbits = IMAX(0, IMIN(b, (b-delta)/2));
      sbits = b-mbits;
      ctx->remaining_bits -= qalloc;

      rebalance = ctx->remaining_bits;
      if (mbits >= sbits)
      {
         quant_partition(ctx, N, mbits, B, LM);
         rebalance = mbits - (rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta!=0)
            sbits += rebalance - (3<<BITRES);
         quant_partition(ctx, N, sbits, B, LM);
      } else {
         quant_partition(ctx, N, sbits, B, LM);
         rebalance = sbits - (rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta!=16384)
            mbits += rebalance - (3<<BITRES);
         quant_partition(ctx, N, mbits, B, LM);
      }
   } else {
      /* This is the basic no-split case */
      q = bits2pulses(m, i, LM, b);
      curr_bits = pulses2bits(m, i, LM, q);
      ctx->remaining_bits -= curr_bits;

      /* Ensures we can never bust the budget */
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
         decode_pulses(N, K, ctx->ec);
      }
   }
}

/* A band of one channel: the time-frequency changes that reshape the
   block count, then the partition. */
static void quant_band(struct band_ctx *ctx, int N, int b, int B, int LM)
{
   int N_B=N;
   (void) B;
   int B0=B;
   int recombine=0;
   int tf_change;

   tf_change = ctx->tf_change;

   N_B = celt_udiv(N_B, B);

   /* Special case for one sample */
   if (N==1)
   {
      quant_band_n1(ctx, 0);
      return;
   }

   if (tf_change>0)
      recombine = tf_change;
   /* Band recombining to increase frequency resolution */
   B>>=recombine;
   N_B<<=recombine;

   /* Increasing the time resolution */
   while ((N_B&1) == 0 && tf_change<0)
   {
      B <<= 1;
      N_B >>= 1;
      tf_change++;
   }
   B0=B;
   (void) B0;

   quant_partition(ctx, N, b, B, LM);
}

/* A band of a coupled stereo pair. */
static void quant_band_stereo(struct band_ctx *ctx, int N, int b, int B, int LM)
{
   int mbits, sbits, delta;
   int itheta;
   int qalloc;
   struct split_ctx sctx;
   ec_dec *ec;

   ec = ctx->ec;

   /* Special case for one sample */
   if (N==1)
   {
      quant_band_n1(ctx, 1);
      return;
   }

   compute_theta(ctx, &sctx, N, &b, B, B, LM, 1);
   delta = sctx.delta;
   itheta = sctx.itheta;
   qalloc = sctx.qalloc;

   /* This is a special case for N=2 that only works for stereo and takes
      advantage of the fact that mid and side are orthogonal to encode
      the side with just one bit. */
   if (N==2)
   {
      mbits = b;
      sbits = 0;
      /* Only need one bit for the side. */
      if (itheta != 0 && itheta != 16384)
         sbits = 1<<BITRES;
      mbits -= sbits;
      ctx->remaining_bits -= qalloc+sbits;

      if (sbits)
         (void) ec_dec_bits(ec, 1, OREC_S_CELT_SIDE_SIGN);   /* the sign of the side */
      quant_band(ctx, N, mbits, B, LM);
   } else {
      /* "Normal" split code */
      opus_int32 rebalance;

      mbits = IMAX(0, IMIN(b, (b-delta)/2));
      sbits = b-mbits;
      ctx->remaining_bits -= qalloc;

      rebalance = ctx->remaining_bits;
      if (mbits >= sbits)
      {
         quant_band(ctx, N, mbits, B, LM);
         rebalance = mbits - (rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta!=0)
            sbits += rebalance - (3<<BITRES);
         quant_band(ctx, N, sbits, B, LM);
      } else {
         quant_band(ctx, N, sbits, B, LM);
         rebalance = sbits - (rebalance-ctx->remaining_bits);
         if (rebalance > 3<<BITRES && itheta!=16384)
            mbits += rebalance - (3<<BITRES);
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
      orec_band = i; orec_LM = LM; orec_C = C;

      N = M*eBands[i+1]-M*eBands[i];
      celt_assert(N > 0);
      tell = ec_tell_frac(ec);

      /* Compute how many bits we want to allocate to this band */
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

      if (dual_stereo && i==intensity)
      {
         /* Switch off dual stereo to do intensity. */
         dual_stereo = 0;
      }
      if (dual_stereo)
      {
         quant_band(&ctx, N, b/2, B, LM);
         quant_band(&ctx, N, b/2, B, LM);
      } else {
         if (C==2)
            quant_band_stereo(&ctx, N, b, B, LM);
         else
            quant_band(&ctx, N, b, B, LM);
      }
      balance += pulses[i] + tell;
   }
}
