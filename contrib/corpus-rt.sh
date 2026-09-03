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

#  Round-trip every file in a directory.
#
#    contrib/corpus-rt.sh <dir> [balrogg]
set -eu
D=${1:?usage: corpus-rt.sh <dir> [balrogg]}
BIN=${2:-$(cd "$(dirname "$0")/.." && pwd)/balrogg}
[ -d "$D" ] || { echo "$D is not a directory" >&2; exit 2; }
D=$(cd "$D" && pwd)
T=$(mktemp -d /tmp/blrrt.XXXXXX); trap 'rm -rf "$T"' EXIT
mkdir "$T/in" "$T/out"
shopt -s nullglob

inputs=()
staged=()
for f in "$D"/*; do
  [ -f "$f" ] || continue
  staged_file="$T/in/${f##*/}"
  inputs+=("$f")
  staged+=("$staged_file")
  ln -s "$f" "$staged_file"
done
n=${#inputs[@]}
if [ "$n" -eq 0 ]; then
  echo "corpus $D: 0/0 exact, 0 failed"
  exit 0
fi

"$BIN" --batch -- e "${staged[@]}" || true
archives=("$T/in/"*.blr)
for f in "${archives[@]}"; do ln "$f" "$T/out/${f##*/}"; done
if [ "${#archives[@]}" -gt 0 ]; then
  "$BIN" --batch -- d "$T/out/"*.blr || true
fi

ok=0
for f in "${inputs[@]}"; do
  out="$T/out/${f##*/}"
  if [ -f "$out" ] && cmp -s "$f" "$out"; then
    ok=$((ok + 1))
  else
    echo "FAIL $f"
  fi
done
bad=$((n - ok))
echo "corpus $D: $ok/$n exact, $bad failed"
[ "$bad" -eq 0 ]
