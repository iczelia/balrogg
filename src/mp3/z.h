#ifndef MP3_Z_H
#define MP3_Z_H

#include <stdio.h>

int mp3z_pack_stream(FILE * in, FILE * out, unsigned opt, long * junk);
int mp3z_unpack_stream(FILE * in, FILE * out, unsigned opt);
#define MP3Z_SCF  0x8u
#define MP3Z_EXT  0x10u
#define MP3Z_MIX  0x20u
#define MP3Z_MAG  0x40u
#define MP3Z_SMIX 0x80u
#define MP3Z_MAGF 0x400u
#define MP3Z_MIX4 0x800u
#define MP3Z_SLIM 0x2000u
#define MP3Z_LEVEL0 (MP3Z_SCF | MP3Z_EXT | MP3Z_MAGF)
#define MP3Z_LEVEL4 (MP3Z_SCF | MP3Z_EXT | MP3Z_MIX | MP3Z_MAG)
#define MP3Z_LEVEL5 (MP3Z_LEVEL4 | MP3Z_MIX4)
#define MP3Z_LEVEL6 (MP3Z_LEVEL5 | MP3Z_SMIX)
#define MP3Z_LEVEL7 (MP3Z_MAGF | MP3Z_SLIM)

#endif
