#!/bin/bash
#  Copyright (C) 2026 Kamila Szewczyk
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, version 3.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program. If not, see <http://www.gnu.org/licenses/>.

#  Generate the extended test corpus with ffmpeg and no network access.
#
#    contrib/mkdata.sh [dir]        (default: ./testdata, gitignored)
set -eu
cd "$(dirname "$0")/.."
D="${1:-$PWD/testdata}"
V="$D/vorbis"; B="$D/big"; O="$D/opus"
mkdir -p "$V" "$B" "$O"
command -v ffmpeg >/dev/null || { echo "ffmpeg is required" >&2; exit 2; }
command -v python3 >/dev/null || { echo "python3 is required" >&2; exit 2; }
# Stabilize noise and muxer metadata. Pin FFmpeg for release corpora.
gen() { ffmpeg -hide_banner -loglevel error -fflags +bitexact -flags:a +bitexact -y "$@"; }

echo ">> vorbis: small, deliberately varied"
# Cover noise, silence, tones, sweeps, channels, rates, and qualities.
gen -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=0.25" -ac 2 -c:a libvorbis -q:a 3 "$V/short.ogg"
gen -f lavfi -i "anullsrc=r=44100:cl=stereo" -t 3 -c:a libvorbis -q:a 3 "$V/silence.ogg"
gen -f lavfi -i "sine=frequency=1000:sample_rate=48000:duration=2" -ac 1 -c:a libvorbis -q:a 5 "$V/tone48k.ogg"
gen -f lavfi -i "sine=frequency=220:duration=2,aeval='val(0)|-val(0)':c=stereo" -c:a libvorbis -q:a 6 "$V/tone_stereo.ogg"
gen -f lavfi -i "anoisesrc=r=44100:d=2:c=pink:a=0.5:s=1" -ac 2 -c:a libvorbis -q:a 4 "$V/noise_pink.ogg"
gen -f lavfi -i "anoisesrc=r=44100:d=2:c=white:a=0.8:s=2" -ac 1 -c:a libvorbis -q:a 2 "$V/noise_white.ogg"
gen -f lavfi -i "sine=frequency=100:duration=2,volume=1" -af "aeval='sin(2*PI*(100+2000*t)*t)'" -ac 1 -c:a libvorbis -q:a 5 "$V/sweep.ogg"
gen -f lavfi -i "sine=frequency=440:duration=2,vibrato=f=5:d=0.8" -ac 2 -c:a libvorbis -q:a 7 "$V/vibrato.ogg"
gen -f lavfi -i "sine=frequency=440:duration=1" -f lavfi -i "anoisesrc=d=1:a=0.3:s=3" \
    -filter_complex amix=inputs=2 -ac 2 -c:a libvorbis -q:a 0 "$V/mix_lowq.ogg"
gen -f lavfi -i "sine=frequency=60:sample_rate=22050:duration=2" -ac 1 -c:a libvorbis -q:a 9 "$V/lowfreq22k.ogg"
gen -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=0.05" -ac 1 -c:a libvorbis -q:a 3 "$V/tiny.ogg"

echo ">> vorbis: large, and long enough to cross the window and the ring slot"
gen -f lavfi -i "sine=frequency=440:duration=30" -ac 2 -c:a libvorbis -q:a 4 "$B/big_tone.ogg"
gen -f lavfi -i "anoisesrc=r=44100:d=25:c=pink:a=0.5:s=4" -ac 2 -c:a libvorbis -q:a 6 "$B/big_pink.ogg"
gen -f lavfi -i "anoisesrc=r=44100:d=220:c=pink:a=0.5:s=5" -ac 2 -c:a libvorbis -q:a 6 "$B/huge_pink.ogg"
gen -f lavfi -i "anullsrc=r=44100:cl=stereo" -t 300 -c:a libvorbis -q:a 3 "$B/long_silence.ogg"
gen -f lavfi -i "sine=frequency=1000:sample_rate=48000:duration=120" -ac 1 -c:a libvorbis -q:a 5 "$B/long_tone_mono.ogg"
gen -f lavfi -i "aevalsrc='0.4*sin(2*PI*(200+3000*t)*t)+0.2*random(0)':s=44100:d=60" -ac 2 -c:a libvorbis -q:a 5 "$B/mix_sweepnoise.ogg"

