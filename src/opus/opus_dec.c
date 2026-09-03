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

/*  Opus packet and frame dispatch with audio processing removed.  */

#include <stdlib.h>
#include <string.h>
#include "opusdec.h"
#include "celt.h"
#include "silk.h"

#define MODE_SILK_ONLY 1000
#define MODE_HYBRID    1001
#define MODE_CELT_ONLY 1002

struct OpusDecoder {
   silk_decoder silk;
   silk_DecControlStruct DecControl;
   CELTDecoder celt;
   int channels;
   int stream_channels;
   int bandwidth;
   int mode;
   int prev_mode;
   int frame_size;
   int prev_redundancy;
};

OpusDecoder *opus_decoder_create(int channels)
{
   OpusDecoder *st;
   if (channels != 1 && channels != 2) return NULL;
   st = (OpusDecoder *) calloc(1, sizeof(*st));
   if (st == NULL) return NULL;
   st->stream_channels = st->channels = channels;
   st->DecControl.API_sampleRate = 48000;
   st->DecControl.nChannelsAPI = channels;
   silk_InitDecoder(&st->silk);
   celt_decoder_init(&st->celt, channels);
   st->prev_mode = 0;
   st->frame_size = 48000/400;
   return st;
}

void opus_decoder_destroy(OpusDecoder *st) { free(st); }


static int parse_size(const unsigned char *data, opus_int32 len, opus_int16 *size)
{
   if (len<1)
   {
      *size = -1;
      return -1;
   } else if (data[0]<252)
   {
      *size = data[0];
      return 1;
   } else if (len<2)
   {
      *size = -1;
      return -1;
   } else {
      *size = 4*data[1] + data[0];
      return 2;
   }
}

static int opus_packet_get_samples_per_frame(const unsigned char *data, opus_int32 Fs)
{
   int audiosize;
   if (data[0]&0x80)
   {
      audiosize = ((data[0]>>3)&0x3);
      audiosize = (Fs<<audiosize)/400;
   } else if ((data[0]&0x60) == 0x60)
   {
      audiosize = (data[0]&0x08) ? Fs/50 : Fs/100;
   } else {
      audiosize = ((data[0]>>3)&0x3);
      if (audiosize == 3)
         audiosize = Fs*60/1000;
      else
         audiosize = (Fs<<audiosize)/100;
   }
   return audiosize;
}

static int opus_packet_get_bandwidth(const unsigned char *data)
{
   int bandwidth;
   if (data[0]&0x80)
   {
      bandwidth = OPUS_BANDWIDTH_MEDIUMBAND + ((data[0]>>5)&0x3);
      if (bandwidth == OPUS_BANDWIDTH_MEDIUMBAND)
         bandwidth = OPUS_BANDWIDTH_NARROWBAND;
   } else if ((data[0]&0x60) == 0x60)
   {
      bandwidth = (data[0]&0x10) ? OPUS_BANDWIDTH_FULLBAND :
                                   OPUS_BANDWIDTH_SUPERWIDEBAND;
   } else {
      bandwidth = OPUS_BANDWIDTH_NARROWBAND + ((data[0]>>5)&0x3);
   }
   return bandwidth;
}

static int opus_packet_get_nb_channels(const unsigned char *data)
{
   return (data[0]&0x4) ? 2 : 1;
}

static int opus_packet_get_mode(const unsigned char *data)
{
   int mode;
   if (data[0]&0x80)
   {
      mode = MODE_CELT_ONLY;
   } else if ((data[0]&0x60) == 0x60)
   {
      mode = MODE_HYBRID;
   } else {
      mode = MODE_SILK_ONLY;
   }
   return mode;
}

