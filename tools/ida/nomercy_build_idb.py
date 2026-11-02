"""Build a MIPS IDA database for WWF No Mercy (USA, Rev 1) from the ROM.

Same pipeline as revenge_build_idb.py: lays the main segment and the match overlay (ovl_e) out
at their N64 addresses in a flat image and runs idat headless with wcw_idb_setup.py
(AKI_GAME=nomercy). Function starts are seeded from the No Mercy recomp's symbol dump
(_recomps/WWFNoMercyRecomp/syms/dump.toml, the main and ovl_e sections). The recomp and these
symbols are for Rev 1; Rev 0 is laid out differently and is refused. The database is
ROM-derived, so it goes under _tools/ (git-ignored). Usually run via
`py tools/sym/build.py --game nomercy --ida`.

    py tools/ida/nomercy_build_idb.py [--rom ROM] [--idat PATH] [--out DIR]
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import tomllib

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHA1_US = "91cee3d096f4a76644d8b35b9aead6448909abd1"  # Rev 1
DUMP = os.path.join(REPO, "_recomps", "WWFNoMercyRecomp", "syms", "dump.toml")

IMAGE_BASE = 0x80000000
IMAGE_END = 0x80170000  # covers ovl_e bss (ends 0x8016B9F0)

# (rom_start, rom_end, vram) from the recomp's splat config (disasm/nomercy.yaml). ovl_e holds
# rng_match (LCG x0x5D588B65), so it is taken as the match overlay; it spans both overlay slots,
# so ovl_a-ovl_d (which share its addresses) are left out.
PIECES = [
    (0x001000, 0x057270, 0x80000400),  # entry + main text and data, up to the bss at 0x80056670
    (0x144BE0, 0x1BD150, 0x800D9960),  # ovl_e (match overlay) text/data
]


def build_image(rom):
    img = bytearray(IMAGE_END - IMAGE_BASE)
    for start, end, vram in PIECES:
        off = vram - IMAGE_BASE
        img[off:off + end - start] = rom[start:end]
    return bytes(img)


def seed_functions(path):
    """Function starts of main and ovl_e (the other overlays share ovl_e's addresses)."""
    with open(path, "rb") as f:
        dump = tomllib.load(f)
    return sorted(fn["vram"] for sec in dump["section"] if sec["name"] in ("entry", "main_1050", "ovl_e")
                  for fn in sec.get("functions", []))


def seed_sizes(path):
    """{start: size} for the same sections, for starts IDA will not turn into functions by itself."""
    with open(path, "rb") as f:
        dump = tomllib.load(f)
    return {str(fn["vram"]): fn["size"] for sec in dump["section"] if sec["name"] in ("entry", "main_1050", "ovl_e")
            for fn in sec.get("functions", [])}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", default=os.path.join(REPO, "_roms", "WWF No Mercy (USA) (Rev A).z64"))
    ap.add_argument("--idat", default=r"C:\Program Files\IDA Professional 9.2\idat.exe")
    ap.add_argument("--out", default=os.path.join(REPO, "_tools", "ida_nomercy"))
    args = ap.parse_args()

    rom = open(args.rom, "rb").read()
    if hashlib.sha1(rom).hexdigest() != SHA1_US:
        sys.exit("ROM is not WWF No Mercy (USA) Rev 1 .z64 (Rev 0 is not supported)")

    os.makedirs(args.out, exist_ok=True)
    image = os.path.join(args.out, "nomercy_ovl_e.bin")
    with open(image, "wb") as f:
        f.write(build_image(rom))
    seeds = os.path.join(args.out, "seed_funcs.json")
    with open(seeds, "w", encoding="utf-8") as f:
        json.dump(seed_functions(DUMP), f)
    sizes = os.path.join(args.out, "seed_sizes.json")
    with open(sizes, "w", encoding="utf-8") as f:
        json.dump(seed_sizes(DUMP), f)
    idb = os.path.join(args.out, "nomercy_ovl_e.i64")
    for ext in (".i64", ".id0", ".id1", ".id2", ".nam", ".til"):
        p = os.path.splitext(idb)[0] + ext
        if os.path.exists(p):
            os.remove(p)

    setup = os.path.join(os.path.dirname(os.path.abspath(__file__)), "wcw_idb_setup.py")
    log = os.path.join(args.out, "build.log")
    if os.path.exists(log):
        os.remove(log)  # idat appends to -L logs
    env = dict(os.environ, AKI_GAME="nomercy", AKI_SEED_FUNCS=seeds, AKI_SEED_SIZES=sizes)
    cmd = [args.idat, "-A", "-c", "-pmipsb", "-b%X" % (IMAGE_BASE >> 4), "-L" + log,
           "-S" + setup, "-o" + idb, image]
    print(" ".join('"%s"' % c if " " in c else c for c in cmd))
    r = subprocess.run(cmd, env=env)
    print("idat exit", r.returncode, "->", idb, "(log:", log + ")")
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
