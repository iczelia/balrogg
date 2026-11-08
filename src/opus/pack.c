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

static void pvq_choice(void) {
  int i, own = 1;
  if (!pvq_free) return;
  Fi(PVQ_MIXMAX, own &= pvq_opt[i] == pvq_dflt[i]);
  om_bit(K_CHOICE, 0x2000, &own);
  if (!own)
    Fi(PVQ_MIXMAX,
      int v = pvq_opt[i];
      om_int(K_CHOICE | (okey) (i + 1), PO_N, 256, 24, 8192, &v);
      pvq_opt[i] = (u8) v);
  pvq_ext = 0;
  Fi(PVQ_MIXMAX, pvq_ext |= pvq_opt[i] == PO_MIXX);
  if (om_stat) pvq_ext = pvq_xok;
  if (pvq_ext) px_reset();
}

#define OM_RETRY (-1)

static int pack_pass(blr_file * input, const char * in, const char * out,
                     const ogg_gaps * gs, archive * a, blr_file ** output) {
  ostream s;
  rc_enc e;
  oc_state cs, * cx = xcalloc(256, sizeof *cx);
  OpusDecoder ** dec = NULL;
  u8 * packet = xmalloc(OPUS_MAXPKT), * msb = xmalloc(2 * OPUS_MAXPKT);
  sz off = 0;
  int i, err = 0, rc = BLR_EXIT_REFUSED, more, ndec = 0;
  u32 u;

  memset(&e, 0, sizeof e);
  if (parse_stream(input, &off, &s)) goto done;
  blr_progress_begin(in, om_stat ? "tuning" : "encoding", input->len);

  om_init();
  if (om_stat) {
    rc_enc_init(&e);  st_lginit();
    memset(st_cost, 0, sizeof st_cost);  memset(st_n, 0, sizeof st_n);
  } else {
    *output = bf_open(out, 1);  arc_begin(a, *output);
    rc_enc_file(&e, arc_newstream(a));
    if (oz_on) zr_start(arc_newstream(a), 0, 0);
  }
  om_mode = OM_ENC;  E = &e;  D = NULL;
  memset(&cs, 0, sizeof cs);
  pvq_choice();

  for (;;) {
    if (s.npk > 1L << 24 || s.npg > 1L << 24) {
      fprintf(stderr, "balrogg: %s has %d packets on %d pages, limit %ld\n",
              in, s.npk, s.npg, 1L << 24);
      goto done;
    }
    more = 1;  om_bit(K_LINK, 0x8000, &more);
    om_reset();

    u = (u32) s.npk;  oc_count(K_COUNT, &u);
    u = (u32) s.npg;  oc_count(K_COUNT | 0x100, &u);
    u = s.serial;  oc_u32(&u);
    Fi(s.npg, oc_page(&cs, s.pg + i));

    { int n = s.pk[0].len;
      packet_read(input, s.pk, packet);  oc_blob(packet, &n); }
    { cmt_io c;
      tag_reader r;
      u = (u32) s.pk[1].len;  oc_count(K_COUNT | 0x200, &u);
      tag_open(&r, input, s.pk + 1);
      { u8 vb[16];  u32 vn = 0;
        gap_cls = GAP_OTHER;
        if (r.len >= 12) {
          tag_read(&r, 8, vb, 4);
          vn = (u32) MIN((sz) rd32le(vb), MIN(r.len - 12, sizeof vb));
          tag_read(&r, 12, vb, vn);
          gap_cls = gap_class(vb, vn);
        }
        om_int(K_GAPCLS, GAP_NCLS, 192, 28, 16384, &gap_cls); }
      c.enc = 1;  c.input = &r;  c.read = tag_read;
      c.coder = NULL;  c.bit = comment_bit;  c.output = NULL;  c.write = NULL;
      cmt_code(&c, u, om_feat & OF_TAGS ? CMT_OPUS | CMT_ODICT : CMT_OPUS);
      xfree(r.ext);
    }

    dec = decs_open(&s);  ndec = s.nstreams;
    if (!dec) {
      fprintf(stderr, "balrogg: %s cannot create Opus decoder\n", in);
      goto done;
    }

    for (i = 2; i < s.npk; i++) {
      int at = 0, k, len = s.pk[i].len;
      packet_read(input, s.pk + i, packet);
      Fk(s.nstreams,
        unsigned char toc;
        const unsigned char * frames[48];
        opus_int16 sizes[48];
        u8 * sub = packet + at;
        int poff = 0, nf, j, sum = 0, L = len - at, used = L;
        if (k < s.nstreams - 1) {
          used = ms_split(packet + at, len - at, msb, &L, msb + OPUS_MAXPKT);
          if (used < 0) {
            fprintf(stderr, "balrogg: %s packet %d stream %d is not "
                            "self-delimited\n", in, i, k);
            goto done;
          }
          sub = msb;
        }
        nf = opus_packet_parse(sub, L, &toc, frames, sizes, &poff);
        if (nf < 0) {
          fprintf(stderr, "balrogg: %s packet %d parse failed (%d)\n", in, i, nf);
          goto done;
        }
        if (poff < 1 || poff > L) {
          fprintf(stderr,
                  "balrogg: %s packet %d header is %d bytes, limit %d\n",
                  in, i, poff, L);
          goto done;
        }
        Fj(nf, sum += sizes[j]);
        oc_packet_hdr(k ? cx + k : &cs, &poff, sub, &L);
        oc_packet_trailer(L - poff - sum, sub + poff + sum);
        oe_begin();  orec_mode = OREC_ANALYZE;
        err = opus_decode(dec[k], sub, L);
        oe_end();  orec_mode = OREC_OFF;
        if (err < 0) {
          fprintf(stderr, "balrogg: %s packet %d decode failed (%d)\n", in, i, err);
          goto done;
        }
        if (orec_lost) { rc = OM_RETRY;  goto done; }
        at += used);
      if (om_stat && e.len > 1UL << 16) e.len = 1;
      blr_progress_update(s.pk[i].off + (sz) s.pk[i].len);
    }
    decs_close(dec, ndec);  dec = NULL;

    if (off >= input->len) break;
    st_free(&s);
    if (parse_stream(input, &off, &s)) goto done;
  }
  more = 0;  om_bit(K_LINK, 0x8000, &more);

  if (!om_stat) {
    rc_enc_finish(&e);
    if (oz_on) zr_finish();
    if (gs->n) ogg_gaps_enc(gs, input->parent, arc_newstream(a));
    arc_finish(a);
  }
  rc = BLR_EXIT_OK;
done:
  if (rc) blr_progress_cancel();
  decs_close(dec, ndec);
  rc_enc_free(&e);  xfree(packet);  xfree(msb);  xfree(cx);  st_free(&s);
  if (!om_stat || rc) om_free();
  if (!rc) blr_progress_end();
  return rc;
}

