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

#ifndef BLR_CODEC_H
#define BLR_CODEC_H

#include "vorbis/vb.h"

#define VB_CFG0 100
int vb_nconfig(void);
int vb_config(int i);
int vb_config_acm(int n);
int vb_config_tune(int n, vb_tune * t);

enum { VC_MONO, VC_MONO_HI, VC_MULTI, VC_MULTI_HI, VC_MULTI_X, VC_SURR, VC_N };
int vb_classify(const char * in);
int vb_level_cfg(int level, int cls);

void vb_pack(const char * in, const char * out, int cfg);
void vb_unpack(const char * in, const char * out);

#endif
