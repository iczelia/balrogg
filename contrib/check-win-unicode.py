#!/usr/bin/env python3
#  Copyright (C) 2026 Kamila Szewczyk
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, version 3.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program. If not, see <http://www.gnu.org/licenses/>.

"""Check modern Windows UTF-8 boundaries on Windows or through Wine."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

def main():
  ap = argparse.ArgumentParser(description=__doc__)
  ap.add_argument("binary", type=Path)
  ap.add_argument("data", type=Path)
  ap.add_argument("--wine", help="Wine executable when running on Linux")
  args = ap.parse_args()
  if os.name != "nt" and not args.wine:
    ap.error("run on Windows or supply --wine")
  binary = args.binary.resolve()
  data = args.data.resolve()

  def native(path):
    s = str(path)
    return "Z:" + s.replace("/", "\\") if args.wine else s

  with tempfile.TemporaryDirectory(prefix="balrogg-unicode-") as tmp:
    root = Path(tmp).resolve()
    work = root / "Zażółć 日本語 𝄞"
    work.mkdir()
    exe = work / "balrogg 𝄞.exe"
    shutil.copyfile(binary, exe)

    def run(*words, code=0, env=None):
      command = [native(exe)] + [native(w) if isinstance(w, Path) else w
                                 for w in words]
      if args.wine:
        command.insert(0, args.wine)
      result = subprocess.run(command, cwd=work, env=env,
                              stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=120)
      if result.returncode != code:
        raise AssertionError((words, result.returncode, result.stdout,
                              result.stderr))
      return result.stdout + result.stderr

    version = run("--version")
    assert b"sse2 dispatched" in version, version
    env = dict(os.environ, BLR_SIMD="scalar")
    assert b"scalar dispatched" in run("--version", env=env)
    for fixture in ("tiny.ogg", "silk_mono_16k.opus"):
      source = data / fixture
      if not source.exists():
        raise AssertionError("missing fixture: " + str(source))
      original = source.read_bytes()
      inp = work / ("音楽 𝄞 " + fixture)
      arc = inp.with_suffix(".blr")
      out = inp.with_suffix(".out")
      inp.write_bytes(original)
      run("-1", "e", inp, arc)
      run("d", arc, out)
      assert out.read_bytes() == original
      run("pages", inp)
      run("dump", arc)
      alias = work / ("別名 " + fixture)
      os.link(inp, alias)
      run("e", inp, alias, code=3)
      assert inp.read_bytes() == original
      alias.unlink()
      arc.unlink()
      #  This invokes the worker from an image path containing a surrogate.
      run("-1", "-b", "--jobs=2", "e", inp)
      batch_arc = Path(str(inp) + ".blr")
      assert batch_arc.exists()
      inp.unlink()
      run("-b", "--jobs=2", "d", batch_arc)
      assert inp.read_bytes() == original

    missing = "不存在 𝄞.ogg"
    log = run("e", missing, "unused.blr", code=3)
    assert missing.encode("utf-8") in log
    for odd in ('quote"𝄞.ogg', "tail 𝄞\\", "", 'a\\\\"b 𝄞'):
      log = run("e", odd, "unused.blr", code=3)
      assert odd.encode("utf-8") in log
    deep = work
    for i in range(12):
      deep = deep / ("long 日本語 directory %02d" % i)
    deep.mkdir(parents=True)
    inp = deep / "𝄞.ogg"
    inp.write_bytes((data / "tiny.ogg").read_bytes())
    arc, out = deep / "𝄞.blr", deep / "𝄞.out"
    assert len(str(inp)) > 260
    run("-1", "e", os.path.relpath(inp, work), os.path.relpath(arc, work))
    run("d", arc, out)
    assert out.read_bytes() == inp.read_bytes()
  print("windows-unicode: ok")


if __name__ == "__main__":
  main()
