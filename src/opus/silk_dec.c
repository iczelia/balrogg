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

/*  SILK packet control flow with synthesis removed.  */

#include "silk.h"

static void silk_init_decoder(silk_decoder_state *psDec) {
    silk_memset(psDec, 0, sizeof(*psDec));
}

void silk_InitDecoder(silk_decoder *psDec) {
    silk_memset(psDec, 0, sizeof(*psDec));
}

void silk_ResetDecoder(silk_decoder *psDec) {
    opus_int n;
    for (n = 0; n < DECODER_NUM_CHANNELS; n++) silk_init_decoder(&psDec->channel_state[n]);
    psDec->prev_decode_only_middle = 0;
}

/*  Set decoder sampling rate: which tables the parse reads from.  */
static void silk_decoder_set_fs(silk_decoder_state *psDec, opus_int fs_kHz) {
    opus_int frame_length;

    psDec->subfr_length = silk_SMULBB(SUB_FRAME_LENGTH_MS, fs_kHz);
    frame_length = silk_SMULBB(psDec->nb_subfr, psDec->subfr_length);

    if (psDec->fs_kHz != fs_kHz || frame_length != psDec->frame_length) {
        if (fs_kHz == 8) {
            if (psDec->nb_subfr == MAX_NB_SUBFR) psDec->pitch_contour_iCDF = silk_pitch_contour_NB_iCDF;
            else psDec->pitch_contour_iCDF = silk_pitch_contour_10_ms_NB_iCDF;
        } else {
            if (psDec->nb_subfr == MAX_NB_SUBFR) psDec->pitch_contour_iCDF = silk_pitch_contour_iCDF;
            else psDec->pitch_contour_iCDF = silk_pitch_contour_10_ms_iCDF;
        }
        if (psDec->fs_kHz != fs_kHz) {
            if (fs_kHz == 8 || fs_kHz == 12) {
                psDec->LPC_order = MIN_LPC_ORDER;
                psDec->psNLSF_CB = &silk_NLSF_CB_NB_MB;
            } else {
                psDec->LPC_order = MAX_LPC_ORDER;
                psDec->psNLSF_CB = &silk_NLSF_CB_WB;
            }
            if (fs_kHz == 16) psDec->pitch_lag_low_bits_iCDF = silk_uniform8_iCDF;
            else if (fs_kHz == 12) psDec->pitch_lag_low_bits_iCDF = silk_uniform6_iCDF;
            else psDec->pitch_lag_low_bits_iCDF = silk_uniform4_iCDF;
        }
        psDec->fs_kHz = fs_kHz;
        psDec->frame_length = frame_length;
    }
}

/*  One frame of one channel: the side information, then the excitation.
    The synthesis that followed them read nothing from the stream.  */
static void silk_decode_frame(silk_decoder_state *psDec, ec_dec *psRangeDec,
                              opus_int lostFlag, opus_int condCoding) {
    if (lostFlag == FLAG_DECODE_NORMAL ||
        (lostFlag == FLAG_DECODE_LBRR && psDec->LBRR_flags[psDec->nFramesDecoded] == 1)) {
        opus_int16 pulses[(MAX_FRAME_LENGTH + SHELL_CODEC_FRAME_LENGTH - 1) &
                          ~(SHELL_CODEC_FRAME_LENGTH - 1)];
        silk_decode_indices(psDec, psRangeDec, psDec->nFramesDecoded, lostFlag, condCoding);
        silk_decode_pulses(psRangeDec, pulses, psDec->indices.signalType,
                           psDec->indices.quantOffsetType, psDec->frame_length);
    }
}

