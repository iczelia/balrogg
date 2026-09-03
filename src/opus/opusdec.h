/* Copyright (c) 2010 Xiph.Org Foundation, Skype Limited
   Written by Jean-Marc Valin and Koen Vos */
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

/*  Opus packet parser used by opusmode.c. See README for its origin.  */

#ifndef BLR_OPUSDEC_H
#define BLR_OPUSDEC_H

#include "opus_types.h"
#include "opus_defines.h"

/*  Which libopus this parser was cut from; balrogg -v prints it.  */
#define OPUS_PARSER_VERSION "libopus 3da9f7a"

typedef struct OpusDecoder OpusDecoder;

/*  A decoder for a 1- or 2-channel stream, or NULL when out of memory.  */
OpusDecoder *opus_decoder_create(int channels);
void opus_decoder_destroy(OpusDecoder *st);

/*  Parse one packet: every frame in it, in order, through the driven
    range decoder.  OPUS_OK, or a negative error for a packet no decoder
    would accept.  */
int opus_decode(OpusDecoder *st, const unsigned char *data, opus_int32 len);

/*  The packet layout: the TOC, each frame's start and size, and the
    offset of the first frame.  Returns the frame count or an error.  */
int opus_packet_parse(const unsigned char *data, opus_int32 len,
      unsigned char *out_toc, const unsigned char *frames[48],
      opus_int16 size[48], int *payload_offset);

#endif