echo ">> vorbis chained streams"
# Mix stream parameters to exercise per-stream resets.
gen -f lavfi -i "sine=frequency=300:sample_rate=44100:duration=2" -ac 2 -c:a libvorbis -q:a 3 "$B/.c1.ogg"
gen -f lavfi -i "sine=frequency=900:sample_rate=48000:duration=2" -ac 1 -c:a libvorbis -q:a 6 "$B/.c2.ogg"
gen -f lavfi -i "anoisesrc=r=22050:d=2:c=white:a=0.4:s=6"         -ac 1 -c:a libvorbis -q:a 1 "$B/.c3.ogg"
cat "$B/.c1.ogg" "$B/.c2.ogg"                 > "$B/smallchain.ogg"
cat "$B/.c1.ogg" "$B/.c2.ogg" "$B/.c3.ogg"    > "$B/chain3.ogg"
cat "$B/big_pink.ogg" "$B/long_tone_mono.ogg" > "$B/chained.ogg"
rm -f "$B/.c1.ogg" "$B/.c2.ogg" "$B/.c3.ogg"

echo ">> vorbis: equal blocksizes (blocksize_0 == blocksize_1)"
# Include equal block sizes missing from the small corpus.
gen -f lavfi -i "sine=frequency=440:sample_rate=8000:duration=8"   -ac 1 -c:a libvorbis -b:a 16k "$B/eq_mono8k.ogg"
gen -f lavfi -i "sine=frequency=500:sample_rate=11025:duration=8"  -ac 2 -c:a libvorbis -b:a 24k "$B/eq_st11k.ogg"
gen -f lavfi -i "sine=frequency=700:sample_rate=16000:duration=8"  -ac 2 -c:a libvorbis -b:a 32k "$B/eq_st16k.ogg"
gen -f lavfi -i "anoisesrc=r=22050:d=8:c=pink:a=0.5:s=7"           -ac 1 -c:a libvorbis -b:a 24k "$B/eq_pink22k.ogg"
gen -f lavfi -i "anoisesrc=r=22050:d=8:c=white:a=0.5:s=8"          -ac 2 -c:a libvorbis -b:a 32k "$B/eq_st22k.ogg"
gen -f lavfi -i "sine=frequency=440:sample_rate=22050:duration=12" -ac 1 -c:a libvorbis -b:a 24k "$B/lowbr.ogg"

echo ">> opus: every mode the encoder can be pushed into"
# Cover CELT, hybrid, SILK, channels, frame sizes, VBR, and CBR.
osrc() { gen -f lavfi -i "$1" -ac "$2" -c:a libopus "${@:4}" "$O/$3.opus"; }
MUS="sine=frequency=440:duration=10,aeval='val(0)*0.6+0.3*sin(2*PI*880*t)':c=same"
osrc "$MUS" 2 celt_st_256k -b:a 256k -vbr on -application audio
osrc "$MUS" 2 celt_st_128k -b:a 128k -vbr on -application audio
osrc "$MUS" 2 celt_st_cbr128 -b:a 128k -vbr off -application audio
osrc "$MUS" 2 celt_st_96k_20ms -b:a 96k -frame_duration 20 -application audio
osrc "$MUS" 2 celt_st_96k_5ms  -b:a 96k -frame_duration 5  -application audio
osrc "$MUS" 2 celt_st_96k_60ms -b:a 96k -frame_duration 60 -application audio
osrc "$MUS" 1 celt_mono_96k -b:a 96k -application audio
osrc "$MUS" 2 hyb_st_48k  -b:a 48k -application audio
osrc "$MUS" 1 hyb_mono_40k -b:a 40k -application audio
SPCH="sine=frequency=200:duration=10,tremolo=f=6:d=0.9"
osrc "$SPCH" 1 silk_mono_24k -b:a 24k -application voip
osrc "$SPCH" 1 silk_mono_16k -b:a 16k -application voip
osrc "$SPCH" 1 silk_speech_32k -b:a 32k -application voip
osrc "$SPCH" 1 silk_speech_16k -b:a 16k -application voip
osrc "$SPCH" 1 silk_speech_12k -b:a 12k -application voip
osrc "anoisesrc=r=48000:d=10:c=pink:a=0.5:s=9" 2 noise_st_128k -b:a 128k -application audio
osrc "sine=frequency=1000:sample_rate=48000:duration=10" 1 tone_mono_64k -b:a 64k -application audio
osrc "anullsrc=r=48000:cl=stereo" 2 silence_st_64k -b:a 64k -application audio -t 10

# Normalize random Ogg serial numbers and their CRCs after chaining.
find "$D" -type f \( -name '*.ogg' -o -name '*.opus' \) -exec \
  python3 contrib/normalize-ogg.py {} +

printf ">> done: %s vorbis, %s big, %s opus in %s\n" \
  "$(find "$V" -type f | wc -l)" "$(find "$B" -type f | wc -l)" \
  "$(find "$O" -type f | wc -l)" "$D"
du -sh "$D"
