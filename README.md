# balrogg

balrogg losslessly recompresses Ogg Vorbis and Opus files. Compressed files
are typically 8-12% smaller than corresponding `.ogg` files and 3-8% smaller
than `.opus` files.

balrogg is licensed under GNU GPL version 3. See [COPYING](COPYING). Report
issues to Kamila Szewczyk <k@iczelia.net>. The project is hosted at
<https://github.com/iczelia/balrogg>.

## Quick start

```sh
balrogg e music.ogg music.blr
balrogg d music.blr music.ogg
balrogg -b e *.ogg *.opus
```

`e` compresses and `d` expands. balrogg detects the codec from the input. In
batch mode, `-b` processes every remaining path using the available cores and
memory. Encoding appends `.blr` and decoding removes it. Larger inputs run
first.

## Installation

Use your package manager or download a binary from GitHub Releases. To build a
release tarball, run

```sh
./configure
make
sudo make install
```

The project uses C99 and depends only on the C and math libraries. Its Opus
parser is derived from libopus and lives under `src/opus`.

| Configure option | Effect |
| --- | --- |
| `--enable-sanitizers` | Enable ASan and UBSan for tests |

Run `./bootstrap` first when building from a Git checkout. It requires autoconf
and automake.

## Effort

`-1` through `-9` select the effort. The default is `-9`. Up to `-4`, each level
adds a residue-model stage and affects decode speed. Higher levels only search
for better encoding parameters. The archive stores them, so levels `-4` through
`-9` decode at the same speed.

Results for a five-megabyte music file follow.

| Level | Vorbis | Opus |
| --- | --- | --- |
| `-1` | 8.3 percent smaller at 6.4 MB/s | 4.0 percent smaller at 4.2 MB/s |
| `-9` | 9.8 percent smaller at 2.9 MB/s | 4.8 percent smaller at 2.4 MB/s |

## Caveats

The encoder refuses files it cannot reproduce exactly. This includes bad page
checksums, invalid page sequences, unsupported Vorbis features, and files
without a final end-of-stream page.

Opus support is limited to one mono or stereo logical stream with channel
mapping family 0. Chained and multichannel Opus files are refused. Refusals
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

Each codec uses one thread per file and keeps the input and archive in memory.
A five-megabyte Vorbis file uses about 40 MB at `-4` and 70 MB at `-9`. Model
setup makes small files slower per byte.

Supported hosts apply a 2 GiB process memory cap to reject unreasonable
allocations. Set `BLR_MEMCAP` to another size in MiB, or to `0` to disable the
cap.

Archives are portable across hosts. Coding uses integer arithmetic, the scalar
and SSE2 mixers agree exactly, and the Opus parser does no signal processing.
The suite tests archives on 32-bit and 64-bit hosts.

Windows builds use MinGW as follows:

```sh
./configure --host=x86_64-w64-mingw32 CC=x86_64-w64-mingw32-gcc LDFLAGS=-static
make
```

## Testing

`make check` runs unit, codec-layer, whole-file, command-line, and regression
tests.

`contrib/mkdata.sh` generates a larger ffmpeg corpus. Test it with an absolute
path using `make check-full BLR_TEST_CORPUS=$PWD/testdata`. The `fuzz` directory
contains libFuzzer harnesses and a mutator for Ogg framing and corruption.
