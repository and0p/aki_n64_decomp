"""Build a MIPS IDA database for WCW vs. nWo: World Tour (US) from the ROM.

Lays the main segment and the match overlay (ovl_b) out at their N64 addresses in a flat
image, then runs idat headless with wcw_idb_setup.py to split segments, find functions,
apply _re_docs/symbols/wcw and export xrefs/pseudocode for tools/sym. The database is
ROM-derived, so it goes under _tools/ (git-ignored). Usually run via
`py tools/sym/build.py --ida`.

    py tools/ida/wcw_build_idb.py [--rom ROM] [--idat PATH] [--out DIR]
"""
import argparse
import hashlib
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHA1_US = "5ad2d8359058c8bb71f08e3d3433b7a50d3bb645"

IMAGE_BASE = 0x80000000
IMAGE_END = 0x80100000  # covers ovl_b bss (ends 0x800F8AC0)

# (rom_start, rom_end, vram) from the recomp's splat config (disasm/wcw.yaml)
PIECES = [
    (0x001000, 0x0393F0, 0x80000400),  # entry + main text/data
    (0xA69570, 0xAC2970, 0x80090000),  # ovl_b (match overlay) text/data
]


def build_image(rom):
    img = bytearray(IMAGE_END - IMAGE_BASE)
    for start, end, vram in PIECES:
        off = vram - IMAGE_BASE
        img[off:off + end - start] = rom[start:end]
    return bytes(img)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", default=os.path.join(REPO, "_tools", "WCWRecompiled", "wcw.nwo.worldtour.us.z64"))
    ap.add_argument("--idat", default=r"C:\Program Files\IDA Professional 9.2\idat.exe")
    ap.add_argument("--out", default=os.path.join(REPO, "_tools", "ida_wcw"))
    args = ap.parse_args()

    rom = open(args.rom, "rb").read()
    if hashlib.sha1(rom).hexdigest() != SHA1_US:
        sys.exit("ROM is not WCW vs. nWo: World Tour (USA) .z64")

    os.makedirs(args.out, exist_ok=True)
    image = os.path.join(args.out, "wcw_ovl_b.bin")
    with open(image, "wb") as f:
        f.write(build_image(rom))
    idb = os.path.join(args.out, "wcw_ovl_b.i64")
    for ext in (".i64", ".id0", ".id1", ".id2", ".nam", ".til"):
        p = os.path.splitext(idb)[0] + ext
        if os.path.exists(p):
            os.remove(p)

    setup = os.path.join(os.path.dirname(os.path.abspath(__file__)), "wcw_idb_setup.py")
    log = os.path.join(args.out, "build.log")
    if os.path.exists(log):
        os.remove(log)  # idat appends to -L logs
    cmd =[args.idat, "-A", "-c", "-pmipsb", "-b%X" % (IMAGE_BASE >> 4), "-L" + log,
           "-S" + setup, "-o" + idb, image]
    print(" ".join('"%s"' % c if " " in c else c for c in cmd))
    r = subprocess.run(cmd)
    print("idat exit", r.returncode, "->", idb, "(log:", log + ")")
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
