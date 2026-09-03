#!/usr/bin/env python3
"""Re-page an Ogg file, or mutate one and look for silent mismatches.

    mutate.py repage IN OUT MAXBODY [flush|mix|all]
    mutate.py fuzz BALROGG FILE ITERATIONS SEED

`repage` lays the packets out again with at most MAXBODY payload bytes per
page, so larger packets continue across pages; the header mode says whether
each header packet gets a page of its own (flush), whether the setup header
shares a page with the first audio packets (mix), or whether everything
starts on the first page (all).  `fuzz` damages one page at a time, keeps the
CRC valid, and reports every input that both directions accept but that does
not come back byte for byte.  A refusal is the expected outcome for most
mutations; a mismatch is a bug.
"""

import os, random, struct, subprocess, sys

def crc_table():
    t = []
    for i in range(256):
        c = i << 24
        for _ in range(8):
            c = ((c << 1) ^ 0x04C11DB7) if c & 0x80000000 else (c << 1)
            c &= 0xFFFFFFFF
        t.append(c)
    return t

CRC = crc_table()

def crc(b):
    c = 0
    for x in b:
        c = ((c << 8) & 0xFFFFFFFF) ^ CRC[((c >> 24) ^ x) & 0xFF]
    return c

def parse(buf):
    """Pages as [type, granule, serial, sequence, lacing, body] lists."""
    at, pages = 0, []
    while at < len(buf):
        if buf[at:at + 4] != b"OggS":
            raise ValueError("no Ogg page at %d" % at)
        typ = buf[at + 5]
        gran = struct.unpack("<q", buf[at + 6:at + 14])[0]
        ser, seq = struct.unpack("<II", buf[at + 14:at + 22])
        nseg = buf[at + 26]
        lace = list(buf[at + 27:at + 27 + nseg])
        blen = sum(lace)
        body = buf[at + 27 + nseg:at + 27 + nseg + blen]
        pages.append([typ, gran, ser, seq, lace, body])
        at += 27 + nseg + blen
    return pages

def emit(p):
    typ, gran, ser, seq, lace, body = p
    hdr = (b"OggS" + bytes([0, typ]) + struct.pack("<qII", gran, ser, seq)
           + b"\0\0\0\0" + bytes([len(lace)]) + bytes(lace))
    page = bytearray(hdr + body)
    page[22:26] = struct.pack("<I", crc(page))
    return bytes(page)

def packets(pages):
    """Per stream, in order of first appearance: (serial, [[bytes, granule]]).
    The granule is that of the page the packet ends, or None."""
    streams, order = {}, []
    for typ, gran, ser, seq, lace, body in pages:
        if ser not in streams:
            streams[ser] = {"pk": [], "cur": b""}
            order.append(ser)
        s, at = streams[ser], 0
        for i, l in enumerate(lace):
            s["cur"] += body[at:at + l]
            at += l
            if l < 255:
                s["pk"].append([s["cur"], gran if i == len(lace) - 1 else None])
                s["cur"] = b""
    return [(ser, streams[ser]["pk"]) for ser in order]

def repage(ser, pk, maxbody, hdrmode):
    nh = 0
    if pk and pk[0][0][:1] == b"\x01":
        nh = 3                                  # Vorbis: three headers
    if pk and pk[0][0][:8] == b"OpusHead":
        nh = 2
    if hdrmode == "flush":
        groups = [[pk[i]] for i in range(nh)] + [pk[nh:]]
    elif hdrmode == "mix":
        groups = [[pk[i]] for i in range(max(nh - 1, 0))] + [pk[max(nh - 1, 0):]]
    else:
        groups = [pk]
    out, state = [], {"seq": 0, "first": True}
    total, done = len(pk), 0
    for g in groups:
        segs, body, gran, cont = [], b"", -1, False
        def flush(eos):
            typ = (1 if cont else 0) | (2 if state["first"] else 0) | (4 if eos else 0)
            out.append(emit([typ, gran, ser, state["seq"], segs, body]))
            state["seq"] += 1
            state["first"] = False
        for data, pgran in g:
            done += 1
            rem = data
            while True:
                room = maxbody - len(body)
                take = min(len(rem), room)
                n255, r = divmod(take, 255)
                if take == len(rem):            # the packet ends on this page
                    segs += [255] * n255 + [r]
                    body += rem
                    if pgran is not None:
                        gran = pgran
                    if len(segs) >= 250 or len(body) >= maxbody:
                        flush(done == total)
                        segs, body, gran, cont = [], b"", -1, False
                    break
                take = n255 * 255
                segs += [255] * n255
                body += rem[:take]
                rem = rem[take:]
                flush(False)
                segs, body, gran, cont = [], b"", -1, True
        if segs or body:
            flush(done == total)
    return b"".join(out)

