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
# --overlay c: the menu overlay ovl_c (match_rules_build and the match set-up menus) instead of ovl_e,
# in the second slot: text 0x80106760-0x80147EE0, data to 0x80155620, bss to 0x8015BA00.
PIECES_C = [
    (0x001000, 0x057270, 0x80000400),
    (0x0AE390, 0x0FD250, 0x80106760),  # ovl_c text/data
]
# --overlay b: the story-mode overlay ovl_b (the Championship script interpreter) in the second slot:
# text 0x80106760-0x8011EBD0, data to 0x801338E0, bss to 0x8016CDD0.
PIECES_B = [
    (0x001000, 0x057270, 0x80000400),
    (0x081210, 0x0AE390, 0x80106760),  # ovl_b text/data
]


def build_image(rom, pieces=PIECES):
    img = bytearray(IMAGE_END - IMAGE_BASE)
    for start, end, vram in pieces:
        off = vram - IMAGE_BASE
        img[off:off + end - start] = rom[start:end]
    return bytes(img)


def seed_functions(path, overlay="e"):
    """Function starts of main and the chosen overlay (the other overlays share its addresses)."""
    with open(path, "rb") as f:
        dump = tomllib.load(f)
    secs = ("entry", "main_1050", "ovl_" + overlay)
    return sorted(fn["vram"] for sec in dump["section"] if sec["name"] in secs
                  for fn in sec.get("functions", []))


def seed_sizes(path, overlay="e"):
    """{start: size} for the same sections, for starts IDA will not turn into functions by itself."""
    with open(path, "rb") as f:
        dump = tomllib.load(f)
    secs = ("entry", "main_1050", "ovl_" + overlay)
    return {str(fn["vram"]): fn["size"] for sec in dump["section"] if sec["name"] in secs
            for fn in sec.get("functions", [])}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", default=os.path.join(REPO, "_roms", "WWF No Mercy (USA) (Rev A).z64"))
    ap.add_argument("--idat", default=r"C:\Program Files\IDA Professional 9.2\idat.exe")
    ap.add_argument("--out", default=None, help="output folder (default _tools/ida_nomercy, or _tools/ida_nomercy_c)")
    ap.add_argument("--overlay", choices=("e", "c", "b"), default="e",
                    help="which overlay to lay out beside main: e (the match, default), c (the menus) or b (the story)")
    args = ap.parse_args()
    if args.out is None:
        args.out = os.path.join(REPO, "_tools", "ida_nomercy" + ("" if args.overlay == "e" else "_" + args.overlay))
    pieces = {"e": PIECES, "c": PIECES_C, "b": PIECES_B}[args.overlay]

    rom = open(args.rom, "rb").read()
    if hashlib.sha1(rom).hexdigest() != SHA1_US:
        sys.exit("ROM is not WWF No Mercy (USA) Rev 1 .z64 (Rev 0 is not supported)")

    os.makedirs(args.out, exist_ok=True)
    image = os.path.join(args.out, "nomercy_ovl_%s.bin" % args.overlay)
    with open(image, "wb") as f:
        f.write(build_image(rom, pieces))
    seeds = os.path.join(args.out, "seed_funcs.json")
    with open(seeds, "w", encoding="utf-8") as f:
        json.dump(seed_functions(DUMP, args.overlay), f)
    sizes = os.path.join(args.out, "seed_sizes.json")
    with open(sizes, "w", encoding="utf-8") as f:
        json.dump(seed_sizes(DUMP, args.overlay), f)
    idb = os.path.join(args.out, "nomercy_ovl_%s.i64" % args.overlay)
    for ext in (".i64", ".id0", ".id1", ".id2", ".nam", ".til"):
        p = os.path.splitext(idb)[0] + ext
        if os.path.exists(p):
            os.remove(p)

    setup = os.path.join(os.path.dirname(os.path.abspath(__file__)), "wcw_idb_setup.py")
    log = os.path.join(args.out, "build.log")
    if os.path.exists(log):
        os.remove(log)  # idat appends to -L logs
    env = dict(os.environ, AKI_GAME="nomercy", AKI_OVERLAY=args.overlay, AKI_SEED_FUNCS=seeds, AKI_SEED_SIZES=sizes)
    cmd = [args.idat, "-A", "-c", "-pmipsb", "-b%X" % (IMAGE_BASE >> 4), "-L" + log,
           "-S" + setup, "-o" + idb, image]
    print(" ".join('"%s"' % c if " " in c else c for c in cmd))
    r = subprocess.run(cmd, env=env)
    print("idat exit", r.returncode, "->", idb, "(log:", log + ")")
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