opus_int silk_Decode(silk_decoder *psDec, silk_DecControlStruct *decControl,
                     opus_int lostFlag, opus_int newPacketFlag, ec_dec *psRangeDec) {
    opus_int   i, n, decode_only_middle = 0;
    opus_int32 LBRR_symbol;
    silk_decoder_state *channel_state = psDec->channel_state;
    opus_int has_side;

    celt_assert(decControl->nChannelsInternal == 1 || decControl->nChannelsInternal == 2);

    if (newPacketFlag) {
        for (n = 0; n < decControl->nChannelsInternal; n++)
            channel_state[n].nFramesDecoded = 0;
    }

    /* If Mono -> Stereo transition in bitstream: init state of second channel */
    if (decControl->nChannelsInternal > psDec->nChannelsInternal)
        silk_init_decoder(&channel_state[1]);

    if (channel_state[0].nFramesDecoded == 0) {
        for (n = 0; n < decControl->nChannelsInternal; n++) {
            opus_int fs_kHz_dec;
            if (decControl->payloadSize_ms == 0 || decControl->payloadSize_ms == 10) {
                channel_state[n].nFramesPerPacket = 1;
                channel_state[n].nb_subfr = 2;
            } else if (decControl->payloadSize_ms == 20) {
                channel_state[n].nFramesPerPacket = 1;
                channel_state[n].nb_subfr = 4;
            } else if (decControl->payloadSize_ms == 40) {
                channel_state[n].nFramesPerPacket = 2;
                channel_state[n].nb_subfr = 4;
            } else if (decControl->payloadSize_ms == 60) {
                channel_state[n].nFramesPerPacket = 3;
                channel_state[n].nb_subfr = 4;
            } else {
                return SILK_DEC_INVALID_FRAME_SIZE;
            }
            fs_kHz_dec = (decControl->internalSampleRate >> 10) + 1;
            if (fs_kHz_dec != 8 && fs_kHz_dec != 12 && fs_kHz_dec != 16)
                return SILK_DEC_INVALID_SAMPLING_FREQUENCY;
            silk_decoder_set_fs(&channel_state[n], fs_kHz_dec);
        }
    }

    psDec->nChannelsAPI      = decControl->nChannelsAPI;
    psDec->nChannelsInternal = decControl->nChannelsInternal;

    if (decControl->API_sampleRate > (opus_int32) MAX_API_FS_KHZ * 1000 || decControl->API_sampleRate < 8000)
        return SILK_DEC_INVALID_SAMPLING_FREQUENCY;

    if (lostFlag != FLAG_PACKET_LOST && channel_state[0].nFramesDecoded == 0) {
        /* First decoder call for this payload: decode VAD flags and LBRR flag */
        for (n = 0; n < decControl->nChannelsInternal; n++) {
            for (i = 0; i < channel_state[n].nFramesPerPacket; i++)
                channel_state[n].VAD_flags[i] = ec_dec_bit_logp(psRangeDec, 1, OREC_S_SILK_VAD);
            channel_state[n].LBRR_flag = ec_dec_bit_logp(psRangeDec, 1, OREC_S_SILK_LBRR_FLAG);
        }
        /* Decode LBRR flags */
        for (n = 0; n < decControl->nChannelsInternal; n++) {
            silk_memset(channel_state[n].LBRR_flags, 0, sizeof(channel_state[n].LBRR_flags));
            if (channel_state[n].LBRR_flag) {
                if (channel_state[n].nFramesPerPacket == 1) {
                    channel_state[n].LBRR_flags[0] = 1;
                } else {
                    LBRR_symbol = ec_dec_icdf(psRangeDec, silk_LBRR_flags_iCDF_ptr[channel_state[n].nFramesPerPacket - 2], 8, OREC_S_SILK_LBRR_SYM) + 1;
                    for (i = 0; i < channel_state[n].nFramesPerPacket; i++)
                        channel_state[n].LBRR_flags[i] = silk_RSHIFT(LBRR_symbol, i) & 1;
                }
            }
        }

        if (lostFlag == FLAG_DECODE_NORMAL) {
            /* Regular decoding: skip all LBRR data */
            for (i = 0; i < channel_state[0].nFramesPerPacket; i++) {
                for (n = 0; n < decControl->nChannelsInternal; n++) {
                    if (channel_state[n].LBRR_flags[i]) {
                        opus_int16 pulses[MAX_FRAME_LENGTH];
                        opus_int condCoding;

                        if (decControl->nChannelsInternal == 2 && n == 0) {
                            silk_stereo_decode_pred(psRangeDec);
                            if (channel_state[1].LBRR_flags[i] == 0)
                                silk_stereo_decode_mid_only(psRangeDec, &decode_only_middle);
                        }
                        /* Use conditional coding if previous frame available */
                        if (i > 0 && channel_state[n].LBRR_flags[i - 1]) condCoding = CODE_CONDITIONALLY;
                        else condCoding = CODE_INDEPENDENTLY;
                        silk_decode_indices(&channel_state[n], psRangeDec, i, 1, condCoding);
                        silk_decode_pulses(psRangeDec, pulses, channel_state[n].indices.signalType,
                                           channel_state[n].indices.quantOffsetType, channel_state[n].frame_length);
                    }
                }
            }
        }
    }

    /* Get MS predictor index */
    if (decControl->nChannelsInternal == 2) {
        if (lostFlag == FLAG_DECODE_NORMAL ||
            (lostFlag == FLAG_DECODE_LBRR && channel_state[0].LBRR_flags[channel_state[0].nFramesDecoded] == 1)) {
            silk_stereo_decode_pred(psRangeDec);
            /* For LBRR data, decode mid-only flag only if side-channel's LBRR flag is false */
            if ((lostFlag == FLAG_DECODE_NORMAL && channel_state[1].VAD_flags[channel_state[0].nFramesDecoded] == 0) ||
                (lostFlag == FLAG_DECODE_LBRR && channel_state[1].LBRR_flags[channel_state[0].nFramesDecoded] == 0)) {
                silk_stereo_decode_mid_only(psRangeDec, &decode_only_middle);
            } else {
                decode_only_middle = 0;
            }
        }
    }

    if (lostFlag == FLAG_DECODE_NORMAL) {
        has_side = !decode_only_middle;
    } else {
        has_side = !psDec->prev_decode_only_middle
              || (decControl->nChannelsInternal == 2 && lostFlag == FLAG_DECODE_LBRR && channel_state[1].LBRR_flags[channel_state[1].nFramesDecoded] == 1);
    }
    /* Call decoder for one frame */
    for (n = 0; n < decControl->nChannelsInternal; n++) {
        if (n == 0 || has_side) {
            opus_int FrameIndex;
            opus_int condCoding;

            FrameIndex = channel_state[0].nFramesDecoded - n;
            /* Use independent coding if no previous frame available */
            if (FrameIndex <= 0) {
                condCoding = CODE_INDEPENDENTLY;
            } else if (lostFlag == FLAG_DECODE_LBRR) {
                condCoding = channel_state[n].LBRR_flags[FrameIndex - 1] ? CODE_CONDITIONALLY : CODE_INDEPENDENTLY;
            } else if (n > 0 && psDec->prev_decode_only_middle) {
                condCoding = CODE_INDEPENDENTLY_NO_LTP_SCALING;
            } else {
                condCoding = CODE_CONDITIONALLY;
            }
            silk_decode_frame(&channel_state[n], psRangeDec, lostFlag, condCoding);
        }
        channel_state[n].nFramesDecoded++;
    }

    if (lostFlag != FLAG_PACKET_LOST)
        psDec->prev_decode_only_middle = decode_only_middle;
    return SILK_NO_ERROR;
}

