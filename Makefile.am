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
            $(BLR_WIN_CFLAGS) $(BLR_DOS_CFLAGS) $(BLR_SIZE_CFLAGS)
AM_LDFLAGS = $(BLR_SAN_LDFLAGS) $(BLR_WIN_LDFLAGS) $(BLR_DOS_LDFLAGS) $(BLR_SIZE_LDFLAGS)
AM_CPPFLAGS = $(BLR_WIN_CPPFLAGS) $(BLR_DOS_CPPFLAGS) -I$(top_builddir) \
              -I$(top_srcdir)/src

bin_PROGRAMS = balrogg
man1_MANS = balrogg.1

##  Sources by directory; the parts are included by the file before them.
BLR_SYS = src/sys/common.c src/sys/map.c src/sys/file.c src/sys/cpu.c src/sys/crc.c
BLR_CODER = src/coder/rc.c src/coder/model.c src/coder/cm.c src/coder/cmmix.c src/coder/acm.c
BLR_FMT = src/fmt/arc.c src/fmt/cmt.c src/fmt/ogg.c
BLR_VORBIS = src/vorbis/vb.c src/vorbis/pack.c src/vorbis/acm.c src/vorbis/zc.c src/vorbis/dict.c
BLR_VORBIS_PARTS = src/vorbis/setup.c src/vorbis/audio.c src/vorbis/cfg.c
BLR_OPUS = src/opus/opus.c src/opus/acm.c
BLR_OPUS_PARTS = src/opus/rc.c src/opus/cfg.c src/opus/ctx.c src/opus/pvq.c src/opus/side.c src/opus/energy.c \
  src/opus/site.c src/opus/fast.c src/opus/op.c src/opus/frame.c src/opus/stream.c src/opus/pack.c
BLR_MP3 = src/mp3/mp3.c src/mp3/tab.c src/mp3/z.c
BLR_MP3_PARTS = src/mp3/cfg.c src/mp3/zapi.c src/mp3/zcodec.c src/mp3/zcoef.c src/mp3/zframe.c src/mp3/zhdr.c \
  src/mp3/zhuff.c src/mp3/zhufftab.c src/mp3/zlsf.c src/mp3/zmframe.c src/mp3/zpart23.c src/mp3/zrc.c \
  src/mp3/zread.c src/mp3/zscf.c src/mp3/zside.c src/mp3/zstdtab.c src/mp3/ztab.c src/mp3/zwrite.c
BLR_OPUS_PARSE = \
  src/opus/parse/opus_dec.c src/opus/parse/entdec.c src/opus/parse/entcode.c src/opus/parse/entenc.c \
  src/opus/parse/celt_dec.c src/opus/parse/celt_bands.c src/opus/parse/celt_rate.c src/opus/parse/celt_energy.c \
  src/opus/parse/celt_cwrs.c src/opus/parse/celt_laplace.c src/opus/parse/celt_modes.c \
  src/opus/parse/silk_dec.c src/opus/parse/silk_indices.c src/opus/parse/silk_pulses.c src/opus/parse/silk_shell.c \
  src/opus/parse/silk_signs.c src/opus/parse/silk_nlsf_unpack.c src/opus/parse/silk_stereo.c \
  src/opus/parse/silk_tables_gain.c src/opus/parse/silk_tables_ltp.c src/opus/parse/silk_tables_nlsf_nbmb.c \
  src/opus/parse/silk_tables_nlsf_wb.c src/opus/parse/silk_tables_other.c src/opus/parse/silk_tables_pitch.c \
  src/opus/parse/silk_tables_pulses.c
BLR_MOD = src/mod/mod.c src/mod/lay.c src/mod/code.c src/mod/patfast.c \
  src/mod/patcm.c src/mod/smpfast.c src/mod/smpls.c \
  src/mod/lstm.c src/mod/lstmw.c
BLR_IT_PARTS = src/it/st.c src/it/pt.c src/it/xm.c
BLR_IT = src/it/it.c src/it/lay.c src/it/code.c src/it/s16.c src/it/itz.c
BLR_SOURCES = $(BLR_SYS) $(BLR_CODER) $(BLR_FMT) $(BLR_VORBIS) $(BLR_OPUS) $(BLR_MP3) $(BLR_MOD) $(BLR_IT) $(BLR_OPUS_PARSE)
if BLR_IS_WINDOWS
BLR_SOURCES += src/sys/win32.c
endif

