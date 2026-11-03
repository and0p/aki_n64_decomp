"""Compare two traces field by field and report the first frame that differs.

    py tools/trace/diff.py REF.akt TEST.akt [--from N] [--context 3] [--max 30] [--all]

REF is normally the emulator's trace and TEST a library replay of it (tools: nm_replay). Frames are
matched by index (record k is the state after k frames). Each field is compared by its rule from
tools/trace/fields.py: `exact` and `bits` fail, `unknown` is listed but does not fail, `ignore` is
skipped. A replay names the regions it writes in its header (`owned`); the others are not compared.
The first failing frame is printed with every differing field and the field's last few values in
both traces; later frames are usually consequences, so the rest is only counted (--all lists them).
Exit status 1 when a field fails.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fields as fieldlib  # noqa: E402
import trace  # noqa: E402

FMT = {"u8": "B", "s8": "b", "u16": "H", "s16": "h", "u32": "I", "s32": "i", "f32": "f"}
SIZE = {"u8": 1, "s8": 1, "u16": 2, "s16": 2, "u32": 4, "s32": 4, "f32": 4}


def leaves(regions):
    """Every compared element: (region, name, offset in the frame, type, rule)."""
    out = []
    for r in regions:
        for f in r["fields"]:
            if f["rule"] == "ignore":
                continue
            n = SIZE[f["type"]]
            if f["type"] == "u8" and f["count"] > 1 and f["rule"] == "unknown":
                out.append((r["name"], f["name"], r["frame_off"] + f["off"], "raw%d" % f["count"], f["rule"]))
                continue
            for i in range(f["count"]):
                name = f["name"] + ("[%d]" % i if f["count"] > 1 else "")
                out.append((r["name"], name, r["frame_off"] + f["off"] + i * n, f["type"], f["rule"]))
    return out


def value(buf, off, typ):
    if typ.startswith("raw"):
        return buf[off:off + int(typ[3:])]
    return struct.unpack_from(">" + FMT[typ], buf, off)[0]


def show(v, typ):
    if isinstance(v, bytes):
        return v.hex()
    if typ == "f32":
        return "%r (%08X)" % (v, struct.unpack(">I", struct.pack(">f", v))[0])
    if typ in ("u16", "u32"):
        return "0x%X" % v
    return str(v)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ref")
    ap.add_argument("test")
    ap.add_argument("--from", dest="start", type=int, default=0)
    ap.add_argument("--context", type=int, default=3)
    ap.add_argument("--max", type=int, default=30)
    ap.add_argument("--all", action="store_true", help="list every failing frame, not only the first")
    ap.add_argument("--unknown", action="store_true", help="also print the unknown fields that differ")
    a = ap.parse_args()
    ref, test = trace.read(a.ref), trace.read(a.test)
    game = ref.header["game"]
    regions = fieldlib.field_list(game)["regions"]
    by_name = {r["name"]: r for r in ref.regions}
    owned = set(test.header.get("owned") or [r["name"] for r in regions])
    # a trace recorded before a region was added just lacks it
    regions = [dict(r, frame_off=by_name[r["name"]]["frame_off"]) for r in regions
               if r["name"] in owned and r["name"] in by_name]
    if [(r["name"], r["size"]) for r in ref.regions] != [(r["name"], r["size"]) for r in test.regions]:
        sys.exit("the traces record different regions")
    els = leaves(regions)
    n = min(len(ref.frames), len(test.frames))
    first, failing, unknown_frames = None, 0, 0
    for k in range(a.start, n):
        rb, tb = ref.frames[k][2], test.frames[k][2]
        if rb == tb:
            continue
        bad = [(e, value(rb, e[2], e[3]), value(tb, e[2], e[3])) for e in els
               if value(rb, e[2], e[3]) != value(tb, e[2], e[3])]
        hard = [x for x in bad if x[0][4] != "unknown"]
        if not hard:
            unknown_frames += 1 if bad else 0
            continue
        failing += 1
        if first is None or a.all:
            if first is None:
                first = k
            print("frame %d (tick %d): %d fields differ (%d unknown)" %
                  (k, ref.frames[k][0], len(hard), len(bad) - len(hard)))
            for (reg, name, off, typ, rule), rv, tv in (bad if a.unknown else hard)[:a.max]:
                print("  %-44s ref %-26s test %s%s" % (name, show(rv, typ), show(tv, typ),
                                                       "  [unknown]" if rule == "unknown" else ""))
                if not a.all:
                    for j in range(max(0, k - a.context), k):
                        print("    %4d: ref %-26s test %s" % (j, show(value(ref.frames[j][2], off, typ), typ),
                                                             show(value(test.frames[j][2], off, typ), typ)))
            if len(hard) > a.max:
                print("  ... %d more" % (len(hard) - a.max))
    mode = test.header.get("mode", "?")
    if first is None:
        print("%d frames match (%s; regions %s)%s" % (n - a.start, mode, ", ".join(sorted(owned)),
                                                       "; %d frames differ only in unknown fields" % unknown_frames
                                                       if unknown_frames else ""))
        return 0
    print("first difference at frame %d; %d of %d frames fail (%s)" % (first, failing, n - a.start, mode))
    return 1


if __name__ == "__main__":
    sys.exit(main())
