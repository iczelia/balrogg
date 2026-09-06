/*
Copyright (c) 2006-2011, Skype Limited. All rights reserved.
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
- Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
- Neither the name of Internet Society, IETF or IETF Trust, nor the
names of specific contributors, may be used to endorse or promote
products derived from this software without specific prior written
permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/
/*  Trimmed for balrogg: the decoder-side half only; see silk.h.  */


#include "silk.h"

const opus_uint8 silk_LTP_per_index_iCDF[3] = {
       179,     99,      0
};

static const opus_uint8 silk_LTP_gain_iCDF_0[8] = {
        71,     56,     43,     30,     21,     12,      6,      0
};

static const opus_uint8 silk_LTP_gain_iCDF_1[16] = {
       199,    165,    144,    124,    109,     96,     84,     71,
        61,     51,     42,     32,     23,     15,      8,      0
};

static const opus_uint8 silk_LTP_gain_iCDF_2[32] = {
       241,    225,    211,    199,    187,    175,    164,    153,
       142,    132,    123,    114,    105,     96,     88,     80,
        72,     64,     57,     50,     44,     38,     33,     29,
        24,     20,     16,     12,      9,      5,      2,      0
};

const opus_uint8 * const silk_LTP_gain_iCDF_ptrs[NB_LTP_CBKS] = {
    silk_LTP_gain_iCDF_0,
    silk_LTP_gain_iCDF_1,
    silk_LTP_gain_iCDF_2
};

