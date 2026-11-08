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

#ifndef BLR_OPUSMODE_H
#define BLR_OPUSMODE_H

#include "sys/common.h"

#define OM_CFG0 200
int opus_nconfig(void);
int opus_config_id(int i);
int opus_config_acm(int n);
enum { OC_SILK, OC_LM0, OC_NARROW, OC_MONO, OC_LOW, OC_HIGH, OC_LM0H, OC_N };
int opus_level_cfg(int level, int cls);
int opus_classify(const char * in);
int opus_pack_classed(const char * in, const char * out, const int * cfg);
int opus_pack(const char * in, const char * out, int n);
int opus_unpack(const char * in, const char * out);

const char * opus_mode_version(void);

#endif
