# balrogg

balrogg losslessly recompresses Ogg Vorbis and Opus files. Archives are
typically 8-12% smaller than `.ogg` files and 3-8% smaller than `.opus` files.

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

## Caveats

The encoder refuses files it cannot reproduce exactly, including files with
bad checksums, invalid page sequences, or unsupported Vorbis features.
Chained Vorbis files share the adaptive models across links. A link missing its
end-of-stream flag, such as a stream cut short before the next link begins, is
supported when it ends on a complete packet.

Vorbis packets with extra padding or alternative floor subclass choices retain
normal floor and residue compression. Classword corrections and padding are
modeled separately. Shortened packets (packet peeling) use an adaptive byte model.

Opus support is limited to mono or stereo logical streams with channel mapping
family 0. Audio packets and OpusHead are limited to 61,440 bytes;
extended frame headers and padding are supported within that limit. OpusTags
packets may be up to 120 MiB and are processed in bounded batches.
Multichannel Opus files are refused. Refusals
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

```

            parallel (79.3 MB, 1047 files)    newmusic_piano3.ogg (2.5 MB)
   tool  |  e/time |     ratio (size) | mem |  e/time |    ratio (size) | mem
 1.3  -1 |  1.61 s | 90.4% (75,256kB) | 15M |  0.38 s | 83.3% (2,221kB) |  6M
      -2 |  1.78 s | 89.2% (74,259kB) | 16M |  0.69 s | 79.4% (2,118kB) |  7M
      -3 |  1.91 s | 88.9% (74,035kB) | 17M |  0.77 s | 79.2% (2,113kB) |  8M
      -4 |  1.99 s | 88.6% (73,743kB) | 17M |  0.82 s | 78.9% (2,106kB) |  8M
      -5 |  4.19 s | 88.3% (73,506kB) | 17M |  2.67 s | 78.6% (2,095kB) |  9M
      -6 |  6.17 s | 88.2% (73,421kB) | 17M |  4.27 s | 78.3% (2,089kB) | 10M
      -7 |  8.12 s | 88.2% (73,417kB) | 17M |  5.84 s | 78.3% (2,089kB) | 11M
      -8 | 10.06 s | 88.2% (73,411kB) | 17M |  7.50 s | 78.3% (2,089kB) | 11M
      -9 | 14.15 s | 88.2% (73,393kB) | 17M | 10.60 s | 78.3% (2,088kB) | 11M

 1.2  -1 |  1.63 s | 90.4% (75,274kB) | 15M |  0.42 s | 83.3% (2,221kB) |  7M
      -2 |  1.85 s | 89.2% (74,276kB) | 15M |  0.77 s | 79.4% (2,118kB) |  7M
      -3 |  1.97 s | 89.0% (74,052kB) | 17M |  0.82 s | 79.2% (2,113kB) |  8M
      -4 |  2.06 s | 88.6% (73,761kB) | 17M |  0.90 s | 78.9% (2,106kB) |  8M
      -5 |  4.24 s | 88.3% (73,524kB) | 17M |  2.78 s | 78.6% (2,095kB) |  9M
      -6 |  6.31 s | 88.2% (73,438kB) | 17M |  4.55 s | 78.3% (2,089kB) | 10M
      -7 |  8.39 s | 88.2% (73,435kB) | 17M |  6.25 s | 78.3% (2,089kB) | 11M
      -8 | 10.65 s | 88.2% (73,428kB) | 17M |  8.00 s | 78.3% (2,089kB) | 11M
      -9 | 15.11 s | 88.2% (73,428kB) | 17M | 11.73 s | 78.3% (2,089kB) | 11M

 1.1  -1 |  0.93 s | 90.4% (75,270kB) | 11M |  0.45 s | 83.3% (2,221kB) |  6M
      -2 |  1.30 s | 89.2% (74,272kB) | 11M |  0.84 s | 79.4% (2,118kB) |  7M
      -3 |  1.41 s | 88.9% (74,048kB) | 13M |  0.90 s | 79.2% (2,113kB) |  8M
      -4 |  1.64 s | 88.6% (73,757kB) | 13M |  0.96 s | 78.9% (2,106kB) |  8M
      -5 |  4.39 s | 88.3% (73,520kB) | 13M |  3.01 s | 78.6% (2,095kB) | 10M
      -6 |  6.89 s | 88.2% (73,434kB) | 13M |  5.00 s | 78.3% (2,089kB) | 10M
      -7 |  9.44 s | 88.2% (73,431kB) | 13M |  6.84 s | 78.3% (2,089kB) | 10M
      -8 | 11.92 s | 88.2% (73,424kB) | 13M |  8.56 s | 78.3% (2,089kB) | 10M
      -9 | 16.75 s | 88.2% (73,424kB) | 13M | 12.22 s | 78.3% (2,089kB) | 10M

 1.0  -1 |  0.92 s | 90.4% (75,238kB) | 37M |  0.43 s | 83.3% (2,221kB) | 12M
      -2 |  1.25 s | 89.2% (74,241kB) | 37M |  0.86 s | 79.4% (2,118kB) | 12M
      -3 |  1.40 s | 88.9% (74,017kB) | 38M |  0.91 s | 79.2% (2,113kB) | 13M
      -4 |  1.63 s | 88.6% (73,726kB) | 38M |  1.00 s | 78.9% (2,105kB) | 13M
      -5 |  5.68 s | 88.3% (73,489kB) | 46M |  3.13 s | 78.6% (2,095kB) | 46M
      -6 |  9.66 s | 88.2% (73,404kB) | 47M |  5.07 s | 78.3% (2,089kB) | 46M
      -7 | 13.30 s | 88.2% (73,401kB) | 47M |  7.00 s | 78.3% (2,089kB) | 46M
      -8 | 16.91 s | 88.2% (73,394kB) | 47M |  9.02 s | 78.3% (2,089kB) | 46M
      -9 | 24.04 s | 88.2% (73,394kB) | 48M | 13.08 s | 78.3% (2,089kB) | 48M
```

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
