"""Build a MIPS IDA database for WWF WrestleMania 2000 (USA, V1.2) from the ROM.

Same pipeline as nomercy_build_idb.py: lays the main segment and the match overlay (ovl_d) out
at their N64 addresses in a flat image and runs idat headless with wcw_idb_setup.py
(AKI_GAME=wm2000). Function starts are seeded from the WrestleMania 2000 recomp's symbol dump
(_recomps/WWFWrestleMania2000Recomp/syms/dump.toml, the main and ovl_d sections). The recomp and
these symbols are for V1.2; V1.0 is laid out slightly differently and is refused. The database is
ROM-derived, so it goes under _tools/ (git-ignored). Usually run via
`py tools/sym/build.py --game wm2000 --ida`.

    py tools/ida/wm2000_build_idb.py [--rom ROM] [--idat PATH] [--out DIR]
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import tomllib

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHA1_US = "d7d1fad473fef9b61fe5f8273c975ee7c603a51b"  # V1.2
DUMP = os.path.join(REPO, "_recomps", "WWFWrestleMania2000Recomp", "syms", "dump.toml")

IMAGE_BASE = 0x80000000
IMAGE_END = 0x80170000  # covers ovl_d bss (ends 0x8016F170)

# (rom_start, rom_end, vram) from the recomp's splat config (disasm/wm2k.yaml). ovl_d holds
# rng_match (LCG x0x5D588B65), so it is taken as the match overlay; it spans both overlay slots,
# so ovl_a-ovl_c (which share its addresses) are left out.
PIECES = [
    (0x001000, 0x04C0C0, 0x80000400),  # entry + main text and data, up to the bss at 0x8004B4C0
    (0x0D2720, 0x144AA0, 0x800E1B90),  # ovl_d (match overlay) text/data
]


def build_image(rom):
    img = bytearray(IMAGE_END - IMAGE_BASE)
    for start, end, vram in PIECES:
        off = vram - IMAGE_BASE
        img[off:off + end - start] = rom[start:end]
    return bytes(img)


def seed_functions(path):
    """Function starts of main and ovl_d (the other overlays share ovl_d's addresses)."""
    with open(path, "rb") as f:
        dump = tomllib.load(f)
    return sorted(fn["vram"] for sec in dump["section"] if sec["name"] in ("entry", "main_1050", "main_3A1D8", "main_3C0D0", "ovl_d")
                  for fn in sec.get("functions", []))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", default=os.path.join(REPO, "_roms", "WWF WrestleMania 2000 (USA) (V1.2).z64"))
    ap.add_argument("--idat", default=r"C:\Program Files\IDA Professional 9.2\idat.exe")
    ap.add_argument("--out", default=os.path.join(REPO, "_tools", "ida_wm2000"))
    args = ap.parse_args()

    rom = open(args.rom, "rb").read()
    if hashlib.sha1(rom).hexdigest() != SHA1_US:
        sys.exit("ROM is not WWF WrestleMania 2000 (USA) V1.2 .z64 (V1.0 is not supported)")

    os.makedirs(args.out, exist_ok=True)
    image = os.path.join(args.out, "wm2000_ovl_d.bin")
    with open(image, "wb") as f:
        f.write(build_image(rom))
    seeds = os.path.join(args.out, "seed_funcs.json")
    with open(seeds, "w", encoding="utf-8") as f:
        json.dump(seed_functions(DUMP), f)
    idb = os.path.join(args.out, "wm2000_ovl_d.i64")
    for ext in (".i64", ".id0", ".id1", ".id2", ".nam", ".til"):
        p = os.path.splitext(idb)[0] + ext
        if os.path.exists(p):
            os.remove(p)

    setup = os.path.join(os.path.dirname(os.path.abspath(__file__)), "wcw_idb_setup.py")
    log = os.path.join(args.out, "build.log")
    if os.path.exists(log):
        os.remove(log)  # idat appends to -L logs
    env = dict(os.environ, AKI_GAME="wm2000", AKI_SEED_FUNCS=seeds)
    cmd = [args.idat, "-A", "-c", "-pmipsb", "-b%X" % (IMAGE_BASE >> 4), "-L" + log,
           "-S" + setup, "-o" + idb, image]
    print(" ".join('"%s"' % c if " " in c else c for c in cmd))
    r = subprocess.run(cmd, env=env)
    print("idat exit", r.returncode, "->", idb, "(log:", log + ")")
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
