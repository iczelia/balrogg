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

#if !defined(_entenc_H)
# define _entenc_H (1)
# include "entcode.h"

void ec_enc_init(ec_enc *_this,unsigned char *_buf,opus_uint32 _size);
void ec_encode(ec_enc *_this,unsigned _fl,unsigned _fh,unsigned _ft);

void ec_encode_bin(ec_enc *_this,unsigned _fl,unsigned _fh,unsigned _bits);

void ec_enc_bit_logp(ec_enc *_this,int _val,unsigned _logp);

void ec_enc_icdf(ec_enc *_this,int _s,const unsigned char *_icdf,unsigned _ftb);

void ec_enc_icdf16(ec_enc *_this,int _s,const opus_uint16 *_icdf,unsigned _ftb);

void ec_enc_uint(ec_enc *_this,opus_uint32 _fl,opus_uint32 _ft);

void ec_enc_bits(ec_enc *_this,opus_uint32 _fl,unsigned _ftb);

void ec_enc_done(ec_enc *_this);

#endif
