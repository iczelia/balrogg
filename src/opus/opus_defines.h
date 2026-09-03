/* Copyright (c) 2010-2011 Xiph.Org Foundation, Skype Limited
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

/*  The handful of libopus definitions the parser keeps.  */

#ifndef OPUS_DEFINES_H
#define OPUS_DEFINES_H

#include "opus_types.h"

#define OPUS_OK                0
#define OPUS_BAD_ARG          -1
#define OPUS_BUFFER_TOO_SMALL -2
#define OPUS_INTERNAL_ERROR   -3
#define OPUS_INVALID_PACKET   -4
#define OPUS_UNIMPLEMENTED    -5
#define OPUS_INVALID_STATE    -6
#define OPUS_ALLOC_FAIL       -7

#define OPUS_BANDWIDTH_NARROWBAND    1101
#define OPUS_BANDWIDTH_MEDIUMBAND    1102
#define OPUS_BANDWIDTH_WIDEBAND      1103
#define OPUS_BANDWIDTH_SUPERWIDEBAND 1104
#define OPUS_BANDWIDTH_FULLBAND      1105

#if defined(__GNUC__) || defined(__clang__)
# define OPUS_INLINE __inline__
# define OPUS_RESTRICT __restrict__
#elif defined(_MSC_VER)
# define OPUS_INLINE __inline
# define OPUS_RESTRICT __restrict
#else
# define OPUS_INLINE inline
# define OPUS_RESTRICT restrict
#endif

#endif
