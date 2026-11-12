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

#include "silk.h"

void silk_decode_indices(
    silk_decoder_state          *psDec,
    ec_dec                      *psRangeDec,
    opus_int                    FrameIndex,
    opus_int                    decode_LBRR,
    opus_int                    condCoding
)
{
    opus_int   i, k, Ix;
    opus_int   decode_absolute_lagIndex, delta_lagIndex;
    opus_int16 ec_ix[ MAX_LPC_ORDER ];
    opus_uint8 pred_Q8[ MAX_LPC_ORDER ];

    if( decode_LBRR || psDec->VAD_flags[ FrameIndex ] ) {
        Ix = ec_dec_icdf( psRangeDec, silk_type_offset_VAD_iCDF, 8 , OREC_S_SILK_TYPE_VAD) + 2;
    } else {
        Ix = ec_dec_icdf( psRangeDec, silk_type_offset_no_VAD_iCDF, 8 , OREC_S_SILK_TYPE_NOVAD);
    }
    psDec->indices.signalType      = (opus_int8)silk_RSHIFT( Ix, 1 );
    psDec->indices.quantOffsetType = (opus_int8)( Ix & 1 );

    if( condCoding == CODE_CONDITIONALLY ) {
        psDec->indices.GainsIndices[ 0 ] = (opus_int8)ec_dec_icdf( psRangeDec, silk_delta_gain_iCDF, 8 , OREC_S_SILK_GAIN_DELTA0);
    } else {
        psDec->indices.GainsIndices[ 0 ]  = (opus_int8)silk_LSHIFT( ec_dec_icdf( psRangeDec, silk_gain_iCDF[ psDec->indices.signalType ], 8 , OREC_S_SILK_GAIN_MSB), 3 );
        psDec->indices.GainsIndices[ 0 ] += (opus_int8)ec_dec_icdf( psRangeDec, silk_uniform8_iCDF, 8 , OREC_S_SILK_GAIN_LSB);
    }

    for( i = 1; i < psDec->nb_subfr; i++ ) {
        psDec->indices.GainsIndices[ i ] = (opus_int8)ec_dec_icdf( psRangeDec, silk_delta_gain_iCDF, 8 , OREC_S_SILK_GAIN_DELTA);
    }

    psDec->indices.NLSFIndices[ 0 ] = (opus_int8)ec_dec_icdf( psRangeDec, &psDec->psNLSF_CB->CB1_iCDF[ ( psDec->indices.signalType >> 1 ) * psDec->psNLSF_CB->nVectors ], 8 , OREC_S_SILK_NLSF_CB1);
    silk_NLSF_unpack( ec_ix, pred_Q8, psDec->psNLSF_CB, psDec->indices.NLSFIndices[ 0 ] );
    celt_assert( psDec->psNLSF_CB->order == psDec->LPC_order );
    for( i = 0; i < psDec->psNLSF_CB->order; i++ ) {
        Ix = ec_dec_icdf( psRangeDec, &psDec->psNLSF_CB->ec_iCDF[ ec_ix[ i ] ], 8 , OREC_S_SILK_NLSF_RES);
        if( Ix == 0 ) {
            Ix -= ec_dec_icdf( psRangeDec, silk_NLSF_EXT_iCDF, 8 , OREC_S_SILK_NLSF_EXT_LO);
        } else if( Ix == 2 * NLSF_QUANT_MAX_AMPLITUDE ) {
            Ix += ec_dec_icdf( psRangeDec, silk_NLSF_EXT_iCDF, 8 , OREC_S_SILK_NLSF_EXT_HI);
        }
        psDec->indices.NLSFIndices[ i+1 ] = (opus_int8)( Ix - NLSF_QUANT_MAX_AMPLITUDE );
    }

    if( psDec->nb_subfr == MAX_NB_SUBFR ) {
        psDec->indices.NLSFInterpCoef_Q2 = (opus_int8)ec_dec_icdf( psRangeDec, silk_NLSF_interpolation_factor_iCDF, 8 , OREC_S_SILK_NLSF_INTERP);
    } else {
        psDec->indices.NLSFInterpCoef_Q2 = 4;
    }

    if( psDec->indices.signalType == TYPE_VOICED )
    {
        decode_absolute_lagIndex = 1;
        if( condCoding == CODE_CONDITIONALLY && psDec->ec_prevSignalType == TYPE_VOICED ) {
            delta_lagIndex = (opus_int16)ec_dec_icdf( psRangeDec, silk_pitch_delta_iCDF, 8 , OREC_S_SILK_LAG_DELTA);
            if( delta_lagIndex > 0 ) {
                delta_lagIndex = delta_lagIndex - 9;
                psDec->indices.lagIndex = (opus_int16)( psDec->ec_prevLagIndex + delta_lagIndex );
                decode_absolute_lagIndex = 0;
            }
        }
        if( decode_absolute_lagIndex ) {
            psDec->indices.lagIndex  = (opus_int16)ec_dec_icdf( psRangeDec, silk_pitch_lag_iCDF, 8 , OREC_S_SILK_LAG_ABS) * silk_RSHIFT( psDec->fs_kHz, 1 );
            psDec->indices.lagIndex += (opus_int16)ec_dec_icdf( psRangeDec, psDec->pitch_lag_low_bits_iCDF, 8 , OREC_S_SILK_LAG_LOW);
        }
        psDec->ec_prevLagIndex = psDec->indices.lagIndex;

        psDec->indices.contourIndex = (opus_int8)ec_dec_icdf( psRangeDec, psDec->pitch_contour_iCDF, 8 , OREC_S_SILK_CONTOUR);

        psDec->indices.PERIndex = (opus_int8)ec_dec_icdf( psRangeDec, silk_LTP_per_index_iCDF, 8 , OREC_S_SILK_PER_INDEX);

        for( k = 0; k < psDec->nb_subfr; k++ ) {
            psDec->indices.LTPIndex[ k ] = (opus_int8)ec_dec_icdf( psRangeDec, silk_LTP_gain_iCDF_ptrs[ psDec->indices.PERIndex ], 8 , OREC_S_SILK_LTP_GAIN);
        }

        if( condCoding == CODE_INDEPENDENTLY ) {
            psDec->indices.LTP_scaleIndex = (opus_int8)ec_dec_icdf( psRangeDec, silk_LTPscale_iCDF, 8 , OREC_S_SILK_LTP_SCALE);
        } else {
            psDec->indices.LTP_scaleIndex = 0;
        }
    }
    psDec->ec_prevSignalType = psDec->indices.signalType;

    psDec->indices.Seed = (opus_int8)ec_dec_icdf( psRangeDec, silk_uniform4_iCDF, 8 , OREC_S_SILK_SEED);
}
