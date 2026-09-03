/*  Copyright (C) 2026 Kamila Szewczyk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 3.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.  */

/*  One-process harness covering units, codec layers, whole files, and CLI.  */

#ifndef BLR_T_HARNESS_H
#define BLR_T_HARNESS_H

#include "common.h"
#include <stdarg.h>

/*  Scale iteration counts with BLR_TEST_LEVEL.  */
extern int xt_level;
extern int xt_tracing;
extern unsigned long xt_checks, xt_failures;
extern const char * xt_section;

/*  Where the fixtures live, from the environment; NULL when unset.  */
extern const char * xt_data;      /*  tests/data  */
extern const char * xt_regress;   /*  tests/regress  */
extern const char * xt_corpus;    /*  a contrib/mkdata.sh tree, or NULL  */
extern const char * xt_binary;    /*  the built balrogg, or NULL  */

void xt_init(void);

/*  Optional tracing helps diagnose hangs without cluttering normal logs.  */
void xt_section_begin(const char * name);
void xt_trace(const char * fmt, ...) BLR_PRINTF(1, 2);
void xt_report(int ok, const char * fmt, ...) BLR_PRINTF(2, 3);

#define CHECK(cond, ...)  xt_report((cond), __VA_ARGS__)

int xt_finish(const char * program);


/*  Return fixtures, adding corpus files for full tests.  */
char ** xt_files(const char * ext);
void xt_files_free(char ** list);
int xt_files_count(char ** list);

const char * xt_basename(const char * path);

/*  Return a stable scratch path unique to `tag`.  */
const char * xt_tmp(const char * tag);
void xt_unlink(const char * path);

int xt_same_file(const char * a, const char * b);
long xt_file_size(const char * path);

/*  Run the binary with `args`, optionally capture output, and return status.  */
int xt_run(const char * args, const char * out);
/*  Whether `path` contains `needle`.  */
int xt_file_contains(const char * path, const char * needle);
int xt_file_before(const char * path, const char * first, const char * second);

/*  A small deterministic generator for the unit tests.  */
typedef struct { u32 s; } xt_rng;
void xt_seed(xt_rng * r, u32 seed);
u32 xt_next(xt_rng * r, u32 n);

void xt_run_unit(void);
void xt_run_layers(void);
void xt_run_files(void);
void xt_run_cli(void);

#endif