static void om_close(blr_file * raw, blr_file * input, ogg_gaps * gs) {
  if (input != raw) bf_close(input);
  bf_close(raw);  ogg_gaps_free(gs);
}

int opus_classify(const char * in) {
  blr_file * raw = bf_open(in, 0), * input;
  ostream s;
  ogg_gaps gs;
  sz off = 0;
  int k = -1;
  ogg_gaps_find(&gs, raw, 0);  input = ogg_gaps_view(&gs, raw);
  if (!parse_stream(input, &off, &s)) k = st_class(input, &s);
  st_free(&s);  om_close(raw, input, &gs);
  return k;
}

int opus_pack_classed(const char * in, const char * out, const int * cfg) {
  blr_file * raw = bf_open(in, 0), * input, * output = NULL;
  archive a;
  ocfg c;
  ostream s;
  ogg_gaps gs;
  sz off = 0;
  const ocfg * row;
  int rc = BLR_EXIT_OK, act = RA_OWN, i, id;

  om_length(raw->len);
  ogg_gaps_find(&gs, raw, 0);  input = ogg_gaps_view(&gs, raw);
  if (parse_stream(input, &off, &s)) {
    st_free(&s);  om_close(raw, input, &gs);
    return BLR_EXIT_REFUSED;
  }
  id = cfg[st_class(input, &s)];  row = om_cfg(id);
  FATAL_UNLESS(row != NULL, "opus: no configuration %d", id);
  if (row->act) act = row->act >> 2 * st_rate(&s) & 3;
  st_free(&s);
  orec_log = bf_special(out);  orec_lost = 0;
  for (;;) {
    c = *row;  om_level(&c);
    if (act == RA_RULE)
      Fi(PVQ_MIXMAX,
        pvq_opt[i] = (u8) (i >= c.hi ? PO_BLEND : c.ext ? PO_MIXX : PO_MIX));
    arc_init(&a, 1, id - OM_CFG0);
    a.gaps = gs.n != 0;  a.size = input->len + ogg_gaps_len(&gs);
    if (c.acm) {
      om_acm = opa_new(om_opaf(c.feat));  opa_bind(om_acm, om_acm_bit, NULL);
    }
    if (act == RA_COST) {
      om_stat = 1;
      rc = pack_pass(input, in, out, &gs, &a, &output);
      om_stat = 0;
      if (!rc) pvq_pick(c.lam);
    }
    if (!rc) rc = pack_pass(input, in, out, &gs, &a, &output);
    if (rc != OM_RETRY) break;
    arc_free(&a);  bf_close(output);  output = NULL;
    opa_free(om_acm);  om_acm = NULL;
    orec_log = 1;  orec_lost = 0;  rc = BLR_EXIT_OK;
  }
  orec_log = 0;
  arc_free(&a);  bf_close(output);  om_close(raw, input, &gs);
  opa_free(om_acm);  om_acm = NULL;
  return rc;
}

