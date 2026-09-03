#!/bin/sh
#  Audit a Windows 95 PE image.
#
#    OBJDUMP=... NM=... contrib/check-win95-pe.sh balrogg.exe config.h [symbols]

set -eu

objdump=${OBJDUMP:-objdump}
nm=${NM:-nm}
exe=${1:-./balrogg.exe}
config=${2:-./config.h}
symbols=${3:-$exe}
tmp=${TMPDIR:-/tmp}/balrogg-pe-$$
trap 'rm -f "$tmp" "$tmp.k32"' EXIT HUP INT TERM

"$objdump" -p "$exe" >"$tmp"

check() {
  if ! grep -Eq "$1" "$tmp"; then
    echo "win95-check: $2" >&2
    exit 1
  fi
}

check 'file format pei-i386' 'not a 32-bit PE image'

entry=`"$objdump" -f "$exe" |
       sed -n 's/^start address 0x0*\([0-9a-fA-F]*\).*/\1/p' |
       tr 'A-F' 'a-f'`
want=`"$nm" "$symbols" |
      sed -n 's/^0*\([0-9a-fA-F]*\) [Tt] _blr_entry$/\1/p' |
      tr 'A-F' 'a-f'`
if test -z "$entry" || test -z "$want"; then
  echo 'win95-check: cannot read entry point' >&2
  exit 1
fi
if test "$entry" != "$want"; then
  echo "win95-check: entry is 0x$entry but _blr_entry is 0x$want" >&2
  echo 'win95-check: unresolved -e symbol' >&2
  exit 1
fi

check 'MajorOSystemVersion[[:space:]]+4' 'OS version is not 4.x'
check 'MinorOSystemVersion[[:space:]]+0' 'OS revision is not 0'
check 'MajorSubsystemVersion[[:space:]]+4' 'subsystem is not 4.x'
check 'MinorSubsystemVersion[[:space:]]+0' 'subsystem revision is not 0'
check 'DllCharacteristics[[:space:]]+00000000' 'modern PE flags are set'

awk '/DLL Name:/{
       if (seen) on=0
       if ($3 == "KERNEL32.dll") { on=1; seen=1 }
     }
     on' "$tmp" >"$tmp.k32"
if grep -Eq 'Member-Name[[:space:]]+.*W$' "$tmp.k32"; then
  echo 'win95-check: Unicode kernel import' >&2
  exit 1
fi
if grep -Eq 'GetFileInformationByHandleEx|GetFileSizeEx|'\
'GetFinalPathNameByHandle|GlobalMemoryStatusEx|IsProcessorFeaturePresent|'\
'SetFilePointerEx|GetModuleHandleEx' "$tmp.k32"; then
  echo 'win95-check: post-Win95 kernel import' >&2
  exit 1
fi

#  Allowed runtime and batch imports.
allowed=' CloseHandle CreateFileA CreateProcessA ExitProcess
GetCommandLineA GetEnvironmentVariableA GetExitCodeProcess
GetFileAttributesA GetFileInformationByHandle GetFileSize GetLastError
GetModuleFileNameA GetProcessHeap GetStdHandle GetSystemInfo
GlobalMemoryStatus HeapAlloc HeapFree HeapReAlloc ReadFile
WaitForMultipleObjects WriteFile '
for sym in `awk '$2 == "<none>" {print $4}' "$tmp.k32"`; do
  found=no
  for known in $allowed; do
    test "$known" = "$sym" && found=yes
  done
  if test "$found" != yes; then
    echo "win95-check: unsupported kernel import $sym" >&2
    exit 1
  fi
done
if grep -Eq '^#define HAVE_SSE2' "$config"; then
  echo 'win95-check: the SSE2 mixer is compiled in' >&2
  exit 1
fi
if test "`grep -ci 'DLL Name:' "$tmp"`" -ne 1 ||
   ! grep -qi 'DLL Name: KERNEL32.dll' "$tmp"; then
  echo 'win95-check: executable imports a DLL other than KERNEL32' >&2
  exit 1
fi

echo 'win95-check: ok'
