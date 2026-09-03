##  Copyright (C) 2026 Kamila Szewczyk
##
##  This program is free software; you can redistribute it and/or modify
##  it under the terms of the GNU General Public License as published by
##  the Free Software Foundation, version 3.
##
##  This program is distributed in the hope that it will be useful,
##  but WITHOUT ANY WARRANTY; without even the implied warranty of
##  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
##  GNU General Public License for more details.
##
##  You should have received a copy of the GNU General Public License
##  along with this program. If not, see <http://www.gnu.org/licenses/>.

ACLOCAL_AMFLAGS = -I m4

AM_CFLAGS = -std=c99 $(BLR_WARN_CFLAGS) $(BLR_HARDEN_CFLAGS) $(BLR_SAN_CFLAGS) \
            $(BLR_WIN_CFLAGS) $(BLR_DOS_CFLAGS)
AM_LDFLAGS = $(BLR_SAN_LDFLAGS) $(BLR_WIN_LDFLAGS) $(BLR_DOS_LDFLAGS)
AM_CPPFLAGS = $(BLR_WIN_CPPFLAGS) $(BLR_DOS_CPPFLAGS) -I$(top_builddir) \
              -I$(top_srcdir)/src

bin_PROGRAMS = balrogg
dist_man1_MANS = balrogg.1
noinst_HEADERS = \
  src/common.h src/rc.h src/model.h src/cm.h src/archive.h src/ogg.h \
  src/vorbis.h src/codec.h src/opusmode.h src/opusent.h src/prof.h \
  src/cpu.h src/yarg.h \
  src/opus/opusdec.h src/opus/celt.h src/opus/silk.h src/opus/arch.h \
  src/opus/opus_types.h src/opus/opus_defines.h src/opus/os_support.h \
  src/opus/ecintrin.h src/opus/entcode.h src/opus/entdec.h \
  src/opus/entenc.h src/opus/mfrngcod.h

##  Library sources shared by the binary and tests.
noinst_LIBRARIES = libbalrogg_core.a

libbalrogg_core_a_SOURCES = \
  src/common.c src/rc.c src/archive.c src/model.c src/ogg.c src/vorbis.c \
  src/codec.c src/cm.c src/cmmix.c src/cpu.c

balrogg_SOURCES = src/main.c
balrogg_CFLAGS = $(AM_CFLAGS)
balrogg_LDFLAGS = $(AM_LDFLAGS)
balrogg_LDADD = libbalrogg_core.a

if BLR_SSE2
noinst_LIBRARIES += libbalrogg_sse2.a
libbalrogg_sse2_a_SOURCES  = src/cmmix.c
libbalrogg_sse2_a_CPPFLAGS = $(AM_CPPFLAGS) -DBLR_CM_SSE2
libbalrogg_sse2_a_CFLAGS   = $(AM_CFLAGS) $(BLR_SSE2_CFLAGS)
balrogg_LDADD += libbalrogg_sse2.a
endif

##  Windows 95: a freestanding C runtime on KERNEL32 replaces MSVCRT.
if BLR_WIN_LEGACY
balrogg_SOURCES += src/win95crt.c
balrogg_CFLAGS += -fno-builtin -fno-tree-loop-distribute-patterns
balrogg_LDFLAGS += -nostartfiles -nodefaultlibs -Wl,-e,_blr_entry \
                   -static -static-libgcc -Wl,--subsystem,console
balrogg_LDADD += -lgcc -lkernel32
endif

##  Ogg Opus model and bundled parser.
BLR_OPUS_SOURCES = \
  src/opus/opus_dec.c src/opus/entdec.c src/opus/entcode.c \
  src/opus/entenc.c \
  src/opus/celt_dec.c src/opus/celt_bands.c src/opus/celt_rate.c \
  src/opus/celt_energy.c src/opus/celt_cwrs.c src/opus/celt_laplace.c \
  src/opus/celt_modes.c \
  src/opus/silk_dec.c src/opus/silk_indices.c src/opus/silk_pulses.c \
  src/opus/silk_shell.c src/opus/silk_signs.c src/opus/silk_nlsf_unpack.c \
  src/opus/silk_stereo.c \
  src/opus/silk_tables_gain.c src/opus/silk_tables_ltp.c \
  src/opus/silk_tables_nlsf_nbmb.c src/opus/silk_tables_nlsf_wb.c \
  src/opus/silk_tables_other.c src/opus/silk_tables_pitch.c \
  src/opus/silk_tables_pulses.c

BLR_OPUS_CPPFLAGS = -I$(top_srcdir)/src/opus

noinst_LIBRARIES += libbalrogg_opus.a

libbalrogg_opus_a_SOURCES  = src/opusmode.c $(BLR_OPUS_SOURCES)
libbalrogg_opus_a_CPPFLAGS = $(AM_CPPFLAGS) $(BLR_OPUS_CPPFLAGS)

balrogg_LDADD += libbalrogg_opus.a

##  Optional profiling build with per-component bit accounting and dumps.
EXTRA_PROGRAMS = balrogg_prof
balrogg_prof_SOURCES  = src/main.c src/prof.c $(libbalrogg_core_a_SOURCES) \
                        src/opusmode.c $(BLR_OPUS_SOURCES)
