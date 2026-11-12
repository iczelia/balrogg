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

#ifndef BLR_COMMENT_H
#define BLR_COMMENT_H

#include "sys/common.h"

#define CMT_MAXLEN (120UL * 1024 * 1024)

typedef struct {
  int enc;
  void * input;
  void (*read)(void * input, sz at, u8 * data, sz len);
  void * coder;
  int (*bit)(void * coder, u32 prob, int bit);
  void * output;
  void (*write)(void * output, const u8 * data, sz len);
} cmt_io;

#define CMT_OPUS  1
#define CMT_ODICT 2
void cmt_code(cmt_io * io, sz len, int opus);

#endif
