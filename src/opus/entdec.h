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

#if !defined(_entdec_H)
# define _entdec_H (1)
# include "entcode.h"

/*  Initialize a decoder over _buf.  */
void ec_dec_init(ec_dec *_this,unsigned char *_buf,opus_uint32 _storage);

/*  Return a cumulative frequency with total 1<<_bits.  */
unsigned ec_decode_bin(ec_dec *_this,unsigned _bits);

/*  Advance through [_fl, _fh) of total frequency _ft.  */
void ec_dec_update(ec_dec *_this,unsigned _fl,unsigned _fh,unsigned _ft);

/* Decode a bit that has a 1/(1<<_logp) probability of being a one */
int ec_dec_bit_logp(ec_dec *_this,unsigned _logp,int _site);

/*  Decode a symbol from a monotone inverse CDF ending in zero.  */
int ec_dec_icdf(ec_dec *_this,const unsigned char *_icdf,unsigned _ftb,int _site);

/*  Decode a symbol from a 16-bit inverse CDF.  */
int ec_dec_icdf16(ec_dec *_this,const opus_uint16 *_icdf,unsigned _ftb,int _site);

/*  Decode an unsigned integer in [0, _ft), where _ft is at least 2.  */
opus_uint32 ec_dec_uint(ec_dec *_this,opus_uint32 _ft,int _site);

/*  Decode 0 to 25 raw bits.  */
opus_uint32 ec_dec_bits(ec_dec *_this,unsigned _ftb,int _site);

#endif
