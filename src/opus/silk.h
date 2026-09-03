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

/*  SILK parser from libopus with signal processing removed. Symbol-reading
    control flow and its tables retain the upstream structure.  */

#ifndef BLR_SILK_H
#define BLR_SILK_H

#include <string.h>
#include "arch.h"
#include "entdec.h"
#include "opusent.h"

#define silk_assert(c)
#define SILK_FIX_CONST( C, Q ) ((opus_int32)((C) * ((opus_int64)1 << (Q)) + 0.5))


#define DECODER_NUM_CHANNELS                    2
#define MAX_FRAMES_PER_PACKET                   3
#define MAX_FS_KHZ                              16
#define MAX_API_FS_KHZ                          48
#define TYPE_NO_VOICE_ACTIVITY                  0
#define TYPE_UNVOICED                           1
#define TYPE_VOICED                             2
#define CODE_INDEPENDENTLY                      0
#define CODE_INDEPENDENTLY_NO_LTP_SCALING       1
#define CODE_CONDITIONALLY                      2
#define MAX_NB_SUBFR                            4
#define SUB_FRAME_LENGTH_MS                     5
#define MAX_SUB_FRAME_LENGTH                    ( SUB_FRAME_LENGTH_MS * MAX_FS_KHZ )
#define MAX_FRAME_LENGTH_MS                     ( SUB_FRAME_LENGTH_MS * MAX_NB_SUBFR )
#define MAX_FRAME_LENGTH                        ( MAX_FRAME_LENGTH_MS * MAX_FS_KHZ )
#define N_LEVELS_QGAIN                          64
#define MAX_DELTA_GAIN_QUANT                    36
#define MIN_DELTA_GAIN_QUANT                    -4
#define MAX_LPC_ORDER                           16
#define MIN_LPC_ORDER                           10
#define LTP_ORDER                               5
#define NB_LTP_CBKS                             3
#define SHELL_CODEC_FRAME_LENGTH                16
#define LOG2_SHELL_CODEC_FRAME_LENGTH           4
#define MAX_NB_SHELL_BLOCKS                     ( MAX_FRAME_LENGTH / SHELL_CODEC_FRAME_LENGTH )
#define N_RATE_LEVELS                           10
#define SILK_MAX_PULSES                         16
#define NLSF_QUANT_MAX_AMPLITUDE                4
#define NLSF_QUANT_MAX_AMPLITUDE_EXT            10
#define STEREO_QUANT_TAB_SIZE                   16
#define STEREO_QUANT_SUB_STEPS                  5
#define PITCH_EST_MIN_LAG_MS                    2
#define PITCH_EST_MAX_LAG_MS                    18
#define PE_NB_CBKS_STAGE2_EXT                   11
#define PE_NB_CBKS_STAGE3_MAX                   34
#define PE_NB_CBKS_STAGE3_10MS                  12
#define PE_NB_CBKS_STAGE2_10MS                  3

#define FLAG_DECODE_NORMAL                      0
#define FLAG_PACKET_LOST                        1
#define FLAG_DECODE_LBRR                        2

#define SILK_NO_ERROR                           0
#define SILK_DEC_INVALID_SAMPLING_FREQUENCY     -200
#define SILK_DEC_INVALID_FRAME_SIZE             -203


#define silk_memset(dest, src, size)        memset((dest), (src), (size))
#define silk_LSHIFT32(a, shift)             ((opus_int32)((opus_uint32)(a)<<(shift)))
#define silk_LSHIFT(a, shift)               silk_LSHIFT32(a, shift)
#define silk_RSHIFT32(a, shift)             ((a)>>(shift))
#define silk_RSHIFT(a, shift)               silk_RSHIFT32(a, shift)
#define silk_ADD_LSHIFT(a, b, shift)        ((a) + silk_LSHIFT((b), (shift)))
#define silk_min(a, b)                      (((a) < (b)) ? (a) : (b))
#define silk_max(a, b)                      (((a) > (b)) ? (a) : (b))
#define silk_SMULBB(a32, b32)               ((opus_int32)((opus_int16)(a32)) * (opus_int32)((opus_int16)(b32)))
#define silk_SMLABB(a32, b32, c32)          ((a32) + ((opus_int32)((opus_int16)(b32))) * (opus_int32)((opus_int16)(c32)))
#define silk_SMULWB(a32, b32)               ((opus_int32)(((a32) * (opus_int64)((opus_int16)(b32))) >> 16))
#define silk_DIV32_16(a32, b16)             ((opus_int32)((a32) / (b16)))


