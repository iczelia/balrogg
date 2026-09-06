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

#include "rc.h"

rc_hook rc_hook_fn;
void * rc_hook_ctx;

void rc_hook_set(rc_hook h, void * ctx) { rc_hook_fn = h;  rc_hook_ctx = ctx; }
/*  Decoder.
    Seed from up to four bytes so short streams remain valid.  */

void rc_dec_init(rc_dec * d, const u8 * buf, sz len) {
  sz i, n = MIN(len, 4);
  d->in = buf;  d->len = len;  d->pos = n;
  d->file = NULL;  d->window = NULL;  d->off = d->base = 0;  d->avail = len;
  d->code = 0;  d->range = 0xFFFFFFFFUL;
  Fi(n, d->code |= (u32) buf[i] << (24 - 8 * i));
}

void rc_dec_refill(rc_dec * d) {
  d->base = d->pos;  d->avail = MIN(BLR_IO_CHUNK, d->len - d->pos);
  bf_read(d->file, d->off + d->pos, d->window, d->avail);
  d->in = d->window;
}

void rc_dec_file(rc_dec * d, blr_file * f, sz off, sz len) {
  sz i;
  memset(d, 0, sizeof *d);
  d->file = f;  d->off = off;  d->len = len;  d->range = 0xFFFFFFFFUL;
  d->window = xmalloc(MIN(len, BLR_IO_CHUNK));
  Fi(MIN(len, 4), d->code = (d->code << 8) | rc_get(d));
  if (len && len < 4) d->code <<= 8 * (4 - len);
}

void rc_dec_free(rc_dec * d) { free(d->window);  d->window = NULL; }

/*  Encoder.
    Propagate carries forward with one cached byte and a pending 0xFF count.
    `carry` holds bit 32 of `low` until the next shift.  */

void rc_enc_init(rc_enc * e) {
  e->low = 0;  e->range = 0xFFFFFFFFUL;  e->carry = 0;
  /*  Begin with one owed zero byte, which finish strips.  */
  e->cache = 0;  e->pending = 1;
  e->cap = 1 << 12;  e->len = 0;  e->buf = xmalloc(e->cap);
  e->file = NULL;
}

void rc_enc_file(rc_enc * e, blr_file * output) {
  rc_enc_init(e);  free(e->buf);  e->buf = NULL;  e->cap = 0;
  e->file = output;
}

void rc_enc_free(rc_enc * e) {
  free(e->buf);  e->buf = NULL;  e->cap = e->len = 0;
  e->file = NULL;
}

/*  Flush enough bytes for the decoder seed to fall inside the final range.  */
sz rc_enc_finish(rc_enc * e) {
  if (e->range > 0x2000000UL) { e->range = 0x800000UL;  rc_addlow(e, 0x1000000UL); }
  else                        { e->range = 0x8000UL;    rc_addlow(e, 0x800000UL); }
  do { rc_shift(e);  e->range <<= 8; } while (e->range < RC_TOP);
  /*  Release the cached byte now that no carry can arrive.  */
  rc_put(e, e->cache);
  while (--e->pending) rc_put(e, 0xFF);
  e->pending = 0;
  if (e->len < 1 || (!e->file && e->buf[0] != 0))
    FATAL_CODE(BLR_EXIT_INTERNAL, "internal: range coder leading byte %02x",
               e->len && !e->file ? e->buf[0] : 0);
  return e->len - 1;
}

u8 * rc_enc_data(rc_enc * e) { return e->buf + 1; }

void rc_probs_init(u16 * p, sz n) {
  sz i;
  Fi(n, p[i] = RC_PINIT);
}

/*  Count-capped adaptation.
    Counts select the adaptation rate but are not stored. Per-mode hot paths
    remain separate to avoid a branch on every bit.  */

u16 rc_divt[RC_CNTMAX + 1];

void rc_adapt_init(void) {
  sz i;
  Fi(RC_CNTMAX + 1, rc_divt[i] = (u16) (65535U / (unsigned) (i + 3)));
}
