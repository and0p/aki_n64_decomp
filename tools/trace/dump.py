"""Print chosen fields of a trace frame by frame (to script a trace from a save state).

    py tools/trace/dump.py TRACE.akt [--every 5] [--fields state,anim,pos.x,pos.z,facing] [--slots 0,1]

Field names are matched as suffixes of the region's field names (wrestlers[k].<name>); a field is
printed whenever it changes or every --every frames.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fields as fieldlib  # noqa: E402
import trace  # noqa: E402
from diff import leaves, value, show  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace")
    ap.add_argument("--every", type=int, default=10)
    ap.add_argument("--fields", default="state,state_step,anim,anim_frame,pos.x,pos.z,facing")
    ap.add_argument("--slots", default="0,1")
    a = ap.parse_args()
    t = trace.read(a.trace)
    regions = fieldlib.field_list(t.header["game"])["regions"]
    by_name = {r["name"]: r for r in t.regions}
    regions = [dict(r, frame_off=by_name[r["name"]]["frame_off"]) for r in regions if r["name"] in by_name]
    want = [w.strip() for w in a.fields.split(",")]
    slots = [int(s) for s in a.slots.split(",")]
    els = []
    for e in leaves(regions):
        if e[0] != "wrestlers":
            continue
        for s in slots:
            pre = "[%d]." % s
            if pre in e[1] and any(e[1].endswith(pre + w) or e[1].endswith("." + w) and e[1].split(pre)[1] == w for w in want):
                els.append(e)
    last = None
    for k, fr in enumerate(t.frames):
        buf = fr[2]
        vals = [show(value(buf, e[2], e[3]), e[3]) for e in els]
        if vals != last or k % a.every == 0:
            changed = [i for i, v in enumerate(vals) if last is None or v != last[i]]
            print("%4d: " % k + "  ".join("%s=%s" % (els[i][1], vals[i]) + ("*" if i in changed else "") for i in range(len(els))))
            last = vals


if __name__ == "__main__":
    main()
