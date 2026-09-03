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

/* Decode mid/side predictors */
void silk_stereo_decode_pred(
    ec_dec                      *psRangeDec                     /* I/O  Compressor data structure                   */
)
{
    opus_int   n, ix[ 2 ][ 3 ];

    /* Entropy decoding */
    n = ec_dec_icdf( psRangeDec, silk_stereo_pred_joint_iCDF, 8 , OREC_S_SILK_STEREO_PRED);
    ix[ 0 ][ 2 ] = silk_DIV32_16( n, 5 );
    ix[ 1 ][ 2 ] = n - 5 * ix[ 0 ][ 2 ];
    for( n = 0; n < 2; n++ ) {
        ix[ n ][ 0 ] = ec_dec_icdf( psRangeDec, silk_uniform3_iCDF, 8 , OREC_S_SILK_STEREO_IX0);
        ix[ n ][ 1 ] = ec_dec_icdf( psRangeDec, silk_uniform5_iCDF, 8 , OREC_S_SILK_STEREO_IX1);
    }

    /* The dequantised predictors only shaped audio.  */
    (void) ix;
}

/* Decode mid-only flag */
void silk_stereo_decode_mid_only(
    ec_dec                      *psRangeDec,                    /* I/O  Compressor data structure                   */
    opus_int                    *decode_only_mid                /* O    Flag that only mid channel has been coded   */
)
{
    /* Decode flag that only mid channel is coded */
    *decode_only_mid = ec_dec_icdf( psRangeDec, silk_stereo_only_code_mid_iCDF, 8 , OREC_S_SILK_MID_ONLY);
}
