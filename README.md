# balrogg

balrogg losslessly recompresses Ogg Vorbis and Opus files, and MP3 files.
Archives are typically 8-12% smaller than `.ogg` files, 3-8% smaller than
`.opus` files and 15-24% smaller than `.mp3` files.

Releases are not backwards or forwards compatible until v2.0 is reached.

balrogg is licensed under GNU GPL version 3. See [COPYING](COPYING). Report
issues to Kamila Szewczyk <k@iczelia.net>. The project is hosted at
<https://github.com/iczelia/balrogg>.

Discussion:
- [HN - Balrogg: Demonically compacting (up to 15%) lossless Vorbis/Opus recompressor](https://news.ycombinator.com/item?id=49549778)
- [FileForums - balrogg: lossless Vorbis/Opus recompressor](https://fileforums.com/showthread.php?p=510887)

## Quick start

```sh
balrogg e music.ogg music.blr
balrogg d music.blr music.ogg
balrogg -b e *.ogg *.opus
balrogg --progress -9 e music.ogg music.blr
```

`e` compresses and `d` expands. balrogg detects the codec. With `-b`, every
remaining path is processed using the available cores and memory. Encoding
appends `.blr`; decoding removes it. Larger inputs run first. Each archive
stores the length and CRC-32C of its original file, and decoding checks both.

`-p` or `--progress` displays a progress bar on stderr for encoding and
decoding. Use `--progress-lines` for logs. Batch progress uses separate lines labeled with
the input filename.

## Installation

Use your package manager or download a binary from GitHub Releases. To build a
release tarball, run

```sh
./configure
make
sudo make install
```

The project uses C99 and only the C standard library. The Opus parser in
`src/opus/parse` is derived from libopus.

| Configure option | Effect |
| --- | --- |
| `--enable-sanitizers` | Enable ASan and UBSan for tests |
| `--enable-region-check` | Check the allocator's blocks: canaries, filled and delayed free blocks, periodic walks |
| `--disable-simd` | Build the portable mixer and CRC-32C only |
| `--with-windows-target=win95` | Target Windows 95 on an i486 (MinGW, 32-bit) |

Run `./bootstrap` first when building from a Git checkout. It requires autoconf
and automake.

## Effort

The flags `-1` through `-20` select effort.  The default effort level `-8`.

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

Archives are portable across hosts. `contrib/cross.sh` builds cross targets,
runs their test suites under qemu or wine and compares their archives with the
native build's. Windows builds use MinGW as follows:

```sh
./configure --host=x86_64-w64-mingw32 CC=x86_64-w64-mingw32-gcc LDFLAGS=-static
make
```

A windows 9x build (as opposed to the Windows NT build) targets i486.

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