noinst_HEADERS = \
  src/sys/common.h src/sys/map.h src/sys/file.h src/sys/cpu.h src/sys/crc.h src/sys/crchw.h src/sys/prof.h src/sys/win32.h \
  src/sys/yarg.h \
  src/coder/rc.h src/coder/model.h src/coder/cm.h src/coder/cmmix.h src/coder/acm.h src/coder/squash.h \
  src/fmt/arc.h src/fmt/cmt.h src/fmt/ogg.h \
  src/vorbis/vorbis.h src/vorbis/vb.h src/vorbis/res.h src/vorbis/acm.h src/vorbis/zc.h src/vorbis/dict.h \
  src/opus/opus.h src/opus/acm.h src/opus/cwrs.h src/opus/ent.h \
  src/mp3/mp3.h src/mp3/z.h src/mp3/zbase.h src/mp3/zc.h src/mp3/zcoef.h src/mp3/zframe.h src/mp3/zhdr.h \
  src/mp3/zlayer.h src/mp3/zmframe.h src/mp3/zmodel.h src/mp3/znames.h src/mp3/zrc.h src/mp3/ztab.h \
  src/mod/mod.h src/mod/mi.h src/mod/lstm.h src/it/it.h src/it/ii.h src/it/itz.h \
  src/opus/parse/opusdec.h src/opus/parse/celt.h src/opus/parse/silk.h src/opus/parse/arch.h \
  src/opus/parse/opus_types.h src/opus/parse/opus_defines.h src/opus/parse/os_support.h \
  src/opus/parse/ecintrin.h src/opus/parse/entcode.h src/opus/parse/entdec.h \
  src/opus/parse/entenc.h src/opus/parse/mfrngcod.h

##  The program's sources, shared by the binary and the tests, and the kernels built for instruction sets beyond the
##  baseline, which run when the CPU has them.
noinst_LIBRARIES = libbalrogg.a
libbalrogg_a_SOURCES = $(BLR_SOURCES)
BLR_LIBS = libbalrogg.a

if BLR_SSE2
noinst_LIBRARIES += libbalrogg_sse2.a
libbalrogg_sse2_a_SOURCES  = src/coder/cmmix.c
libbalrogg_sse2_a_CPPFLAGS = $(AM_CPPFLAGS) -DBLR_CM_SSE2
libbalrogg_sse2_a_CFLAGS   = $(AM_CFLAGS) $(BLR_SSE2_CFLAGS)
BLR_LIBS += libbalrogg_sse2.a
endif
if BLR_AVX2
noinst_LIBRARIES += libbalrogg_avx2.a
libbalrogg_avx2_a_SOURCES  = src/coder/cmmix.c src/coder/acmmix.c \
                             src/mod/smpfast.c src/mod/smpls.c src/it/s16.c
libbalrogg_avx2_a_CPPFLAGS = $(AM_CPPFLAGS) -DBLR_CM_AVX2
libbalrogg_avx2_a_CFLAGS   = $(AM_CFLAGS) $(BLR_AVX2_CFLAGS)
BLR_LIBS += libbalrogg_avx2.a
endif
if BLR_SSE42
noinst_LIBRARIES += libbalrogg_sse42.a
libbalrogg_sse42_a_SOURCES = src/sys/crcx86.c
libbalrogg_sse42_a_CFLAGS  = $(AM_CFLAGS) $(BLR_SSE42_CFLAGS)
BLR_LIBS += libbalrogg_sse42.a
endif
if BLR_CLMUL
noinst_LIBRARIES += libbalrogg_clmul.a
libbalrogg_clmul_a_SOURCES = src/fmt/oggclmul.c
libbalrogg_clmul_a_CFLAGS  = $(AM_CFLAGS) $(BLR_CLMUL_CFLAGS)
BLR_LIBS += libbalrogg_clmul.a
endif
if BLR_ARMCRC
noinst_LIBRARIES += libbalrogg_armcrc.a
libbalrogg_armcrc_a_SOURCES = src/sys/crcarm.c
libbalrogg_armcrc_a_CFLAGS  = $(AM_CFLAGS) $(BLR_ARMCRC_CFLAGS)
BLR_LIBS += libbalrogg_armcrc.a
endif

balrogg_SOURCES = src/main.c
balrogg_CFLAGS = $(AM_CFLAGS)
balrogg_LDFLAGS = $(AM_LDFLAGS)
balrogg_LDADD = $(BLR_LIBS)

