/* Copyright (c) 2001-2011 Timothy B. Terriberry
   Copyright (c) 2008-2009 Xiph.Org Foundation */
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

#include "opus_types.h"
#include "opus_defines.h"

#if !defined(_entcode_H)
# define _entcode_H (1)
# include <limits.h>
# include <stddef.h>
# include "ecintrin.h"

extern const opus_uint32 SMALL_DIV_TABLE[129];

#ifdef OPUS_ARM_ASM
#define USE_SMALL_DIV_TABLE
#endif

/*  ec_window must be at least 32 bits.  */
typedef opus_uint32           ec_window;
typedef struct ec_ctx         ec_ctx;
typedef struct ec_ctx         ec_enc;
typedef struct ec_ctx         ec_dec;

# define EC_WINDOW_SIZE ((int)sizeof(ec_window)*CHAR_BIT)

/*The number of bits to use for the range-coded part of unsigned integers.*/
# define EC_UINT_BITS   (8)

/*The resolution of fractional-precision bit usage measurements, i.e.,
   3 => 1/8th bits.*/
# define BITRES 3

/*  Shared entropy encoder and decoder context.  */
struct ec_ctx{
   /*Buffered input/output.*/
   unsigned char *buf;
   /*The size of the buffer.*/
   opus_uint32    storage;
   /*The offset at which the last byte containing raw bits was read/written.*/
   opus_uint32    end_offs;
   /*Bits that will be read from/written at the end.*/
   ec_window      end_window;
   /*Number of valid bits in end_window.*/
   int            nend_bits;
   /*The total number of whole bits read/written.
     This does not include partial bits currently in the range coder.*/
   int            nbits_total;
   /*The offset at which the next range coder byte will be read/written.*/
   opus_uint32    offs;
   /*The number of values in the current range.*/
   opus_uint32    rng;
   /*In the decoder: the difference between the top of the current range and
      the input value, minus one.
     In the encoder: the low end of the current range.*/
   opus_uint32    val;
   /*In the decoder: the saved normalization factor from ec_decode().
     In the encoder: the number of outstanding carry propagating symbols.*/
   opus_uint32    ext;
   /*A buffered input/output symbol, awaiting carry propagation.*/
   int            rem;
   /*Nonzero if an error occurred.*/
   int            error;
};

/*  Return a conservative whole-bit usage count.  */
static OPUS_INLINE int ec_tell(ec_ctx *_this){
  return _this->nbits_total-EC_ILOG(_this->rng);
}

/*  Return conservative bit usage scaled by 2**BITRES.  */
opus_uint32 ec_tell_frac(ec_ctx *_this);

/* Tested exhaustively for all n and for 1<=d<=256 */
static OPUS_INLINE opus_uint32 celt_udiv(opus_uint32 n, opus_uint32 d) {
   celt_sig_assert(d>0);
#ifdef USE_SMALL_DIV_TABLE
   if (d>256)
      return n/d;
   else {
      opus_uint32 t, q;
      t = EC_ILOG(d&-d);
      q = (opus_uint64)SMALL_DIV_TABLE[d>>t]*(n>>(t-1))>>32;
      return q+(n-q*d >= d);
   }
#else
   return n/d;
#endif
}

static OPUS_INLINE opus_int32 celt_sudiv(opus_int32 n, opus_int32 d) {
   celt_sig_assert(d>0);
#ifdef USE_SMALL_DIV_TABLE
   if (n<0)
      return -(opus_int32)celt_udiv(-n, d);
   else
      return celt_udiv(n, d);
#else
   return n/d;
#endif
}

#endif
