"""Record a long match in short chunks, with a save state (a key frame) at the start of each.

    py tools/trace/record_keyframes.py --port 5678 --state _tools/libretro/states/nm_4_fighters_all_cpu.state --chunks 10

Writes _tools/libretro/states/<name>/kNNNN.state and _tools/traces/<name>/kNNNN.akt, each chunk at least --frames long
(150 by default, 5 seconds at 30 ticks a second). A chunk's trace is an ordinary trace from its key frame, so a
difference can be re-recorded or replayed from the key frame before it without running the match from the start.
Rerunning carries on from the last key frame. Input: none, so for all-CPU matches. A chunk's record 0 is the same
moment as the last record of the chunk before.

Saving serializes the core, and that runs the CPU on to a safe point, which can be part way into the next tick: the
RAM then no longer matches the last record, and a state saved there starts a trace mid-tick. So a chunk runs on, a frame
at a time, until a save leaves the regions alone. The extra frames stay exact: the run carries on the same either way.
"""
import argparse
import glob
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(REPO, "tools", "recomp_dbg"))
import akidbg  # noqa: E402
import fields  # noqa: E402
import record as recorder  # noqa: E402
import trace  # noqa: E402


def snapshot(d, regions):
    return [d.read(r["addr"], r["size"]) for r in regions]


def save_clean(d, regions, path):
    """Saves; true when the save left the regions as they were."""
    before = snapshot(d, regions)
    d.save(file=path)
    return snapshot(d, regions) == before


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=akidbg.PORT)
    ap.add_argument("--state", required=True)
    ap.add_argument("--game", default="nomercy")
    ap.add_argument("--chunks", type=int, default=10)
    ap.add_argument("--frames", type=int, default=150)
    ap.add_argument("--name")
    a = ap.parse_args()
    source = os.path.abspath(os.path.join(REPO, a.state))
    name = a.name or os.path.splitext(os.path.basename(source))[0]
    state_dir = os.path.join(REPO, "_tools", "libretro", "states", name)
    trace_dir = os.path.join(REPO, "_tools", "traces", name)
    os.makedirs(state_dir, exist_ok=True)
    os.makedirs(trace_dir, exist_ok=True)
    d = akidbg.Debugger(port=a.port)
    regions = [dict(name=r["name"], addr=r["addr"], size=r["size"]) for r in fields.field_list(a.game)["regions"]]
    k = len(glob.glob(os.path.join(trace_dir, "k*.akt")))
    state = os.path.join(state_dir, "k%04d.state" % k)
    if k == 0:
        d.pause()
        d.load(file=source)
        d.step(1)  # a state saved elsewhere: lines the pause up with the match frames (record.py)
        while not save_clean(d, regions, state):
            d.step(1)
    for _ in range(a.chunks):
        out = os.path.join(trace_dir, "k%04d.akt" % k)
        next_state = os.path.join(state_dir, "k%04d.state" % (k + 1))
        n = recorder.record(d, a.game, [dict(frames=a.frames)], out, "key frame %d of %s" % (k, name), state, align=False,
                            more=lambda regs: not save_clean(d, regs, next_state))
        t = trace.read(out)
        last = t.region(n, "wrestlers")
        st = [struct.unpack(">ii", last[856 * s + 0x14:856 * s + 0x1C]) for s in range(4)]
        print("k%04d: %d frames, states %s" % (k, n, " ".join("%02X/%d" % x for x in st)))
        k += 1
        state = next_state
        d.pause()
        d.load(file=state)
        now = d.read(t.regions[0]["addr"], t.regions[0]["size"])
        if now != last:
            print("  key frame k%04d differs from the chunk's end after loading" % k, file=sys.stderr)


if __name__ == "__main__":
    main()
