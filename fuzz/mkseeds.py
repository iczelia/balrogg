#!/usr/bin/env python3
"""Convert Ogg files to length-prefixed packet seeds for fuzz_ogg.

Continued packets are joined across pages.
"""

import sys, os, struct

def packets(buf):
    at, pend, out = 0, b"", []
    while at + 27 <= len(buf):
        if buf[at:at + 4] != b"OggS":
            break
        nseg = buf[at + 26]
        hl = 27 + nseg
        if at + hl > len(buf):
            break
        lace = buf[at + 27:at + hl]
        blen = sum(lace)
        if at + hl + blen > len(buf):
            break
        body, off = buf[at + hl:at + hl + blen], 0
        run = pend
        for i, l in enumerate(lace):
            run += body[off:off + l]
            off += l
            if l != 255:
                out.append(run)
                run = b""
        pend = run
        at += hl + blen
    if pend:
        out.append(pend)
    return out

def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: mkseeds.py INPUT_DIR OUTPUT_DIR")
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    n = 0
    for name in sorted(os.listdir(src)):
        path = os.path.join(src, name)
        if not os.path.isfile(path):
            continue
        with open(path, "rb") as f:
            buf = f.read()
        rec = b"".join(struct.pack("<H", len(p)) + p
                       for p in packets(buf) if 0 < len(p) < 65536)
        if len(rec) < 16 or len(rec) > (1 << 20):
            continue
        with open(os.path.join(dst, "%s.pkt" % name), "wb") as f:
            f.write(rec)
        n += 1
    print("%d seeds -> %s" % (n, dst))

main()
