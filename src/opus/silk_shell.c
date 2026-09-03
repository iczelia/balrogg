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

static OPUS_INLINE void decode_split(
    opus_int16                  *p_child1,      /* O    pulse amplitude of first child subframe     */
    opus_int16                  *p_child2,      /* O    pulse amplitude of second child subframe    */
    ec_dec                      *psRangeDec,    /* I/O  Compressor data structure                   */
    const opus_int              p,              /* I    pulse amplitude of current subframe         */
    const opus_uint8            *shell_table    /* I    table of shell cdfs                         */
)
{
    if( p > 0 ) {
        p_child1[ 0 ] = ec_dec_icdf( psRangeDec, &shell_table[ silk_shell_code_table_offsets[ p ] ], 8 , OREC_S_SILK_SHELL);
        p_child2[ 0 ] = p - p_child1[ 0 ];
    } else {
        p_child1[ 0 ] = 0;
        p_child2[ 0 ] = 0;
    }
}

/* Shell encoder, operates on one shell code frame of 16 pulses */


/* Shell decoder, operates on one shell code frame of 16 pulses */
void silk_shell_decoder(
    opus_int16                  *pulses0,                       /* O    data: nonnegative pulse amplitudes          */
    ec_dec                      *psRangeDec,                    /* I/O  Compressor data structure                   */
    const opus_int              pulses4                         /* I    number of pulses per pulse-subframe         */
)
{
    opus_int16 pulses3[ 2 ], pulses2[ 4 ], pulses1[ 8 ];

    /* this function operates on one shell code frame of 16 pulses */
    silk_assert( SHELL_CODEC_FRAME_LENGTH == 16 );

    decode_split( &pulses3[  0 ], &pulses3[  1 ], psRangeDec, pulses4,      silk_shell_code_table3 );

    decode_split( &pulses2[  0 ], &pulses2[  1 ], psRangeDec, pulses3[ 0 ], silk_shell_code_table2 );

    decode_split( &pulses1[  0 ], &pulses1[  1 ], psRangeDec, pulses2[ 0 ], silk_shell_code_table1 );
    decode_split( &pulses0[  0 ], &pulses0[  1 ], psRangeDec, pulses1[ 0 ], silk_shell_code_table0 );
    decode_split( &pulses0[  2 ], &pulses0[  3 ], psRangeDec, pulses1[ 1 ], silk_shell_code_table0 );

    decode_split( &pulses1[  2 ], &pulses1[  3 ], psRangeDec, pulses2[ 1 ], silk_shell_code_table1 );
    decode_split( &pulses0[  4 ], &pulses0[  5 ], psRangeDec, pulses1[ 2 ], silk_shell_code_table0 );
    decode_split( &pulses0[  6 ], &pulses0[  7 ], psRangeDec, pulses1[ 3 ], silk_shell_code_table0 );

    decode_split( &pulses2[  2 ], &pulses2[  3 ], psRangeDec, pulses3[ 1 ], silk_shell_code_table2 );

    decode_split( &pulses1[  4 ], &pulses1[  5 ], psRangeDec, pulses2[ 2 ], silk_shell_code_table1 );
    decode_split( &pulses0[  8 ], &pulses0[  9 ], psRangeDec, pulses1[ 4 ], silk_shell_code_table0 );
    decode_split( &pulses0[ 10 ], &pulses0[ 11 ], psRangeDec, pulses1[ 5 ], silk_shell_code_table0 );

    decode_split( &pulses1[  6 ], &pulses1[  7 ], psRangeDec, pulses2[ 3 ], silk_shell_code_table1 );
    decode_split( &pulses0[ 12 ], &pulses0[ 13 ], psRangeDec, pulses1[ 6 ], silk_shell_code_table0 );
    decode_split( &pulses0[ 14 ], &pulses0[ 15 ], psRangeDec, pulses1[ 7 ], silk_shell_code_table0 );
}