##  Native Windows: a freestanding C runtime on KERNEL32 replaces MSVCRT/UCRT.
if BLR_IS_WINDOWS
balrogg_SOURCES += src/sys/wincrt.c
balrogg_CFLAGS += -fno-builtin -fno-tree-loop-distribute-patterns
balrogg_LDFLAGS += -nostartfiles -nodefaultlibs -Wl,-e,$(BLR_WIN_ENTRY) \
                   -static -static-libgcc -Wl,--subsystem,console
balrogg_LDADD += balrogg-res.$(OBJEXT) -lgcc -lkernel32

balrogg-res.$(OBJEXT): $(top_srcdir)/src/balrogg.rc $(top_srcdir)/src/balrogg.ico config.h
	$(AM_V_GEN)$(WINDRES) -I$(top_builddir) -I$(top_srcdir)/src \
	  -i $(top_srcdir)/src/balrogg.rc -O coff -o $@

balrogg_prof-res.$(OBJEXT): $(top_srcdir)/src/balrogg.rc $(top_srcdir)/src/balrogg.ico config.h
	$(AM_V_GEN)$(WINDRES) -I$(top_builddir) -I$(top_srcdir)/src -DBLR_PROFILE \
	  -i $(top_srcdir)/src/balrogg.rc -O coff -o $@

CLEANFILES = balrogg-res.$(OBJEXT) balrogg_prof-res.$(OBJEXT)
endif

##  Optional profiling build with per-component bit accounting and dumps.
EXTRA_PROGRAMS = balrogg_prof
balrogg_prof_SOURCES  = src/main.c src/sys/prof.c $(BLR_SOURCES)
balrogg_prof_CPPFLAGS = $(AM_CPPFLAGS) -DBLR_PROFILE
balrogg_prof_CFLAGS   = $(AM_CFLAGS)
if BLR_IS_WINDOWS
balrogg_prof_SOURCES += src/sys/wincrt.c
balrogg_prof_CFLAGS  += -fno-builtin -fno-tree-loop-distribute-patterns
balrogg_prof_LDFLAGS  = $(balrogg_LDFLAGS)
balrogg_prof_LDADD    = balrogg_prof-res.$(OBJEXT) -lgcc -lkernel32
else
balrogg_prof_LDFLAGS  = $(AM_LDFLAGS)
balrogg_prof_LDADD    = -lm
endif
if BLR_SSE2
noinst_LIBRARIES += libbalrogg_prof_sse2.a
libbalrogg_prof_sse2_a_SOURCES  = src/coder/cmmix.c
libbalrogg_prof_sse2_a_CPPFLAGS = $(AM_CPPFLAGS) -DBLR_PROFILE -DBLR_CM_SSE2
libbalrogg_prof_sse2_a_CFLAGS   = $(AM_CFLAGS) $(BLR_SSE2_CFLAGS)
balrogg_prof_LDADD += libbalrogg_prof_sse2.a
endif
if BLR_AVX2
noinst_LIBRARIES += libbalrogg_prof_avx2.a
libbalrogg_prof_avx2_a_SOURCES  = src/coder/cmmix.c src/coder/acmmix.c \
                                  src/mod/smpfast.c src/mod/smpls.c \
                                  src/it/s16.c
libbalrogg_prof_avx2_a_CPPFLAGS = $(AM_CPPFLAGS) -DBLR_PROFILE -DBLR_CM_AVX2
libbalrogg_prof_avx2_a_CFLAGS   = $(AM_CFLAGS) $(BLR_AVX2_CFLAGS)
balrogg_prof_LDADD += libbalrogg_prof_avx2.a
endif
if BLR_SSE42
balrogg_prof_LDADD += libbalrogg_sse42.a
endif
if BLR_CLMUL
balrogg_prof_LDADD += libbalrogg_clmul.a
endif
if BLR_ARMCRC
balrogg_prof_LDADD += libbalrogg_armcrc.a
endif

##  Test suite linked to the library and built binary.
check_PROGRAMS = tests/t_suite
TESTS = tests/t_suite tests/strayfree.sh
TEST_EXTENSIONS = .sh
SH_LOG_COMPILER = $(SHELL)

tests_t_suite_SOURCES = tests/t_suite.c tests/t_harness.c tests/t_unit.c \
                        tests/t_layers.c tests/t_files.c tests/t_cli.c tests/t_comments.c
tests_t_suite_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/tests
tests_t_suite_LDADD = $(BLR_LIBS)