int opus_packet_parse(const unsigned char *data, opus_int32 len,
      unsigned char *out_toc, const unsigned char *frames[48],
      opus_int16 size[48], int *payload_offset)
{
   int i, bytes;
   int count;
   int cbr;
   unsigned char ch, toc;
   int framesize;
   opus_int32 last_size;
   const unsigned char *data0 = data;

   if (size==NULL || len<0)
      return OPUS_BAD_ARG;
   if (len==0)
      return OPUS_INVALID_PACKET;

   framesize = opus_packet_get_samples_per_frame(data, 48000);

   cbr = 0;
   toc = *data++;
   len--;
   last_size = len;
   switch (toc&0x3)
   {
   /* One frame */
   case 0:
      count=1;
      break;
   /* Two CBR frames */
   case 1:
      count=2;
      cbr = 1;
      if (len&0x1)
         return OPUS_INVALID_PACKET;
      last_size = len/2;
      /* If last_size doesn't fit in size[0], we'll catch it later */
      size[0] = (opus_int16)last_size;
      break;
   /* Two VBR frames */
   case 2:
      count = 2;
      bytes = parse_size(data, len, size);
      len -= bytes;
      if (size[0]<0 || size[0] > len)
         return OPUS_INVALID_PACKET;
      data += bytes;
      last_size = len-size[0];
      break;
   /* Multiple CBR/VBR frames (from 0 to 120 ms) */
   default: /*case 3:*/
      if (len<1)
         return OPUS_INVALID_PACKET;
      /* Number of frames encoded in bits 0 to 5 */
      ch = *data++;
      count = ch&0x3F;
      if (count <= 0 || framesize*(opus_int32)count > 5760)
         return OPUS_INVALID_PACKET;
      len--;
      /* Padding flag is bit 6 */
      if (ch&0x40)
      {
         int p;
         do {
            int tmp;
            if (len<=0)
               return OPUS_INVALID_PACKET;
            p = *data++;
            len--;
            tmp = p==255 ? 254: p;
            len -= tmp;
         } while (p==255);
      }
      if (len<0)
         return OPUS_INVALID_PACKET;
      /* VBR flag is bit 7 */
      cbr = !(ch&0x80);
      if (!cbr)
      {
         /* VBR case */
         last_size = len;
         for (i=0;i<count-1;i++)
         {
            bytes = parse_size(data, len, size+i);
            len -= bytes;
            if (size[i]<0 || size[i] > len)
               return OPUS_INVALID_PACKET;
            data += bytes;
            last_size -= bytes+size[i];
         }
         if (last_size<0)
            return OPUS_INVALID_PACKET;
      } else
      {
         /* CBR case */
         last_size = len/count;
         if (last_size*count!=len)
            return OPUS_INVALID_PACKET;
         for (i=0;i<count-1;i++)
            size[i] = (opus_int16)last_size;
      }
      break;
   }
   /* Because it's not encoded explicitly, it's possible the size of the
      last packet (or all the packets, for the CBR case) is larger than
      1275. Reject them here.*/
   if (last_size > 1275)
      return OPUS_INVALID_PACKET;
   size[count-1] = (opus_int16)last_size;

   if (payload_offset)
      *payload_offset = (int)(data-data0);

   for (i=0;i<count;i++)
   {
      if (frames)
         frames[i] = data;
      data += size[i];
   }

   if (out_toc)
      *out_toc = toc;

   return count;
}


