#!/bin/bash
# fuzz/triage.sh <dec|enc|ogg|opus> [artifact-dir]
# Summarize whether each artifact still reproduces.
set -u
cd "$(dirname "$0")" || exit 2
T=${1:?usage: fuzz/triage.sh <dec|enc|ogg|opus> [artifact-dir]}
S=${BLR_FZ_DIR:-/tmp/blrfuzz}
D=${2:-$S/art_$T}
export ASAN_OPTIONS=detect_leaks=0:allocator_may_return_null=1
export UBSAN_OPTIONS=print_stacktrace=1
for f in "$D"/*; do
  [ -f "$f" ] || continue
  out=$(./fuzz_"$T" -malloc_limit_mb=1024 -rss_limit_mb=2048 -timeout=20 "$f" 2>&1)
  if printf '%s' "$out" | grep -qE 'ERROR: (AddressSanitizer|libFuzzer)|runtime error:|SEGV|SUMMARY: Undefined'; then
    sig=$(printf '%s' "$out" | grep -E 'runtime error:|ERROR: AddressSanitizer|ERROR: libFuzzer' | head -1)
    frame=$(printf '%s' "$out" | grep -oE '\.\./[a-z_]+\.c:[0-9]+' | head -1)
    printf 'LIVE  %-46s %s %s\n' "$(basename "$f")" "$frame" "$sig"
  else
    printf 'dead  %-46s %s\n' "$(basename "$f")" \
      "$(printf '%s' "$out" | grep -vE '^(INFO|Running|Executed|\*\*\*|$)' | head -1)"
  fi
done