typedef struct {
    const opus_int16             nVectors;
    const opus_int16             order;
    const opus_int16             quantStepSize_Q16;
    const opus_int16             invQuantStepSize_Q6;
    const opus_uint8             *CB1_NLSF_Q8;
    const opus_int16             *CB1_Wght_Q9;
    const opus_uint8             *CB1_iCDF;
    const opus_uint8             *pred_Q8;
    const opus_uint8             *ec_sel;
    const opus_uint8             *ec_iCDF;
    const opus_uint8             *ec_Rates_Q5;
    const opus_int16             *deltaMin_Q15;
} silk_NLSF_CB_struct;

typedef struct {
    opus_int8                    GainsIndices[ MAX_NB_SUBFR ];
    opus_int8                    LTPIndex[ MAX_NB_SUBFR ];
    opus_int8                    NLSFIndices[ MAX_LPC_ORDER + 1 ];
    opus_int16                   lagIndex;
    opus_int8                    contourIndex;
    opus_int8                    signalType;
    opus_int8                    quantOffsetType;
    opus_int8                    NLSFInterpCoef_Q2;
    opus_int8                    PERIndex;
    opus_int8                    LTP_scaleIndex;
    opus_int8                    Seed;
} SideInfoIndices;


extern const opus_uint8  silk_gain_iCDF[ 3 ][ N_LEVELS_QGAIN / 8 ];
extern const opus_uint8  silk_delta_gain_iCDF[ MAX_DELTA_GAIN_QUANT - MIN_DELTA_GAIN_QUANT + 1 ];
extern const opus_uint8  silk_pitch_lag_iCDF[ 2 * ( PITCH_EST_MAX_LAG_MS - PITCH_EST_MIN_LAG_MS ) ];
extern const opus_uint8  silk_pitch_delta_iCDF[ 21 ];
extern const opus_uint8  silk_pitch_contour_iCDF[ 34 ];
extern const opus_uint8  silk_pitch_contour_NB_iCDF[ 11 ];
extern const opus_uint8  silk_pitch_contour_10_ms_iCDF[ 12 ];
extern const opus_uint8  silk_pitch_contour_10_ms_NB_iCDF[ 3 ];
extern const opus_uint8  silk_pulses_per_block_iCDF[ N_RATE_LEVELS ][ SILK_MAX_PULSES + 2 ];
extern const opus_uint8  silk_pulses_per_block_BITS_Q5[ N_RATE_LEVELS - 1 ][ SILK_MAX_PULSES + 2 ];
extern const opus_uint8  silk_rate_levels_iCDF[ 2 ][ N_RATE_LEVELS - 1 ];
extern const opus_uint8  silk_rate_levels_BITS_Q5[ 2 ][ N_RATE_LEVELS - 1 ];
extern const opus_uint8  silk_max_pulses_table[ 4 ];
extern const opus_uint8  silk_shell_code_table0[ 152 ];
extern const opus_uint8  silk_shell_code_table1[ 152 ];
extern const opus_uint8  silk_shell_code_table2[ 152 ];
extern const opus_uint8  silk_shell_code_table3[ 152 ];
extern const opus_uint8  silk_shell_code_table_offsets[ SILK_MAX_PULSES + 1 ];
extern const opus_uint8  silk_lsb_iCDF[ 2 ];
extern const opus_uint8  silk_sign_iCDF[ 42 ];
extern const opus_uint8  silk_uniform3_iCDF[ 3 ];
extern const opus_uint8  silk_uniform4_iCDF[ 4 ];
extern const opus_uint8  silk_uniform5_iCDF[ 5 ];
extern const opus_uint8  silk_uniform6_iCDF[ 6 ];
extern const opus_uint8  silk_uniform8_iCDF[ 8 ];
extern const opus_uint8  silk_NLSF_EXT_iCDF[ 7 ];
extern const opus_uint8  silk_LTP_per_index_iCDF[ 3 ];
extern const opus_uint8  * const silk_LTP_gain_iCDF_ptrs[ NB_LTP_CBKS ];
extern const opus_uint8  * const silk_LTP_gain_BITS_Q5_ptrs[ NB_LTP_CBKS ];
extern const opus_uint8  silk_LTPscale_iCDF[ 3 ];
extern const opus_uint8  silk_type_offset_VAD_iCDF[ 4 ];
extern const opus_uint8  silk_type_offset_no_VAD_iCDF[ 2 ];
extern const opus_uint8  silk_stereo_pred_joint_iCDF[ 25 ];
extern const opus_uint8  silk_stereo_only_code_mid_iCDF[ 2 ];
extern const opus_uint8  * const silk_LBRR_flags_iCDF_ptr[ 2 ];
extern const opus_uint8  silk_NLSF_interpolation_factor_iCDF[ 5 ];
extern const silk_NLSF_CB_struct silk_NLSF_CB_WB;
extern const silk_NLSF_CB_struct silk_NLSF_CB_NB_MB;


