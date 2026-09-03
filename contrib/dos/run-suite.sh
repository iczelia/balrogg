#!/bin/sh
#  Run the C suite in a FreeDOS guest under QEMU.
#
#    contrib/dos/run-suite.sh DOS-BUILD FREEDOS-ROOT [WORKDIR]
#
#  DOS-BUILD holds a --host=i586-pc-msdosdjgpp build with balrogg.exe and
#  tests/t_suite.exe.  FREEDOS-ROOT is what install-freedos.sh produced.
#  Fixtures are copied under the 8.3 names the harness derives on DOS.

set -e

prog=${0##*/}
fail() { echo "$prog: $*" >&2; exit 1; }

dos_build=${1:?usage: run-suite.sh DOS-BUILD FREEDOS-ROOT [WORKDIR]}
freedos_root=${2:?FreeDOS root required}
suite_root=${3:-./dos-suite}

script_dir=`cd "$(dirname "$0")" && pwd`
top_srcdir=`cd "$script_dir/../.." && pwd`
dos_build=`cd "$dos_build" && pwd`
freedos_root=`cd "$freedos_root" && pwd`
mkdir -p "$suite_root"
suite_root=`cd "$suite_root" && pwd`

for tool in mcopy mformat mmd mpartition qemu-system-i386 timeout truncate; do
  command -v "$tool" > /dev/null || fail "$tool is required"
done
for file in balrogg.exe tests/t_suite.exe; do
  test -f "$dos_build/$file" || fail "$dos_build/$file not found"
done
for file in freedos.img bin/CWSDPMI.EXE bin/SYNC.EXE; do
  test -f "$freedos_root/$file" || fail "$freedos_root/$file not found"
done

: "${CC_FOR_DOS:=i586-pc-msdosdjgpp-gcc}"
command -v "$CC_FOR_DOS" > /dev/null || fail "$CC_FOR_DOS is required"

case ${BLR_TEST_LEVEL:-quick} in
  quick|full|torture) test_level=${BLR_TEST_LEVEL:-quick} ;;
  *) fail "invalid BLR_TEST_LEVEL" ;;
esac
boot=$suite_root/boot.img
disk=$suite_root/tests.img
mtools=$suite_root/mtools.conf
rm -f "$boot" "$disk" "$suite_root/result.log" "$suite_root/status.txt"
cp "$freedos_root/freedos.img" "$boot"
truncate -s 528482304 "$disk"

printf 'drive d: file="%s" partition=1\n' "$disk" > "$mtools"
MTOOLSRC=$mtools
MTOOLS_NO_VFAT=1
export MTOOLSRC MTOOLS_NO_VFAT
mpartition -I d:
mpartition -c -s 63 -h 16 -t 1023 d:
mformat -v BLRTEST d:
mmd d:/BIN d:/BLD d:/SRC d:/SRC/DATA d:/SRC/REGRESS d:/TMP

mcopy "$freedos_root/bin/CWSDPMI.EXE" d:/BIN/CWSDPMI.EXE
mcopy "$freedos_root/bin/SYNC.EXE" d:/BIN/SYNC.EXE
"$CC_FOR_DOS" -Os -march=i386 -mtune=i386 \
  -o "$suite_root/EXIT.EXE" "$script_dir/qemu-exit.c"
mcopy "$suite_root/EXIT.EXE" d:/BIN/EXIT.EXE

mcopy "$dos_build/balrogg.exe" d:/BLD/BALROGG.EXE
mcopy "$dos_build/tests/t_suite.exe" d:/BLD/TSUITE.EXE

#  Match xt_dos_name's 8.3 conversion.
dos_name() {
  stem=`printf '%s' "${1%.*}" | tr -cd 'A-Za-z0-9'`
  ext=`printf '%s' "${1##*.}" | cut -c1-3`
  if test "${#stem}" -gt 8; then
    stem=`printf '%s' "$stem" | cut -c1-5``printf '%s' "$stem" | tail -c 3`
  fi
  printf '%s.%s' "$stem" "$ext" | tr 'a-z' 'A-Z'
}

for dir in data regress; do
  up=`printf '%s' "$dir" | tr 'a-z' 'A-Z'`
  for f in "$top_srcdir"/tests/$dir/*; do
    test -f "$f" || continue
    mcopy "$f" "d:/SRC/$up/`dos_name "${f##*/}"`"
  done
done

sed "s/@LEVEL@/$test_level/g" "$script_dir/fdauto.bat" \
  > "$suite_root/FDAUTO.BAT"
mcopy -o -i "$boot" "$suite_root/FDAUTO.BAT" ::FDAUTO.BAT
mcopy -o -i "$boot" "$script_dir/fdconfig.sys" ::FDCONFIG.SYS

if test -r /dev/kvm && test -w /dev/kvm; then
  accel=kvm
  cpu=host
else
  accel=tcg
  cpu=max
fi
timeout_seconds=${BLR_DOS_TIMEOUT_SECONDS:-3600}
case $timeout_seconds in ''|*[!0-9]*) fail "invalid DOS timeout" ;; esac

echo "$prog: booting FreeDOS with $accel"
qemu_status=0
timeout "$timeout_seconds" qemu-system-i386 \
  -machine pc,accel="$accel" -cpu "$cpu" -m 256 \
  -display none -monitor none -serial none -nic none -no-reboot \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
  -drive file="$boot",format=raw,if=floppy,readonly=on \
  -drive file="$disk",format=raw,if=ide,index=0,cache=writeback \
  -boot order=a,strict=on || qemu_status=$?

mcopy d:/RESULT.LOG "$suite_root/result.log" 2> /dev/null || true
mcopy d:/STATUS.TXT "$suite_root/status.txt" 2> /dev/null || true
test -f "$suite_root/result.log" && cat "$suite_root/result.log"

test "$qemu_status" -eq 1 || fail "QEMU exited with status $qemu_status"
test -f "$suite_root/status.txt" || fail "guest status missing"
status=`tr -d '\r\n' < "$suite_root/status.txt"`
case $status in 0) ;; 1) fail "the FreeDOS suite failed" ;;
  *) fail "invalid guest status $status" ;;
esac

echo "$prog: FreeDOS suite passed"
