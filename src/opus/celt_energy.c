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

/*  CELT energy-symbol parser with synthesis removed.  */

#include "celt.h"

/*  Coarse-energy zero probabilities and decay rates in Q8.  */
static const unsigned char e_prob_model[4][2][42] = {
   /*120 sample frames.*/
   {
      /*Inter*/
      {
          72, 127,  65, 129,  66, 128,  65, 128,  64, 128,  62, 128,  64, 128,
          64, 128,  92,  78,  92,  79,  92,  78,  90,  79, 116,  41, 115,  40,
         114,  40, 132,  26, 132,  26, 145,  17, 161,  12, 176,  10, 177,  11
      },
      /*Intra*/
      {
          24, 179,  48, 138,  54, 135,  54, 132,  53, 134,  56, 133,  55, 132,
          55, 132,  61, 114,  70,  96,  74,  88,  75,  88,  87,  74,  89,  66,
          91,  67, 100,  59, 108,  50, 120,  40, 122,  37,  97,  43,  78,  50
      }
   },
   /*240 sample frames.*/
   {
      /*Inter*/
      {
          83,  78,  84,  81,  88,  75,  86,  74,  87,  71,  90,  73,  93,  74,
          93,  74, 109,  40, 114,  36, 117,  34, 117,  34, 143,  17, 145,  18,
         146,  19, 162,  12, 165,  10, 178,   7, 189,   6, 190,   8, 177,   9
      },
      /*Intra*/
      {
          23, 178,  54, 115,  63, 102,  66,  98,  69,  99,  74,  89,  71,  91,
          73,  91,  78,  89,  86,  80,  92,  66,  93,  64, 102,  59, 103,  60,
         104,  60, 117,  52, 123,  44, 138,  35, 133,  31,  97,  38,  77,  45
      }
   },
   /*480 sample frames.*/
   {
      /*Inter*/
      {
          61,  90,  93,  60, 105,  42, 107,  41, 110,  45, 116,  38, 113,  38,
         112,  38, 124,  26, 132,  27, 136,  19, 140,  20, 155,  14, 159,  16,
         158,  18, 170,  13, 177,  10, 187,   8, 192,   6, 175,   9, 159,  10
      },
      /*Intra*/
      {
          21, 178,  59, 110,  71,  86,  75,  85,  84,  83,  91,  66,  88,  73,
          87,  72,  92,  75,  98,  72, 105,  58, 107,  54, 115,  52, 114,  55,
         112,  56, 129,  51, 132,  40, 150,  33, 140,  29,  98,  35,  77,  42
      }
   },
   /*960 sample frames.*/
   {
      /*Inter*/
      {
          42, 121,  96,  66, 108,  43, 111,  40, 117,  44, 123,  32, 120,  36,
         119,  33, 127,  33, 134,  34, 139,  21, 147,  23, 152,  20, 158,  25,
         154,  26, 166,  21, 173,  16, 184,  13, 184,  10, 150,  13, 139,  15
      },
      /*Intra*/
      {
          22, 178,  63, 114,  74,  82,  84,  83,  92,  82, 103,  62,  96,  72,
          96,  67, 101,  73, 107,  72, 113,  55, 118,  52, 125,  52, 118,  52,
         117,  55, 135,  49, 137,  39, 157,  32, 145,  29,  97,  33,  77,  40
      }
   }
};

static const unsigned char small_energy_icdf[3]={2,1,0};

void unquant_coarse_energy(const CELTMode *m, int start, int end, int intra, ec_dec *dec, int C, int LM)
{
   const unsigned char *prob_model = e_prob_model[LM][intra];
   int i, c;
   opus_int32 budget;
   opus_int32 tell;
   (void) m;

   budget = dec->storage*8;

   /* Decode at a fixed coarse resolution */
   for (i=start;i<end;i++)
   {
      c=0;
      do {
         int qi;
         tell = ec_tell(dec);
         orec_band = i; orec_ch = c; orec_intra = intra;
         if(budget-tell>=15)
         {
            int pi;
            pi = 2*IMIN(i,20);
            qi = ec_laplace_decode(dec,
                  prob_model[pi]<<7, prob_model[pi+1]<<6);
         }
         else if(budget-tell>=2)
         {
            qi = ec_dec_icdf(dec, small_energy_icdf, 2, OREC_S_CELT_COARSE_SMALL);
            qi = (qi>>1)^-(qi&1);
         }
         else if(budget-tell>=1)
         {
            qi = -ec_dec_bit_logp(dec, 1, OREC_S_CELT_COARSE_BIT);
         }
         else
            qi = -1;
         (void) qi;
      } while (++c < C);
   }
}

void unquant_fine_energy(const CELTMode *m, int start, int end, const int *fine_quant, ec_dec *dec, int C)
{
   int i, c;
   (void) m;
   for (i=start;i<end;i++)
   {
      opus_int16 extra;
      extra = fine_quant[i];
      if (fine_quant[i] <= 0)
         continue;
      if (ec_tell(dec)+C*fine_quant[i] > (opus_int32)dec->storage*8) continue;
      c=0;
      do {
         orec_band = i; orec_ch = c; orec_ftb = extra;
         (void) ec_dec_bits(dec, extra, OREC_S_CELT_FINE);
      } while (++c < C);
   }
}

void unquant_energy_finalise(const CELTMode *m, int start, int end, const int *fine_quant, const int *fine_priority, int bits_left, ec_dec *dec, int C)
{
   int i, prio, c;
   (void) m;

   /* Use up the remaining bits */
   for (prio=0;prio<2;prio++)
   {
      for (i=start;i<end && bits_left>=C ;i++)
      {
         if (fine_quant[i] >= MAX_FINE_BITS || fine_priority[i]!=prio)
            continue;
         c=0;
         do {
            orec_band = i; orec_ch = c; orec_ftb = 1;
            (void) ec_dec_bits(dec, 1, OREC_S_CELT_FINALISE);
            bits_left--;
         } while (++c < C);
      }
   }
}
