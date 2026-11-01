"""Build a MIPS IDA database for WCW/nWo Revenge (US) from the ROM.

Same pipeline as wcw_build_idb.py: lays the main segment and the match overlay (ovl_b) out at
their N64 addresses in a flat image and runs idat headless with wcw_idb_setup.py
(AKI_GAME=revenge), which splits segments, defines functions, applies _re_docs/symbols/revenge
and exports for tools/sym. Function starts are seeded from the Revenge recomp's symbol dump
(_recomps/WCWnWoRevengeRecomp/syms/dump.toml, the main and ovl_b sections). The database is
ROM-derived, so it goes under _tools/ (git-ignored). Usually run via
`py tools/sym/build.py --game revenge --ida`.

    py tools/ida/revenge_build_idb.py [--rom ROM] [--idat PATH] [--out DIR]
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import tomllib

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHA1_US = "e1711a2511394b9357b5f1ac8ca5cc17bd674836"
DUMP = os.path.join(REPO, "_recomps", "WCWnWoRevengeRecomp", "syms", "dump.toml")

IMAGE_BASE = 0x80000000
IMAGE_END = 0x80100000  # covers ovl_b bss (ends 0x800FAFA0)

# (rom_start, rom_end, vram) from the recomp's splat config (disasm/revenge.yaml)
PIECES = [
    (0x001000, 0x03C770, 0x80000400),  # entry + main (code with data pockets)
    (0x0834A0, 0x0DAC50, 0x80090000),  # ovl_b (match overlay) text/data
]


def build_image(rom):
    img = bytearray(IMAGE_END - IMAGE_BASE)
    for start, end, vram in PIECES:
        off = vram - IMAGE_BASE
        img[off:off + end - start] = rom[start:end]
    return bytes(img)


def seed_functions(path):
    """Function starts of every section except ovl_a, which shares ovl_b's addresses."""
    with open(path, "rb") as f:
        dump = tomllib.load(f)
    return sorted(fn["vram"] for sec in dump["section"] if sec["name"] != "ovl_a"
                  for fn in sec.get("functions", []))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", default=os.path.join(REPO, "_roms", "WCW-nWo Revenge (USA).z64"))
    ap.add_argument("--idat", default=r"C:\Program Files\IDA Professional 9.2\idat.exe")
    ap.add_argument("--out", default=os.path.join(REPO, "_tools", "ida_revenge"))
    args = ap.parse_args()

    rom = open(args.rom, "rb").read()
    if hashlib.sha1(rom).hexdigest() != SHA1_US:
        sys.exit("ROM is not WCW/nWo Revenge (USA) .z64")

    os.makedirs(args.out, exist_ok=True)
    image = os.path.join(args.out, "revenge_ovl_b.bin")
    with open(image, "wb") as f:
        f.write(build_image(rom))
    seeds = os.path.join(args.out, "seed_funcs.json")
    with open(seeds, "w", encoding="utf-8") as f:
        json.dump(seed_functions(DUMP), f)
    idb = os.path.join(args.out, "revenge_ovl_b.i64")
    for ext in (".i64", ".id0", ".id1", ".id2", ".nam", ".til"):
        p = os.path.splitext(idb)[0] + ext
        if os.path.exists(p):
            os.remove(p)

    setup = os.path.join(os.path.dirname(os.path.abspath(__file__)), "wcw_idb_setup.py")
    log = os.path.join(args.out, "build.log")
    if os.path.exists(log):
        os.remove(log)  # idat appends to -L logs
    env = dict(os.environ, AKI_GAME="revenge", AKI_SEED_FUNCS=seeds)
    cmd = [args.idat, "-A", "-c", "-pmipsb", "-b%X" % (IMAGE_BASE >> 4), "-L" + log,
           "-S" + setup, "-o" + idb, image]
    print(" ".join('"%s"' % c if " " in c else c for c in cmd))
    r = subprocess.run(cmd, env=env)
    print("idat exit", r.returncode, "->", idb, "(log:", log + ")")
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