typedef struct {
    opus_int                    fs_kHz;
    opus_int                    nb_subfr;
    opus_int                    frame_length;
    opus_int                    subfr_length;
    opus_int                    LPC_order;
    const opus_uint8            *pitch_lag_low_bits_iCDF;
    const opus_uint8            *pitch_contour_iCDF;
    const silk_NLSF_CB_struct   *psNLSF_CB;
    opus_int                    nFramesDecoded;
    opus_int                    nFramesPerPacket;
    opus_int                    ec_prevSignalType;
    opus_int16                  ec_prevLagIndex;
    opus_int                    VAD_flags[ MAX_FRAMES_PER_PACKET ];
    opus_int                    LBRR_flag;
    opus_int                    LBRR_flags[ MAX_FRAMES_PER_PACKET ];
    SideInfoIndices             indices;
} silk_decoder_state;

typedef struct {
    silk_decoder_state          channel_state[ DECODER_NUM_CHANNELS ];
    opus_int                    nChannelsAPI;
    opus_int                    nChannelsInternal;
    opus_int                    prev_decode_only_middle;
} silk_decoder;

/*  What the Opus layer tells the parser per packet (control.h, trimmed to
    the fields the decode path reads).  */
typedef struct {
    opus_int32 nChannelsAPI;
    opus_int32 nChannelsInternal;
    opus_int32 API_sampleRate;
    opus_int32 internalSampleRate;
    opus_int   payloadSize_ms;
} silk_DecControlStruct;


void silk_InitDecoder(silk_decoder *psDec);
void silk_ResetDecoder(silk_decoder *psDec);
/*  Parse one frame's worth of SILK symbols from psRangeDec.  0 on success,
    a SILK_DEC_* code when the control settings are not a legal stream.  */
opus_int silk_Decode(silk_decoder *psDec, silk_DecControlStruct *decControl,
                     opus_int lostFlag, opus_int newPacketFlag, ec_dec *psRangeDec);

void silk_decode_indices(silk_decoder_state *psDec, ec_dec *psRangeDec,
                         opus_int FrameIndex, opus_int decode_LBRR, opus_int condCoding);
void silk_decode_pulses(ec_dec *psRangeDec, opus_int16 pulses[],
                        const opus_int signalType, const opus_int quantOffsetType,
                        const opus_int frame_length);
void silk_shell_decoder(opus_int16 *pulses0, ec_dec *psRangeDec, const opus_int pulses4);
void silk_decode_signs(ec_dec *psRangeDec, opus_int16 pulses[], opus_int length,
                       const opus_int signalType, const opus_int quantOffsetType,
                       const opus_int sum_pulses[ MAX_NB_SHELL_BLOCKS ]);
void silk_NLSF_unpack(opus_int16 ec_ix[], opus_uint8 pred_Q8[],
                      const silk_NLSF_CB_struct *psNLSF_CB, const opus_int CB1_index);
void silk_stereo_decode_pred(ec_dec *psRangeDec);
void silk_stereo_decode_mid_only(ec_dec *psRangeDec, opus_int *decode_only_mid);

#endif