static int opus_decode_frame(OpusDecoder *st, const unsigned char *data, opus_int32 len)
{
   int silk_ret=0, celt_ret=0;
   ec_dec dec;
   int audiosize;
   int frame_size;
   int mode;
   int bandwidth;
   int start_band;
   int redundancy=0;
   int redundancy_bytes = 0;
   int celt_to_silk=0;
   int F2_5, F5, F10, F20;

   F20 = 48000/50;
   F10 = F20>>1;
   F5 = F10>>1;
   F2_5 = F5>>1;
   (void) F10;
   /* Payloads of 1 (2 including ToC) or 0 trigger the PLC/DTX */
   if (len<=1)
   {
      /* Concealment never reads a one-byte frame, so no entropy mirror
         would store its byte: store it whole against a zero mirror. */
      if (len==1)
      {
         unsigned char zero = 0;
         om_frame((unsigned char *)data, &zero, 1);
      }
      data = NULL;
   }
   if (data != NULL)
   {
      audiosize = st->frame_size;
      mode = st->mode;
      bandwidth = st->bandwidth;
      ec_dec_init(&dec,(unsigned char*)data,len);
   } else {
      /* Concealment reads no symbols; only the mode bookkeeping that the
         next packet's resets depend on is kept.  */
      mode = st->prev_redundancy ? MODE_CELT_ONLY : st->prev_mode;
      if (mode == 0)
         return OPUS_OK;
      st->prev_mode = mode;
      st->prev_redundancy = 0;
      return OPUS_OK;
   }
   frame_size = audiosize;

   /* SILK processing */
   if (mode != MODE_CELT_ONLY)
   {
      int decoded_samples;

      if (st->prev_mode==MODE_CELT_ONLY)
         silk_ResetDecoder( &st->silk );

      /* The SILK PLC cannot produce frames of less than 10 ms */
      st->DecControl.payloadSize_ms = IMAX(10, 1000 * audiosize / 48000);

      st->DecControl.nChannelsInternal = st->stream_channels;
      if( mode == MODE_SILK_ONLY ) {
         if( bandwidth == OPUS_BANDWIDTH_NARROWBAND ) {
            st->DecControl.internalSampleRate = 8000;
         } else if( bandwidth == OPUS_BANDWIDTH_MEDIUMBAND ) {
            st->DecControl.internalSampleRate = 12000;
         } else if( bandwidth == OPUS_BANDWIDTH_WIDEBAND ) {
            st->DecControl.internalSampleRate = 16000;
         } else {
            st->DecControl.internalSampleRate = 16000;
         }
      } else {
         /* Hybrid mode */
         st->DecControl.internalSampleRate = 16000;
      }

      decoded_samples = 0;
      do {
         /* Call SILK decoder */
         int first_frame = decoded_samples == 0;
         int silk_frame_size;
         silk_ret = silk_Decode( &st->silk, &st->DecControl, FLAG_DECODE_NORMAL, first_frame, &dec );
         if( silk_ret )
            return OPUS_INTERNAL_ERROR;
         /* One SILK frame at the API rate: 10 or 20 ms.  */
         silk_frame_size = st->silk.channel_state[0].frame_length * 48000
                         / (st->silk.channel_state[0].fs_kHz * 1000);
         decoded_samples += silk_frame_size;
      } while( decoded_samples < frame_size );
   }

   start_band = 0;
   if (mode != MODE_CELT_ONLY
    && ec_tell(&dec)+17+20*(mode == MODE_HYBRID) <= 8*len)
   {
      /* Check if we have a redundant 0-8 kHz band */
      if (mode == MODE_HYBRID)
         redundancy = ec_dec_bit_logp(&dec, 12, OREC_S_OPUS_REDUNDANCY);
      else
         redundancy = 1;
      if (redundancy)
      {
         celt_to_silk = ec_dec_bit_logp(&dec, 1, OREC_S_OPUS_CELT_TO_SILK);
         /* redundancy_bytes will be at least two, in the non-hybrid
            case due to the ec_tell() check above */
         redundancy_bytes = mode==MODE_HYBRID ?
               (opus_int32)ec_dec_uint(&dec, 256, OREC_S_OPUS_RED_BYTES)+2 :
               len-((ec_tell(&dec)+7)>>3);
         len -= redundancy_bytes;
         /* This is a sanity check. It should never happen for a valid
            packet, so the exact behaviour is not normative. */
         if (len*8 < ec_tell(&dec))
         {
            len = 0;
            redundancy_bytes = 0;
            redundancy = 0;
         }
         /* Shrink decoder because of raw bits */
         dec.storage -= redundancy_bytes;
      }
   }
   if (mode != MODE_CELT_ONLY)
      start_band = 17;

   {
      int endband=21;

      switch(bandwidth)
      {
      case OPUS_BANDWIDTH_NARROWBAND:
         endband = 13;
         break;
      case OPUS_BANDWIDTH_MEDIUMBAND:
      case OPUS_BANDWIDTH_WIDEBAND:
         endband = 17;
         break;
      case OPUS_BANDWIDTH_SUPERWIDEBAND:
         endband = 19;
         break;
      case OPUS_BANDWIDTH_FULLBAND:
         endband = 21;
         break;
      default:
         break;
      }
      st->celt.end = endband;
   }
   st->celt.stream_channels = st->stream_channels;

   /* 5 ms redundant frame for CELT->SILK */
   if (redundancy && celt_to_silk)
   {
      st->celt.start = 0;
      celt_decode_with_ec(&st->celt, data+len, redundancy_bytes, F5, NULL);
   }

   /* MUST be after PLC */
   st->celt.start = start_band;

   if (mode != MODE_SILK_ONLY)
   {
      int celt_frame_size = IMIN(F20, frame_size);
      /* Make sure to discard any previous CELT state */
      if (mode != st->prev_mode && st->prev_mode > 0 && !st->prev_redundancy)
         celt_decoder_reset(&st->celt);
      /* Decode CELT */
      celt_ret = celt_decode_with_ec(&st->celt, data, len, celt_frame_size, &dec);
   } else {
      unsigned char silence[2] = {0xFF, 0xFF};
      /* For hybrid -> SILK transitions, we let the CELT MDCT
         do a fade-out by decoding a silence frame */
      if (st->prev_mode == MODE_HYBRID && !(redundancy && celt_to_silk && st->prev_redundancy) )
      {
         st->celt.start = 0;
         celt_decode_with_ec(&st->celt, silence, 2, F2_5, NULL);
      }
   }

   /* 5 ms redundant frame for SILK->CELT */
   if (redundancy && !celt_to_silk)
   {
      celt_decoder_reset(&st->celt);
      st->celt.start = 0;
      celt_decode_with_ec(&st->celt, data+len, redundancy_bytes, F5, NULL);
   }

   st->prev_mode = mode;
   st->prev_redundancy = redundancy && !celt_to_silk;

   return celt_ret < 0 ? celt_ret : OPUS_OK;
}

