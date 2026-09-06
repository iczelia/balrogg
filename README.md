# balrogg

balrogg losslessly recompresses Ogg Vorbis and Opus files. Archives are
typically 8-12% smaller than `.ogg` files and 3-8% smaller than `.opus` files.

Releases are not backwards or forwards compatible until v2.0 is reached.

balrogg is licensed under GNU GPL version 3. See [COPYING](COPYING). Report
issues to Kamila Szewczyk <k@iczelia.net>. The project is hosted at
<https://github.com/iczelia/balrogg>.

## Quick start

```sh
balrogg e music.ogg music.blr
balrogg d music.blr music.ogg
balrogg -b e *.ogg *.opus
balrogg --progress -9 e music.ogg music.blr
```

`e` compresses and `d` expands. balrogg detects the codec. With `-b`, every
remaining path is processed using the available cores and memory. Encoding
appends `.blr`; decoding removes it. Larger inputs run first.

`-p` or `--progress` displays a progress bar on stderr for encoding and
decoding, with each Vorbis tuning trial identified separately. Use
`--progress-lines` for logs. Batch progress uses separate lines labeled with
the input filename.

## Installation

Use your package manager or download a binary from GitHub Releases. To build a
release tarball, run

```sh
./configure
make
sudo make install
```

The project uses C99 and only the C and math libraries. The Opus parser under
`src/opus` is derived from libopus.

| Configure option | Effect |
| --- | --- |
| `--enable-sanitizers` | Enable ASan and UBSan for tests |
| `--disable-simd` | Build the portable mixer only |
| `--with-windows-target=win95` | Target Windows 95 on an i486 (MinGW, 32-bit) |

Run `./bootstrap` first when building from a Git checkout. It requires autoconf
and automake.

## Effort

`-1` through `-9` select effort; the default is `-9`. Through `-4`, each level
adds a residue-model stage and affects decoding. Higher levels only expand the
parameter search, so `-4` through `-9` decode at the same speed.
Vorbis tuning evaluates the complete file at each selected setting. The best
candidate is retained in the destination file, with at most the best and current
candidate present during a trial.

Results for a five-megabyte music file follow.

| Level | Vorbis | Opus |
| --- | --- | --- |
| `-1` | 8.3 percent smaller at 6.4 MB/s | 4.0 percent smaller at 4.2 MB/s |
| `-9` | 9.8 percent smaller at 2.9 MB/s | 4.8 percent smaller at 2.4 MB/s |

## Caveats

The encoder refuses files it cannot reproduce exactly, including files with
bad checksums, invalid page sequences, or unsupported Vorbis features.
Vorbis files missing the final end-of-stream flag are supported when they end
on a complete packet.

Vorbis packets with extra padding or alternative floor subclass choices retain
normal floor and residue compression. Classword corrections and padding are
modeled separately. Shortened packets (packet peeling) use an adaptive byte model.

Opus support is limited to one mono or stereo logical stream with channel
mapping family 0. Packets, including OpusHead and OpusTags, are limited to 61,440
bytes; extended frame headers and padding are supported within that limit.
Chained and multichannel Opus files are refused. Refusals
produce a diagnostic and exit status 1.

## Exit status

| Code | Meaning |
| --- | --- |
| 0 | Success |
| 1 | Malformed, unsupported, or unrecognized input |
| 2 | Usage error |
| 3 | File access error |
| 4 | Internal error |

Batch mode returns the highest nonzero status reported by any file.

## Performance and portability

Each file uses one thread and requires seekable input and output files.
Supported hosts apply a 2 GiB process memory cap to reject unreasonable
allocations. Set `BLR_MEMCAP` to another size in MiB, or to `0` to disable the
cap.

Archives are portable across hosts. Windows builds use MinGW as follows:

```sh
./configure --host=x86_64-w64-mingw32 CC=x86_64-w64-mingw32-gcc LDFLAGS=-static
make
```

A Windows 95 build uses a small KERNEL32 runtime and targets i486.

```sh
./configure --host=i686-w64-mingw32 --with-windows-target=win95 \
            CC=i686-w64-mingw32-gcc LDFLAGS=-static
make && make win95-check
```

`win95-check` verifies the loader baseline, PE flags, and KERNEL32 imports.
An MS-DOS build uses DJGPP and needs an i386 with a DPMI host such as CWSDPMI.

```sh
./configure --host=i586-pc-msdosdjgpp CC=i586-pc-msdosdjgpp-gcc
make
```

DOS batch mode replaces the extension and runs serially. `song.ogg` becomes
`song.blr`, then expands to `song.ogg` or `song.opu` for Opus.
