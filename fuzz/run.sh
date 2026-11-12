#!/bin/bash
# fuzz/run.sh HARNESS [seconds] [workers]

set -u
D=$(cd "$(dirname "$0")" && pwd)
T=${1:?usage: fuzz/run.sh HARNESS [seconds] [workers]}
SECS=${2:-600}; J=${3:-2}
S=${BLR_FZ_DIR:?set BLR_FZ_DIR to a work directory}
BIN=$(cd "${FZ_BIN:-$D}" && pwd)
mkdir -p "$S/c_$T" "$S/art_$T" "$S/tmp_$T" "$S/log_$T"
cd "$S/log_$T" || exit 2
export TMPDIR=$S/tmp_$T
export ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:allocator_may_return_null=0
ASAN_OPTIONS=$ASAN_OPTIONS:handle_abort=1:malloc_context_size=20
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1:abort_on_error=1
CORPUS=("$S/c_$T")
[ -z "${SEEDS:-}" ] || CORPUS+=("$SEEDS")
if [ "${FZ_MODE:-jobs}" = standalone ]; then
  end=$(($(date +%s) + SECS))
  for w in $(seq "$J"); do
    ( k=0;  R=
      while [ "$(date +%s)" -lt "$end" ]; do
        k=$((k + 1))
        "$BIN/fuzz_$T" -t $((end - $(date +%s))) -s "$w$k$$" \
          -m "${MAXLEN:-16384}" -c "$S/tmp_$T/cur$w" $R "${CORPUS[@]}" \
          > "$S/log_$T/sa$w.$k.log" 2>&1 ||
          cp "$S/tmp_$T/cur$w" "$S/art_$T/crash-sa$w-$k"
        R=-R
      done ) &
  done
  wait
  exit 0
fi
if [ "${FZ_MODE:-jobs}" = fork ]; then
  MODE=(-fork="$J" -ignore_crashes=1 -ignore_timeouts=1 -ignore_ooms=1)
else
  MODE=(-jobs=$((J * 1000)) -workers="$J" -reload=1)
fi
exec timeout "$((SECS + 60))" "$BIN/fuzz_$T" "${MODE[@]}" \
  -max_total_time="$SECS" -max_len="${MAXLEN:-16384}" \
  -rss_limit_mb="${RSS_MB:-3072}" -timeout="${TIMEOUT:-120}" -dict="$D/balrogg.dict" \
  -artifact_prefix="$S/art_$T/" -print_final_stats=1 "${CORPUS[@]}"
