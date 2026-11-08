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

typedef opus_uint32           ec_window;
typedef struct ec_ctx         ec_ctx;
typedef struct ec_ctx         ec_enc;
typedef struct ec_ctx         ec_dec;

# define EC_WINDOW_SIZE ((int)sizeof(ec_window)*CHAR_BIT)

# define EC_UINT_BITS   (8)

# define BITRES 3

struct ec_ctx{
   unsigned char *buf;
   opus_uint32    storage;
   opus_uint32    end_offs;
   ec_window      end_window;
   int            nend_bits;
   int            nbits_total;
   opus_uint32    offs;
   opus_uint32    rng;
   opus_uint32    val;
   opus_uint32    ext;
   int            rem;
   int            error;
   opus_uint32    nnorm;
   opus_uint32    nraw;
   opus_uint32    ostorage;
   int            oslot;
};

static OPUS_INLINE int ec_tell(ec_ctx *_this){
  return _this->nbits_total-EC_ILOG(_this->rng);
}

static OPUS_INLINE opus_uint32 ec_tell_frac(ec_ctx *_this){
  static const unsigned correction[8] =
    {35733, 38967, 42495, 46340,
     50535, 55109, 60097, 65535};
  opus_uint32 nbits;
  opus_uint32 r;
  int         l;
  unsigned    b;
  nbits=_this->nbits_total<<BITRES;
  l=EC_ILOG(_this->rng);
  r=_this->rng>>(l-16);
  b = (r>>12)-8;
  b += r>correction[b];
  l = (l<<3)+b;
  return nbits-l;
}

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
