#!/bin/sh
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

#  Blocks of xmalloc, xcalloc and xrealloc go back through xfree and xrealloc: no file of src/, tests/ or fuzz/ but
#  the allocator, the Windows runtime, yarg and the Opus parser names free or realloc outside comments and strings;
#  common.h keeps the guard that stops such calls from linking; the Opus parser in src/opus/parse, which frees its own
#  blocks, takes none of balrogg's.
top=${srcdir:-.}
cd "$top" || exit 99
test -f src/sys/common.h || { echo "strayfree.sh: no src/sys/common.h in $top" >&2;  exit 99; }
status=0

strip='
  { out = "";  i = 1;  n = length($0)
    while (i <= n) {
      c = substr($0, i, 1);  d = substr($0, i, 2)
      if (cm) { if (d == "*/") { cm = 0;  i += 2 } else i++;  continue }
      if (q != "") { if (c == "\\") i += 2;  else { if (c == q) q = "";  i++ };  continue }
      if (d == "/*") { cm = 1;  i += 2;  out = out " ";  continue }
      if (d == "//") break
      if (c == "\"" || c == "'\''") { q = c;  out = out " ";  i++;  continue }
      out = out c;  i++
    }
    print FILENAME ":" FNR ": " out }'

for f in src/*.c src/*/*.c src/*/*.h tests/*.c tests/*.h fuzz/*.c fuzz/*.h; do
  case $f in
    src/sys/common.c|src/sys/wincrt.c|src/sys/win32.c|tests/t_wincrt.c|src/sys/yarg.h) continue ;;
  esac
  hits=$(awk "$strip" "$f" | grep -Ev '^src/sys/common\.h:[0-9]+: #define (free|realloc)\(' \
           | grep -E '(^|[^A-Za-z0-9_])(free|realloc)([^A-Za-z0-9_]|$)')
  if [ -n "$hits" ]; then
    echo "strayfree.sh: free or realloc outside the allocator; use xfree or xrealloc:"
    echo "$hits"
    status=1
  fi
done

grep '^#define free(p) blr_free_is_xfree(p)$' src/sys/common.h > /dev/null \
  && grep '^#define realloc(p, n) blr_realloc_is_xrealloc(p, n)$' src/sys/common.h > /dev/null \
  || { echo "strayfree.sh: src/sys/common.h lost the guard on free and realloc";  status=1; }

for f in src/opus/parse/*.c src/opus/parse/*.h; do
  hits=$(awk "$strip" "$f" \
           | grep -E '(^|[^A-Za-z0-9_])(xmalloc|xcalloc|xrealloc|xfree|slurp|ar_take|rg_take)([^A-Za-z0-9_]|$)')
  if [ -n "$hits" ]; then
    echo "strayfree.sh: the Opus parser uses balrogg's allocator:"
    echo "$hits"
    status=1
  fi
done

[ $status -eq 0 ] && echo "strayfree.sh: no stray free or realloc"
exit $status