balrogg_prof_CPPFLAGS = $(AM_CPPFLAGS) -DBLR_PROFILE $(BLR_OPUS_CPPFLAGS)
balrogg_prof_LDADD    = -lm
if BLR_SSE2
noinst_LIBRARIES += libbalrogg_prof_sse2.a
libbalrogg_prof_sse2_a_SOURCES  = src/cmmix.c
libbalrogg_prof_sse2_a_CPPFLAGS = $(AM_CPPFLAGS) -DBLR_PROFILE -DBLR_CM_SSE2
libbalrogg_prof_sse2_a_CFLAGS   = $(AM_CFLAGS) $(BLR_SSE2_CFLAGS)
balrogg_prof_LDADD += libbalrogg_prof_sse2.a
endif

##  Test suite linked to the library and built binary.
check_PROGRAMS = tests/t_suite
TESTS = tests/t_suite

tests_t_suite_SOURCES = tests/t_suite.c tests/t_harness.c tests/t_unit.c \
                        tests/t_layers.c tests/t_files.c tests/t_cli.c
tests_t_suite_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/tests
tests_t_suite_LDADD = libbalrogg_core.a libbalrogg_opus.a
if BLR_SSE2
tests_t_suite_LDADD += libbalrogg_sse2.a
endif

test-programs: tests/t_suite$(EXEEXT)

##  Override with `make check BLR_TEST_TIMEOUT_SECONDS=60`.
LOG_COMPILER = $(BLR_TIMEOUT_PROG) $(BLR_TEST_TIMEOUT_SECONDS)

##  Convert MSYS2 paths for the native test binary.
AM_TESTS_ENVIRONMENT = \
  BLR=`cygpath -m '$(BLR_TEST_BINARY)' 2>/dev/null || echo '$(BLR_TEST_BINARY)'`; \
                                                      export BLR; \
  BLR_TEST_DATA=`cygpath -m '$(abs_top_srcdir)/tests/data' 2>/dev/null \
                 || echo '$(abs_top_srcdir)/tests/data'`; export BLR_TEST_DATA; \
  BLR_TEST_REGRESS=`cygpath -m '$(abs_top_srcdir)/tests/regress' 2>/dev/null \
                 || echo '$(abs_top_srcdir)/tests/regress'`; export BLR_TEST_REGRESS; \
  BLR_TEST_LEVEL="$${BLR_TEST_LEVEL-quick}";          export BLR_TEST_LEVEL; \
  BLR_TEST_CORPUS="$${BLR_TEST_CORPUS-}";             export BLR_TEST_CORPUS; \
  BLR_TEST_TRACE="$${BLR_TEST_TRACE-0}";              export BLR_TEST_TRACE;

BLR_TEST_BINARY = $(abs_top_builddir)/balrogg$(EXEEXT)

EXTRA_DIST = \
  bootstrap README.md \
  $(BLR_OPUS_SOURCES) src/opus/README src/opus/COPYING \
  tests/t_harness.h tests/data tests/regress \
  fuzz/harness.h fuzz/fuzz_dec.c fuzz/fuzz_enc.c \
  fuzz/fuzz_ogg.c fuzz/fuzz_opus.c fuzz/build.sh fuzz/run.sh \
  fuzz/triage.sh fuzz/regress.sh fuzz/mkseeds.py fuzz/mutate.py \
  contrib/mkdata.sh contrib/normalize-ogg.py contrib/corpus-rt.sh \
  contrib/gitlog-to-changelog contrib/check-win95-pe.sh \
  contrib/dos/fdauto.bat contrib/dos/fdconfig.sys \
  contrib/dos/install-freedos.sh contrib/dos/qemu-exit.c \
  contrib/dos/run-suite.sh

VENDORED = src/yarg.h src/opus

.PHONY: check-full check-torture check-simd win95-check print-distfiles \
        test-programs

##  Full tests also scan BLR_TEST_CORPUS.
check-full:
	$(MAKE) check BLR_TEST_LEVEL=full

check-torture:
	$(MAKE) check BLR_TEST_LEVEL=torture

##  Run the suite once per mixer kernel this host can dispatch.
check-simd: balrogg$(EXEEXT)
	@tiers=scalar; \
	if ./balrogg$(EXEEXT) --version | grep -q 'sse2 dispatched'; then \
	  tiers="$$tiers sse2"; \
	fi; \
	for t in $$tiers; do \
	  echo "== mixer kernel $$t"; \
	  BLR_SIMD=$$t $(MAKE) check || exit 1; \
	done

##  Audit a --with-windows-target=win95 image's PE headers and imports.
win95-check: balrogg$(EXEEXT)
	OBJDUMP=$(OBJDUMP) NM=$(NM) $(SHELL) \
	  $(top_srcdir)/contrib/check-win95-pe.sh ./balrogg$(EXEEXT) ./config.h

##  Print the files included in the distribution tarball.
print-distfiles:
	@for f in $(DISTFILES) $(DIST_COMMON) $(am__DIST_COMMON) \
	          $(ACLOCAL_M4); do \
	  case $$f in \
	    $(srcdir)/*)     echo "$${f#$(srcdir)/}" ;; \
	    $(top_srcdir)/*) echo "$${f#$(top_srcdir)/}" ;; \
	    *)               echo "$$f" ;; \
	  esac; \
	done