if BLR_IS_WINDOWS
check_PROGRAMS += tests/t_wincrt
TESTS += tests/t_wincrt
tests_t_wincrt_SOURCES = tests/t_wincrt.c src/sys/wincrt.c src/sys/win32.c
tests_t_wincrt_CPPFLAGS = $(AM_CPPFLAGS) -DBLR_PROFILE
tests_t_wincrt_CFLAGS = $(balrogg_CFLAGS)
tests_t_wincrt_LDFLAGS = $(balrogg_LDFLAGS)
tests_t_wincrt_LDADD = -lgcc -lkernel32
endif

test-programs: $(check_PROGRAMS)

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
  bootstrap build-aux/version.sh README.md src/balrogg.rc src/balrogg.ico \
  src/opus/parse/README src/opus/parse/COPYING \
  $(BLR_VORBIS_PARTS) $(BLR_OPUS_PARTS) $(BLR_MP3_PARTS) $(BLR_IT_PARTS) \
  tests/t_harness.h tests/strayfree.sh tests/data tests/regress \
  fuzz/harness.h fuzz/fzcfg.h fuzz/fuzz_dec.c fuzz/fuzz_enc.c \
  fuzz/fuzz_ogg.c fuzz/fuzz_opus.c fuzz/fuzz_odec.c fuzz/fuzz_mp3.c fuzz/fuzz_mdec.c \
  fuzz/fuzz_cli.c fuzz/fuzz_dmg.c fuzz/fzmain.c fuzz/balrogg.dict fuzz/build.sh fuzz/run.sh \
  fuzz/triage.sh fuzz/mkseeds.py fuzz/mutate.py \
  contrib/normalize-ogg.py contrib/corpus-rt.sh \
  contrib/gitlog-to-changelog contrib/check-win95-pe.sh contrib/cross.sh \
  contrib/check-win-pe.sh contrib/check-win-unicode.py \
  contrib/dos/fdauto.bat contrib/dos/fdconfig.sys \
  contrib/dos/install-freedos.sh contrib/dos/qemu-exit.c \
  contrib/dos/run-suite.sh


.PHONY: check-full check-torture check-regress check-simd win95-check windows-check \
        print-distfiles test-programs

##  Full tests also scan BLR_TEST_CORPUS.
check-full:
	$(MAKE) check BLR_TEST_LEVEL=full

check-torture:
	$(MAKE) check BLR_TEST_LEVEL=torture

##  Run the same regressions included in the default suite.
check-regress: balrogg$(EXEEXT) tests/t_suite$(EXEEXT)
	$(AM_TESTS_ENVIRONMENT) \
	  $(LOG_COMPILER) ./tests/t_suite$(EXEEXT) regress

##  Run the suite once per mixer kernel this host can dispatch.
check-simd: balrogg$(EXEEXT)
	@tiers=scalar; \
	for t in sse2 avx2; do \
	  if BLR_SIMD=$$t ./balrogg$(EXEEXT) --version >/dev/null 2>&1; then \
	    tiers="$$tiers $$t"; \
	  fi; \
	done; \
	for t in $$tiers; do \
	  echo "== mixer kernel $$t"; \
	  BLR_SIMD=$$t $(MAKE) check || exit 1; \
	done

##  Audit a native Windows image's entry point, PE headers, and imports.
windows-check: balrogg$(EXEEXT)
	OBJDUMP=$(OBJDUMP) NM=$(NM) $(SHELL) \
	  $(top_srcdir)/contrib/check-win-pe.sh ./balrogg$(EXEEXT) ./config.h
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

##  Record the version and date, and write the ChangeLog from the git history or keep the one of an unpacked package.
dist-hook:
	@echo '$(VERSION)' > "$(distdir)/.tarball-version"
	@echo '$(BLR_DATE)' > "$(distdir)/.tarball-date"
	@if test -e "$(top_srcdir)/.git"; then \
	  $(top_srcdir)/contrib/gitlog-to-changelog --srcdir="$(top_srcdir)" \
	    --format='%s%n%n%b%n' > "$(distdir)/ChangeLog.tmp" && \
	  mv "$(distdir)/ChangeLog.tmp" "$(distdir)/ChangeLog"; \
	elif test -f "$(top_srcdir)/ChangeLog"; then \
	  cp "$(top_srcdir)/ChangeLog" "$(distdir)/ChangeLog"; \
	else \
	  echo "dist-hook: no git history and no ChangeLog" >&2; exit 1; \
	fi
