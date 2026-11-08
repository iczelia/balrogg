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

set -u
src=$(cd "$(dirname "$0")/.." && pwd)
out=${BLR_CROSS_DIR:-$PWD/cross}
levels=${BLR_CROSS_LEVELS:-"1 8"}
[ $# -gt 0 ] || set -- i686-linux-musl aarch64-linux-musl s390x-linux-musl \
  powerpc64-linux-musl x86_64-w64-mingw32 i686-w64-mingw32 win95
mkdir -p "$out"
[ -x "$src/configure" ] || (cd "$src" && ./bootstrap) || exit 1

emulator() {
  case $1 in
    *-mingw32|win95) command -v wine ;;
    i?86-*linux*|x86_64-*linux*) echo ;;
    *) a=${1%%-*}
       case $a in powerpc64) a=ppc64 ;; powerpc64le) a=ppc64le ;; powerpc) a=ppc ;; esac
       command -v "qemu-$a-static" || command -v "qemu-$a" ;;
  esac
}

archives() {
  rm -rf "$1";  mkdir -p "$1"
  for l in $levels; do
    for f in "$src"/tests/data/*; do
      n=$(basename "$f")
      $2 -"$l" e "$f" "$1/$l-$n.blr" > /dev/null 2>&1 || echo "encode -$l $n failed" >&2
    done
  done
  (cd "$1" && cksum ./*.blr) > "$1.sum"
}

build() {
  d=$out/$1;  rm -rf "$d";  mkdir -p "$d"
  if [ -n "$2" ]; then
    case $2 in *mingw32) ldf= ;; *) ldf="-static -no-pie" ;; esac
    case $1 in win95) set -- "$1" "$2" --with-windows-target=win95 ;; *) set -- "$1" "$2" ;; esac
    (cd "$d" && "$src/configure" --host="$2" ${3+"$3"} LDFLAGS="${LDFLAGS-$ldf}") > "$d/configure.log" 2>&1
  else
    (cd "$d" && "$src/configure") > "$d/configure.log" 2>&1
  fi || { echo "$1: configure failed, see $d/config.log";  return 1; }
  make -C "$d" -j"${JOBS:-2}" all > "$d/build.log" 2>&1 \
    && make -C "$d" -j"${JOBS:-2}" check TESTS= >> "$d/build.log" 2>&1 \
    || { echo "$1: build failed, see $d/build.log";  return 1; }
  case $1 in
    win95) make -C "$d" win95-check >> "$d/build.log" 2>&1 || { echo "$1: win95-check failed";  return 1; } ;;
    *mingw32) make -C "$d" windows-check >> "$d/build.log" 2>&1 || { echo "$1: windows-check failed";  return 1; } ;;
  esac
  echo "$1: built, $(grep -c 'warning:' "$d/build.log") warnings"
}

build native "" || exit 1
archives "$out/native/arc" "$out/native/balrogg"
status=0
for t in "$@"; do
  case $t in win95) h=i686-w64-mingw32 ;; *) h=$t ;; esac
  build "$t" "$h" || { status=1;  continue; }
  d=$out/$t;  run=$(emulator "$t")
  case $t in
    *mingw32|win95)
      [ -n "$run" ] || { echo "$t: no wine, tests not run";  continue; }
      w() { printf 'Z:%s' "$1"; }
      BLR=$(w "$d/balrogg.exe") BLR_TEST_DATA=$(w "$src/tests/data") \
        BLR_TEST_REGRESS=$(w "$src/tests/regress") BLR_TEST_LEVEL=quick \
        "$run" "$d/tests/t_suite.exe" > "$d/check.log" 2>&1
      rc=$?;  bin="$run $d/balrogg.exe" ;;
    *)
      case $t in i?86-*linux*|x86_64-*linux*) ;; *)
        [ -n "$run" ] || { echo "$t: no qemu, tests not run";  continue; } ;; esac
      printf '#!/bin/sh\nexec %s "%s/balrogg" "$@"\n' "$run" "$d" > "$d/run";  chmod +x "$d/run"
      make -C "$d" check LOG_COMPILER="$run" BLR_TEST_BINARY="$d/run" > "$d/check.log" 2>&1
      rc=$?;  bin="$d/run" ;;
  esac
  archives "$d/arc" "$bin"
  if [ $rc -ne 0 ]; then echo "$t: tests failed, see $d/check.log";  status=1;
  elif ! cmp -s "$out/native/arc.sum" "$d/arc.sum"; then echo "$t: archives differ from native";  status=1;
  else echo "$t: tests passed, archives identical to native"; fi
done
exit $status
