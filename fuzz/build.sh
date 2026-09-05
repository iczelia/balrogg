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

#  Build libFuzzer harnesses with non-recovering ASan and UBSan.
#
#  BUILD selects the configured directory containing config.h.
#
#    fuzz/build.sh
#    BUILD=../build fuzz/build.sh
set -eu
cd "$(dirname "$0")" || exit 2
TOP=$(cd .. && pwd)
BUILD=${BUILD:-$TOP}
read -r -a CC_CMD <<< "${CC:-clang}"
FLAGS=(-g -O1 -std=c99 -DBLR_FUZZ -DHAVE_CONFIG_H "-I$BUILD" "-I$TOP/src"
       "-fsanitize=fuzzer,address,undefined" -fno-sanitize-recover=all)
SRC=("$TOP/src/common.c" "$TOP/src/file.c" "$TOP/src/rc.c" "$TOP/src/archive.c"
     "$TOP/src/model.c" "$TOP/src/ogg.c" "$TOP/src/vorbis.c"
     "$TOP/src/codec.c" "$TOP/src/cm.c" "$TOP/src/cmmix.c" "$TOP/src/cpu.c")
test -f "$BUILD/config.h" || { echo "fuzz/build.sh: no config.h in $BUILD; configure first" >&2;  exit 2; }
if grep -q '^#define HAVE_SSE2' "$BUILD/config.h"; then
  "${CC_CMD[@]}" "${FLAGS[@]}" -msse2 -DBLR_CM_SSE2 -c -o cmmix_sse2.o "$TOP/src/cmmix.c"
  SRC+=(cmmix_sse2.o)
fi
for t in dec enc ogg; do
  "${CC_CMD[@]}" "${FLAGS[@]}" -o "fuzz_$t" "fuzz_$t.c" "${SRC[@]}" -lm
done
built="fuzz_dec fuzz_enc fuzz_ogg"

"${CC_CMD[@]}" "${FLAGS[@]}" -I"$TOP/src/opus" -o fuzz_opus fuzz_opus.c \
   "${SRC[@]}" \
   "$TOP/src/opusmode.c" "$TOP"/src/opus/*.c -lm
built="$built fuzz_opus"
echo "built: $built"