int opus_decode(OpusDecoder *st, const unsigned char *data, opus_int32 len)
{
   int i;
   int count, offset;
   unsigned char toc;
   int packet_frame_size, packet_bandwidth, packet_mode, packet_stream_channels;
   /* 48 x 2.5 ms = 120 ms */
   opus_int16 size[48];

   if (len==0 || data==NULL)
      return opus_decode_frame(st, NULL, 0);
   else if (len<0)
      return OPUS_BAD_ARG;

   packet_mode = opus_packet_get_mode(data);
   packet_bandwidth = opus_packet_get_bandwidth(data);
   packet_frame_size = opus_packet_get_samples_per_frame(data, 48000);
   packet_stream_channels = opus_packet_get_nb_channels(data);

   count = opus_packet_parse(data, len, &toc, NULL, size, &offset);
   if (count<0)
      return count;

   data += offset;

   if (count*packet_frame_size > 5760)
      return OPUS_BUFFER_TOO_SMALL;

   /* Update the state as the last step to avoid updating it on an invalid packet */
   st->mode = packet_mode;
   st->bandwidth = packet_bandwidth;
   st->frame_size = packet_frame_size;
   st->stream_channels = packet_stream_channels;

   for (i=0;i<count;i++)
   {
      int ret;
      ret = opus_decode_frame(st, data, size[i]);
      if (ret<0)
         return ret;
      data += size[i];
   }
   return OPUS_OK;
}
