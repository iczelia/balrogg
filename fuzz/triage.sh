#!/bin/bash
# fuzz/triage.sh HARNESS [artifact-dir]

set -u
cd "$(dirname "$0")" || exit 2
T=${1:?usage: fuzz/triage.sh HARNESS [artifact-dir]}
S=${BLR_FZ_DIR:?set BLR_FZ_DIR to a work directory}
BIN=${FZ_BIN:-.}
D=${2:-$S/art_$T}
mkdir -p "$S/tmp_$T"
export TMPDIR=$S/tmp_$T
export ASAN_OPTIONS=detect_leaks=0:allocator_may_return_null=0:handle_abort=1
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1:abort_on_error=1
for f in "$D"/*; do
  [ -f "$f" ] || continue
  out=$("$BIN/fuzz_$T" -runs=1 -timeout=300 -rss_limit_mb=4096 "$f" 2>&1)
  if printf '%s' "$out" | grep -qE '==fz==|ERROR: (AddressSanitizer|libFuzzer)|runtime error:'; then
    sig=$(printf '%s' "$out" |
          grep -E '==fz==|runtime error:|ERROR: AddressSanitizer' |
          head -2 | tr '\n' ' ')
    frame=$(printf '%s' "$out" | grep -oE '/src/[a-z0-9_/]+\.[ch]:[0-9]+' |
            grep -v harness.h | head -1)
    printf 'LIVE  %-50s %s %s\n' "$(basename "$f")" "$frame" "$sig"
  else
    printf 'dead  %s\n' "$(basename "$f")"
  fi
done
