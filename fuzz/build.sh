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

#  Build the libFuzzer harnesses with non-recovering ASan and UBSan.

set -eu
cd "$(dirname "$0")" || exit 2
TOP=$(cd .. && pwd)
BUILD=${BUILD:-$TOP}
OUT=${OUT:-$TOP/fuzz}
read -r -a CC_CMD <<< "${CC:-clang}"
read -r -a LD_CMD <<< "${LDFLAGS:-}"
case ${FZ_SAN:-asan} in
  asan) SAN=(-fsanitize=address,undefined -fno-sanitize-recover=all)
        OPT=-O1;  COV=-fsanitize=fuzzer-no-link
        LINK=(-fsanitize=fuzzer -Wl,--wrap=__sanitizer_cov_8bit_counters_init) ;;
  none) SAN=();  OPT=-O2;  COV=-fsanitize=fuzzer-no-link
        LINK=(-fsanitize=fuzzer -Wl,--wrap=__sanitizer_cov_8bit_counters_init) ;;
  standalone) SAN=(-DFZ_STANDALONE);  OPT=-O2;  COV=;  LINK=("$TOP/fuzz/fzmain.c") ;;
  *) echo "fuzz/build.sh: FZ_SAN is asan, none or standalone" >&2;  exit 2 ;;
esac
FLAGS=(-g "$OPT" -std=c99 -fno-delete-null-pointer-checks -DHAVE_CONFIG_H
       "-I$BUILD" "-I$TOP/src" "${SAN[@]}")
test -f "$BUILD/config.h" ||
  { echo "fuzz/build.sh: no config.h in $BUILD; configure first" >&2;  exit 2; }
HARNESSES=${*:-"enc ogg dec opus odec mp3 mdec cli dmg"}
OBJ=$OUT/obj
mkdir -p "$OBJ"

objs=()
build() {               # object, flags..., source
  local o=$1;  shift
  objs+=("$o")
  ( "${CC_CMD[@]}" "${FLAGS[@]}" $COV "$@" -c -o "$o" || touch "$OBJ/failed" ) &
  while [ "$(jobs -rp | wc -l)" -ge "${JOBS:-8}" ]; do sleep 0.1; done
}
rm -f "$OBJ/failed"
for s in sys/common sys/map sys/file sys/cpu sys/crc coder/rc coder/model \
         coder/cm coder/cmmix coder/acm fmt/arc fmt/cmt fmt/ogg vorbis/pack \
         vorbis/vb vorbis/acm vorbis/zc vorbis/dict mp3/mp3 mp3/tab mp3/z; do
  build "$OBJ/$(echo "$s" | tr / _).o" "$TOP/src/$s.c"
done
for s in "$TOP"/src/opus/opus.c "$TOP"/src/opus/acm.c "$TOP"/src/opus/parse/*.c; do
  build "$OBJ/op_$(basename "$s" .c).o" "$s"
done
if grep -q '^#define HAVE_SSE2' "$BUILD/config.h"; then
  build "$OBJ/cmmix_sse2.o" -msse2 -DBLR_CM_SSE2 "$TOP/src/coder/cmmix.c"
fi
if grep -q '^#define HAVE_AVX2' "$BUILD/config.h"; then
  build "$OBJ/cmmix_avx2.o" -mavx2 -DBLR_CM_AVX2 "$TOP/src/coder/cmmix.c"
  build "$OBJ/acmmix_avx2.o" -mavx2 -DBLR_CM_AVX2 "$TOP/src/coder/acmmix.c"
fi
if grep -q '^#define HAVE_SSE42' "$BUILD/config.h"; then
  build "$OBJ/crcx86.o" -msse4.2 "$TOP/src/sys/crcx86.c"
fi
if grep -q '^#define HAVE_CLMUL' "$BUILD/config.h"; then
  build "$OBJ/oggclmul.o" -msse4.1 -mpclmul "$TOP/src/fmt/oggclmul.c"
fi
if grep -q '^#define HAVE_ARM_CRC32' "$BUILD/config.h"; then
  build "$OBJ/crcarm.o" -march=armv8-a+crc "$TOP/src/sys/crcarm.c"
fi
lib=("${objs[@]}")
objs=()
build "$OBJ/cli_main.o" -Dmain=blr_cli_main "$TOP/src/main.c"
wait
[ ! -e "$OBJ/failed" ] || { echo "fuzz/build.sh: a source did not compile" >&2;  exit 1; }

for t in $HARNESSES; do
  extra=()
  case $t in cli|dmg) extra=("$OBJ/cli_main.o") ;; esac
  "${CC_CMD[@]}" "${FLAGS[@]}" "${LINK[@]}" "-I$TOP/fuzz" \
    -o "$OUT/fuzz_$t" "$TOP/fuzz/fuzz_$t.c" \
    "${extra[@]}" "${lib[@]}" "${LD_CMD[@]}" -lpthread
done
echo "built: $HARNESSES"