def cmd_repage(argv):
    inp, outp, maxbody = argv[0], argv[1], int(argv[2])
    hdrmode = argv[3] if len(argv) > 3 else "flush"
    buf = open(inp, "rb").read()
    out = b"".join(repage(ser, pk, maxbody, hdrmode)
                   for ser, pk in packets(parse(buf)))
    open(outp, "wb").write(out)

def mutate(pages, rnd):
    pg = [list(p) for p in pages]
    i = rnd.randrange(len(pg))
    body = bytearray(pg[i][5])
    if body and rnd.random() < 0.85:
        for _ in range(rnd.randint(1, 4)):
            k, m = rnd.randrange(len(body)), rnd.random()
            if m < 0.5:
                body[k] ^= 1 << rnd.randrange(8)
            elif m < 0.8:
                body[k] = rnd.randrange(256)
            else:
                body[k] = 0
        pg[i][5] = bytes(body)
        return pg
    f = rnd.randrange(4)
    if f == 0:
        pg[i][0] ^= 1 << rnd.randrange(3)
    elif f == 1:
        pg[i][1] = rnd.choice([-1, 0, pg[i][1] ^ (1 << rnd.randrange(40)),
                               pg[i][1] + rnd.randint(-5000, 5000)])
    elif f == 2:
        pg[i][3] = (pg[i][3] + rnd.randint(-3, 3)) & 0xFFFFFFFF
    elif pg[i][4]:
        lace = list(pg[i][4])
        k = rnd.randrange(len(lace))
        nv = rnd.choice([0, 255, rnd.randrange(256)])
        d = nv - lace[k]
        if d > 0:
            pg[i][5] = pg[i][5] + bytes(d)
        else:
            at = sum(lace[:k])
            pg[i][5] = pg[i][5][:at + nv] + pg[i][5][at + lace[k]:]
        lace[k] = nv
        pg[i][4] = lace
    return pg

def cmd_fuzz(argv):
    binp, src, iters, seed = argv[0], argv[1], int(argv[2]), int(argv[3])
    rnd = random.Random(seed)
    pages = parse(open(src, "rb").read())
    base = "mutate-%d" % seed
    stats = {"ok": 0, "refused": 0, "mismatch": 0, "crash": 0, "decode": 0}
    for it in range(iters):
        out = b"".join(emit(p) for p in mutate(pages, rnd))
        fn = base + ".in"
        open(fn, "wb").write(out)
        r = subprocess.run([binp, "-3", "e", fn, fn + ".blr"], capture_output=True)
        if r.returncode == 1:
            stats["refused"] += 1
            continue
        keep = "%s-%d" % (base, it)
        if r.returncode != 0:
            stats["crash"] += 1
            os.rename(fn, keep + ".crash")
            print("encode exit %d: %s" % (r.returncode, r.stderr.decode(errors="replace").strip()))
            continue
        r = subprocess.run([binp, "d", fn + ".blr", fn + ".out"], capture_output=True)
        if r.returncode != 0:
            stats["decode"] += 1
            os.rename(fn, keep + ".decfail")
            print("decode exit %d: %s" % (r.returncode, r.stderr.decode(errors="replace").strip()))
        elif open(fn + ".out", "rb").read() != out:
            stats["mismatch"] += 1
            os.rename(fn, keep + ".mismatch")
            print("mismatch: %s.mismatch" % keep)
        else:
            stats["ok"] += 1
    for f in (fn, fn + ".blr", fn + ".out"):
        try:
            os.unlink(f)
        except OSError:
            pass
    print("%s: %s" % (os.path.basename(src),
                      ", ".join("%s %d" % kv for kv in stats.items())))
    return 1 if stats["mismatch"] or stats["crash"] or stats["decode"] else 0

if __name__ == "__main__":
    if len(sys.argv) >= 5 and sys.argv[1] == "repage":
        cmd_repage(sys.argv[2:])
    elif len(sys.argv) == 6 and sys.argv[1] == "fuzz":
        sys.exit(cmd_fuzz(sys.argv[2:]))
    else:
        sys.stderr.write(__doc__)
        sys.exit(2)