int opus_pack(const char * in, const char * out, int n) {
  int cfg[OC_N], i;
  Fi(OC_N, cfg[i] = n);
  return opus_pack_classed(in, out, cfg);
}

int opus_unpack(const char * in, const char * out) {
  ostream s;
  rc_dec d;
  oc_state cs;
  archive a;
  OpusDecoder ** dec = NULL;
  oc_state * cx = xcalloc(256, sizeof *cx);
  u8 * packet = xmalloc(2 * OPUS_MAXPKT), * msb = xmalloc(OPUS_MAXPKT);
  blr_file * input = bf_open(in, 0), * output = NULL;
  page_writer w;
  ocfg cf;
  int i, err = 0, rc = BLR_EXIT_REFUSED, links = 0, more, ndec = 0, gap;
  u32 u = 0;

  memset(&w, 0, sizeof w);  memset(&d, 0, sizeof d);
  memset(&s, 0, sizeof s);
  arc_read(&a, input);
  gap = a.gaps;
  if (!a.opus || a.n < (sz) 1 + (sz) gap || a.n > (sz) 2 + (sz) gap) {
    fprintf(stderr, "balrogg: %s invalid Opus archive (%lu streams)\n", in,
            (unsigned long) a.n);
    goto done;
  }
  rc_dec_file(&d, a.s[0].file, a.s[0].off, a.s[0].len);
  om_length((sz) a.size);
  if (!om_cfg(OM_CFG0 + a.cfg)) {
    fprintf(stderr, "balrogg: %s holds an unknown Opus configuration\n", in);
    goto done;
  }
  cf = *om_cfg(OM_CFG0 + a.cfg);
  om_level(&cf);  om_init();  om_mode = OM_DEC;  E = NULL;  D = &d;
  memset(&cs, 0, sizeof cs);
  if (a.n != (sz) 1 + (sz) gap + (sz) (cf.oz != 0)) {
    fprintf(stderr, "balrogg: %s invalid Opus archive (%lu streams)\n", in,
            (unsigned long) a.n);
    goto done;
  }
  pvq_choice();
  if (oz_on) zr_start(a.s[1].file, a.s[1].off, a.s[1].len);
  if (cf.acm) {
    om_acm = opa_new(om_opaf(cf.feat));  opa_bind(om_acm, om_acm_bit, NULL);
  }
  blr_progress_begin(in, "decoding", a.s[0].len);
  output = bf_open(out, 1);  bf_cap(output, (sz) a.size);  w.output = output;
  w.page = xmalloc(OGG_HDRMIN + OGG_MAXSEG + 65025);
  for (;;) {
    FATAL_UNLESS(!rc_dec_spent(&d), "%s: the Opus stream ran out", in);
    more = 0;  om_bit(K_LINK, 0x8000, &more);
    if (!more) break;
    om_reset();
    st_free(&s);
    w.stream = &s;  w.pg = w.seg = 0;  w.body = w.partial = 0;

    oc_count(K_COUNT, &u);
    if (u < 2 || u > 1UL << 24) {
      fprintf(stderr, "balrogg: %s invalid packet count %lu\n", in,
              (unsigned long) u);
      goto done;
    }
    s.npk = (int) u;
    oc_count(K_COUNT | 0x100, &u);
    if (u < 1 || u > 1UL << 24) {
      fprintf(stderr, "balrogg: %s invalid page count %lu\n", in,
              (unsigned long) u);
      goto done;
    }
    s.npg = (int) u;
    oc_u32(&u);  s.serial = u;

    Fi(s.npg,
      FATAL_UNLESS(!rc_dec_spent(&d), "%s: the Opus stream ran out", in);
      s.pg = xgrow(s.pg, &s.pgcap, (sz) i + 1, sizeof *s.pg, 1024);
      memset(s.pg + i, 0, sizeof *s.pg);
      oc_page(&cs, s.pg + i));

    { int n = 0;
      oc_blob(packet, &n);
      if (n < 19 || memcmp(packet, "OpusHead", 8)) {
        fprintf(stderr, "balrogg: %s has no OpusHead\n", in);  goto done;
      }
      if (head_streams(packet, n, &s)) {
        fprintf(stderr, "balrogg: %s invalid Opus channel mapping\n", in);
        goto done;
      }
      page_packet(&w, packet, (sz) n);
    }
    { cmt_io c;
      oc_count(K_COUNT | 0x200, &u);
      FATAL_UNLESS(u <= CMT_MAXLEN, "opus: comments exceed 120 MiB");
      c.enc = 0;  c.input = NULL;  c.read = NULL;
      c.coder = NULL;  c.bit = comment_bit;
      c.output = &w;  c.write = page_bytes;
      gap_cls = 0;  om_int(K_GAPCLS, GAP_NCLS, 192, 28, 16384, &gap_cls);
      FATAL_UNLESS(gap_cls < GAP_NCLS, "opus: invalid encoder class");
      cmt_code(&c, u, om_feat & OF_TAGS ? CMT_OPUS | CMT_ODICT : CMT_OPUS);
      page_endpacket(&w);
    }

    dec = decs_open(&s);  ndec = s.nstreams;
    if (!dec) {
      fprintf(stderr, "balrogg: %s cannot create Opus decoder\n", in);
      goto done;
    }

    for (i = 2; i < s.npk; i++) {
      int at = 0, k;
      FATAL_UNLESS(!rc_dec_spent(&d), "%s: the Opus stream ran out", in);
      Fk(s.nstreams,
        unsigned char toc;
        const unsigned char * frames[48];
        opus_int16 sizes[48];
        int nhdr = 0, L = 0, nf, j, sum = 0, poff = 0;
        int last = k == s.nstreams - 1;
        u8 * sub = last ? packet + at : msb;
        oc_packet_hdr(k ? cx + k : &cs, &nhdr, sub, &L);
        if (L < 1 || L > OPUS_MAXPKT - at - 2 * !last || nhdr < 1 || nhdr > L) {
          fprintf(stderr, "balrogg: %s packet %d has header %d and length %d\n",
                  in, i, nhdr, L);
          goto done;
        }
        memset(sub + nhdr, 0, (sz) (L - nhdr));
        nf = opus_packet_parse(sub, L, &toc, frames, sizes, &poff);
        if (nf < 0 || poff != nhdr) {
          fprintf(stderr, "balrogg: %s packet %d header mismatch "
                          "(parse %d, size %d, expected %d)\n",
                  in, i, nf, poff, nhdr);
          goto done;
        }
        Fj(nf, sum += sizes[j]);
        oc_packet_trailer(L - nhdr - sum, sub + nhdr + sum);
        oe_begin();  orec_mode = OREC_SYNTH;
        err = opus_decode(dec[k], sub, L);
        oe_end();  orec_mode = OREC_OFF;
        if (err < 0) {
          fprintf(stderr, "balrogg: %s cannot rebuild packet %d (%d)\n", in, i, err);
          goto done;
        }
        at += last ? L : ms_join(sub, L, packet + at));
      page_packet(&w, packet, (sz) at);
      blr_progress_update(MIN(d.pos, d.len));
    }
    decs_close(dec, ndec);  dec = NULL;

    page_empty(&w);
    FATAL_UNLESS(w.pg == s.npg && !w.seg, "opus: page layout exceeds packets");
    links++;
  }
  if (!links) {
    fprintf(stderr, "balrogg: %s holds no Opus stream\n", in);
    goto done;
  }
  if (gap)
    ogg_gaps_dec(output, a.s[1 + oz_on].file, a.s[1 + oz_on].off,
                 a.s[1 + oz_on].len);
  FATAL_UNLESS(output->len == a.size,
               "%s: %lu bytes decoded, the archive records %lu", in,
               (unsigned long) output->len, (unsigned long) a.size);
  rc = BLR_EXIT_OK;
done:
  if (rc) blr_progress_cancel();
  decs_close(dec, ndec);
  bf_close(output);  xfree(w.page);
  bf_close(input);  xfree(packet);  xfree(msb);  xfree(cx);  rc_dec_free(&d);
  arc_free(&a);  st_free(&s);  om_free();
  opa_free(om_acm);  om_acm = NULL;
  if (!rc) blr_progress_end();
  return rc;
}
