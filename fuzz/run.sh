#!/bin/bash
# fuzz/run.sh <dec|enc|ogg|opus> [seconds] [jobs]
#
# Rejection uses longjmp, so leak checks are disabled.
set -u
cd "$(dirname "$0")" || exit 2
T=${1:?usage: fuzz/run.sh <dec|enc|ogg|opus> [seconds] [jobs]}
SECS=${2:-600}; J=${3:-2}
S=${BLR_FZ_DIR:-/tmp/blrfuzz}
RSS=${RSS_MB:-2048}
mkdir -p "$S/c_$T" "$S/art_$T"
SEED=${SEEDS:-$S/seed_$T}
CORPUS=("$S/c_$T")
[ ! -e "$SEED" ] || CORPUS+=("$SEED")
export ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:allocator_may_return_null=1
export UBSAN_OPTIONS=print_stacktrace=1
exec ./fuzz_"$T" -max_total_time="$SECS" -jobs="$J" -workers="$J" \
  -max_len="${MAXLEN:-65536}" -rss_limit_mb="$RSS" \
  -malloc_limit_mb="${MALLOC_MB:-1024}" -timeout=25 \
  -artifact_prefix="$S/art_$T/" -print_final_stats=1 \
  "${CORPUS[@]}"
