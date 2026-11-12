/*  Copyright (C) 2026 Kamila Szewczyk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 3.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "opus/opus.h"
#include "fmt/arc.h"
#include "fmt/cmt.h"
#include "fmt/ogg.h"
#include "opus/ent.h"
#include "coder/rc.h"
#include "coder/cm.h"
#include "sys/map.h"
#include "sys/prof.h"

#include "opus/parse/opusdec.h"
#include "opus/acm.h"
#include "opus/cwrs.h"

#include <limits.h>

#if defined(HAVE_SSE2) && defined(__SSE2__)
#include <emmintrin.h>
#define OM_SSE2 1
#else
#define OM_SSE2 0
#endif

const char * opus_mode_version(void) { return OPUS_PARSER_VERSION; }

#include "opus/rc.c"
#include "opus/cfg.c"
#include "opus/ctx.c"
#include "opus/pvq.c"
#include "opus/side.c"
#include "opus/energy.c"
#include "opus/site.c"
#include "opus/fast.c"
#include "opus/op.c"
#include "opus/frame.c"
#include "opus/stream.c"
#include "opus/pack.c"
