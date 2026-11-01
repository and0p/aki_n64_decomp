"""Read the asset archive of WCW vs. nWo: World Tour or WCW/nWo Revenge (US) and unpack its files.

Format notes: _re_docs/file_formats/asset_archive.md

    python tools/rom/aki_archive.py ROM list
    python tools/rom/aki_archive.py ROM extract OUT_DIR [FILE ...]
    python tools/rom/aki_archive.py ROM verify
    python tools/rom/aki_archive.py ROM survey
"""
import argparse
import hashlib
import os
import struct
import sys

# Per game (US releases, by ROM SHA1): file offsets are relative to data_base; the offset table
# holds a u32 per file plus one end entry (bit 0 = compressed); files are numbered 1..count.
LAYOUTS = {
    "5ad2d8359058c8bb71f08e3d3433b7a50d3bb645": dict(game="wcw", data_base=0x39490, table=0x7C1A78, count=5498),
    # Revenge: table at ROM 0xCE2752 (DMA'd by func_800034A0), data at 0xDAC50 (file loader func_80003684)
    "e1711a2511394b9357b5f1ac8ca5cc17bd674836": dict(game="revenge", data_base=0xDAC50, table=0xCE2752, count=7657),
}
DATA_BASE = 0x39490      # World Tour values, kept for older callers
OFFSET_TABLE = 0x7C1A78
FILE_COUNT = 5498


def lzss_decompress(src):
    """LZSS as used by func_80004E8C: 4-byte big-endian unpacked size, then flag bytes
    (LSB first, 1 = literal byte, 0 = 2-byte back reference into a 4 KB window that
    starts at 0xFEE and is filled with zeros)."""
    size = struct.unpack_from(">I", src, 0)[0]
    out = bytearray()
    i = 4
    flags = 0
    while len(out) < size:
        flags >>= 1
        if not flags & 0x100:
            flags = src[i] | 0xFF00
            i += 1
        if flags & 1:
            out.append(src[i])
            i += 1
            continue
        b0, b1 = src[i], src[i + 1]
        i += 2
        pos = b0 | ((b1 & 0xF0) << 4)
        length = (b1 & 0x0F) + 3
        # window position -> distance back from the current output position
        rel = ((pos + 0x12) & 0xFFF) - (len(out) & 0xFFF)
        if rel >= 0:
            rel -= 0x1000
        start = len(out) + rel
        for k in range(length):
            p = start + k
            out.append(out[p] if p >= 0 else 0)
    return bytes(out[:size])


class Archive:
    def __init__(self, rom):
        self.rom = rom
        layout = LAYOUTS.get(hashlib.sha1(rom).hexdigest())
        if layout is None:
            raise ValueError("unknown ROM (expected a US World Tour or Revenge .z64)")
        self.game = layout["game"]
        self.data_base, self.table, self.count = layout["data_base"], layout["table"], layout["count"]

    def entry(self, n):
        """(rom_offset, packed_length, compressed, unpacked_size) of file n (1-based)."""
        if not 1 <= n <= self.count:
            raise IndexError(n)
        a, b = struct.unpack_from(">II", self.rom, self.table + 4 * (n - 1))
        start = (a & ~1) + self.data_base
        end = (b & ~1) + self.data_base
        compressed = bool(a & 1)
        # The game keeps a size table in RAM; it matches the LZSS header (or the packed
        # length for raw files), so it is derived here instead of read.
        size = struct.unpack_from(">I", self.rom, start)[0] if compressed else end - start
        return start, end - start, compressed, size

    def read(self, n):
        start, length, compressed, size = self.entry(n)
        raw = self.rom[start:start + length]
        if compressed:
            return lzss_decompress(raw)
        return raw


# Known fixed ranges (see _re_docs/file_formats/asset_archive.md)
MOVESET_FIRST, MOVESET_LAST = 222, 617  # 99 groups of 4 files
ANIM_META = 618
ANIM_FIRST = 2901                       # animation id == file number


def classify(n, data, game="wcw"):
    """Best-effort file type from the file number (World Tour only) and its contents."""
    if game != "wcw":
        n = 0
    if n >= ANIM_FIRST:
        return "anim"
    if n and MOVESET_FIRST <= n <= MOVESET_LAST:
        return ("moveset_hdr", "moveset_moves", "moveset_anims", "moveset_x60")[(n - MOVESET_FIRST) % 4]
    if n == ANIM_META:
        return "anim_meta"
    if data[:4] == b"TEX\0":
        return "TEX"
    if len(data) >= 8:
        w1, h1, lw, lh = data[0], data[1], data[6], data[7]
        if lw < 10 and lh < 10 and (1 << lw) == w1 + 1 and (1 << lh) == h1 + 1:
            px = (w1 + 1) * (h1 + 1)
            for bpp in (4, 8, 16, 32):
                if len(data) - 8 == px * bpp // 8:
                    return "tex_%dbpp" % bpp
    if len(data) in (0x20, 0x200):
        return "palette%d" % (len(data) // 2)
    return "unknown"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    ex = sub.add_parser("extract")
    ex.add_argument("out_dir")
    ex.add_argument("files", nargs="*", type=int)
    sub.add_parser("verify")
    sub.add_parser("survey")
    args = ap.parse_args()

    with open(args.rom, "rb") as f:
        arc = Archive(f.read())

    if args.cmd == "list":
        for n in range(1, arc.count + 1):
            start, length, comp, size = arc.entry(n)
            print("%4d  rom %06X  packed %6X  %s  size %6X" % (n, start, length, "lzss" if comp else "raw ", size))
    elif args.cmd == "extract":
        os.makedirs(args.out_dir, exist_ok=True)
        for n in args.files or range(1, arc.count + 1):
            with open(os.path.join(args.out_dir, "%04d.bin" % n), "wb") as f:
                f.write(arc.read(n))
    elif args.cmd == "verify":
        bad = 0
        for n in range(1, arc.count + 1):
            start, length, comp, size = arc.entry(n)
            try:
                data = arc.read(n)
            except IndexError:
                data = b""
            if len(data) != size:
                bad += 1
                print("file %d: got %X bytes, table says %X" % (n, len(data), size))
        print("%d files, %d mismatched" % (arc.count, bad))
        return 1 if bad else 0
    elif args.cmd == "survey":
        # runs of consecutive files with the same type
        run = None
        for n in range(1, arc.count + 2):
            kind = classify(n, arc.read(n), arc.game) if n <= arc.count else None
            if run and kind != run[1]:
                print("%4d-%4d  %-14s x%d" % (run[0], n - 1, run[1], n - run[0]))
                run = None
            if run is None and kind:
                run = (n, kind)


if __name__ == "__main__":
    sys.exit(main())
