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

#  Check clean refusal and round-trip any accepted input.
#
#    fuzz/regress.sh [balrogg]
set -u
cd "$(dirname "$0")/.." || exit 2
BIN=${1:-$PWD/balrogg}
D=$PWD/tests/regress
T=$(mktemp -d /tmp/blrreg.XXXXXX); trap 'rm -rf "$T"' EXIT
TIMEOUT=()
if command -v timeout >/dev/null 2>&1; then TIMEOUT=(timeout 60)
elif command -v gtimeout >/dev/null 2>&1; then TIMEOUT=(gtimeout 60)
fi
fail=0 n=0
for f in "$D"/*; do
  [ -f "$f" ] || continue
  n=$((n + 1))
  case "$f" in
    *.blr) enc=0 ;;
    *)     enc=1 ;;
  esac
  if [ "$enc" -eq 1 ]; then
    BLR_MEMCAP=512 "${TIMEOUT[@]}" "$BIN" e "$f" "$T/o" >/dev/null 2>&1
  else
    BLR_MEMCAP=512 "${TIMEOUT[@]}" "$BIN" d "$f" "$T/o" >/dev/null 2>&1
  fi
  rc=$?
  #  Only success and refusal are valid.
  if [ "$rc" -gt 1 ]; then
    echo "  !! $(basename "$f") exits $rc"
    fail=1
  elif [ "$rc" -eq 0 ] && [ "$enc" -eq 1 ]; then
    if ! BLR_MEMCAP=512 "${TIMEOUT[@]}" "$BIN" d "$T/o" "$T/back" >/dev/null 2>&1 \
       || ! cmp -s "$f" "$T/back"; then
      echo "  !! $(basename "$f") was accepted but does not round-trip"
      fail=1
    fi
  fi
done
if [ "$fail" -eq 0 ]; then echo "  PASS  regress: $n reproducers, all refused cleanly"
else echo "  FAIL  regress"; fi
exit "$fail"
