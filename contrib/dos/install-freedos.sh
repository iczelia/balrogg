#!/bin/sh
#  Install the FreeDOS boot floppy and the DJGPP DPMI host and sync tool
#  used by the test guest.
#
#    contrib/dos/install-freedos.sh PREFIX

set -e

prog=${0##*/}
fail() { echo "$prog: $*" >&2; exit 1; }

prefix=${1:?usage: install-freedos.sh PREFIX}
case $prefix in ''|/) fail "unsafe prefix" ;; esac

version=1.4
root=$prefix/share/balrogg-dos
test -f "$root/version-$version" && test -f "$root/freedos.img" &&
  test -f "$root/bin/CWSDPMI.EXE" && test -f "$root/bin/SYNC.EXE" && exit 0

for tool in curl install sha256sum unzip; do
  command -v "$tool" > /dev/null || fail "$tool is required"
done

work=`mktemp -d "${TMPDIR:-/tmp}/balrogg-freedos.XXXXXX"`
stage=$prefix.tmp.$$
trap 'rm -rf "$work" "$stage"' EXIT HUP INT TERM
mkdir -p "$work/download" "$work/djgpp" "$stage/share/balrogg-dos/bin"

freedos=FD14-FloppyEdition.zip
freedos_url=https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.4
curl -fL "$freedos_url/$freedos" -o "$work/download/$freedos"
echo "45b1fa7c52dd996c3bfa5e352ffcd410781b952a6ad629f15a4c9ec4bbaefc5a  $work/download/$freedos" |
  sha256sum -c -

packages='83551a1a626de95d07dff15c5e5ab9abec7763d81185440b708f41830b8e781f v2gnu/fil41br3.zip
deacda0488e1cdd7c4a9f32fab45662b34c0ed6b2d7d4d13bc07041b62004a8c v2misc/csdpmi7b.zip'

echo "$packages" | while read sum path; do
  file=${path##*/}
  curl -fL "https://www.delorie.com/pub/djgpp/current/$path" \
    -o "$work/download/$file"
  echo "$sum  $work/download/$file" | sha256sum -c -
  unzip -q -o "$work/download/$file" -d "$work/djgpp"
done

unzip -j -q "$work/download/$freedos" 144m/x86BOOT.img \
  -d "$stage/share/balrogg-dos"
mv "$stage/share/balrogg-dos/x86BOOT.img" \
   "$stage/share/balrogg-dos/freedos.img"

install -m 644 "$work/djgpp/bin/CWSDPMI.EXE" \
  "$stage/share/balrogg-dos/bin/CWSDPMI.EXE"
install -m 644 "$work/djgpp/bin/sync.exe" \
  "$stage/share/balrogg-dos/bin/SYNC.EXE"

: > "$stage/share/balrogg-dos/version-$version"
mkdir -p "$prefix/share"
rm -rf "$root"
mv "$stage/share/balrogg-dos" "$root"
rm -rf "$stage"
trap - EXIT HUP INT TERM
rm -rf "$work"