#ifdef BLR_OPUS_TRANSITION
/*  Compatibility shims used when comparing with upstream libopus.  */
opus_int silk_Get_Decoder_Size(opus_int *decSizeBytes);
opus_int silk_Get_Decoder_Size(opus_int *decSizeBytes) { *decSizeBytes = sizeof(silk_decoder);  return 0; }
opus_int silk_LoadOSCEModels(void *d, const unsigned char *data, int len);
opus_int silk_LoadOSCEModels(void *d, const unsigned char *data, int len) { (void) d; (void) data; (void) len;  return 0; }
opus_int silk_Decode_upstream(void *decState, void *decControl, opus_int lostFlag, opus_int newPacketFlag,
                              ec_dec *psRangeDec, void *samplesOut, opus_int32 *nSamplesOut, int arch);
opus_int silk_Decode_upstream(void *decState, void *decControl, opus_int lostFlag, opus_int newPacketFlag,
                              ec_dec *psRangeDec, void *samplesOut, opus_int32 *nSamplesOut, int arch) {
    silk_decoder *d = (silk_decoder *) decState;
    silk_DecControlStruct *c = (silk_DecControlStruct *) decControl;
    opus_int r = silk_Decode(d, c, lostFlag, newPacketFlag, psRangeDec);
    (void) samplesOut; (void) arch;
    *nSamplesOut = d->channel_state[0].frame_length * c->API_sampleRate / (d->channel_state[0].fs_kHz * 1000);
    return r;
}
#endif
