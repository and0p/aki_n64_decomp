"""Record a golden trace from the original game in the emulator.

    py tools/trace/record.py --port 5671 --script tools/trace/scripts/nm_walk_run.json OUT.akt
    py tools/trace/record.py --port 5671 --state _tools/libretro/states/X.state --frames 600 OUT.akt

Start the emulator first, e.g.
    py tools/n64emu/akiemu.py --game nomercy --headless --fast --no-idle-rng --port 5671 --paused

The recorder loads the script's (or --state's) save state, reads the whole RDRAM once (the trace's
image, from which a replay builds its starting state), then for every frame: sets the pads from
the script, runs one game frame and reads the regions in tools/trace/<game>_regions.toml. Record 0
is the state at the start (before the first scripted frame); record k is the state after k frames.
A paused emulator sits between two frames (akiemu counts a frame at each framebuffer flip; the
match logic of a frame is done by then), checked here by the match tick going up by one a frame.

Scripts (JSON): {"game", "state", "note", "steps": [{"frames": N, "pads": {"0": "right+cdown"}}]}.
A pad is a '+'-joined list of buttons (a b z start up down left right l r cup cdown cleft cright)
and optional "sx=..", "sy=.." stick values (akiemu's units: x 416 into the core), or "nx=..",
"ny=.." (the stick exactly as the game reads it, -128..127, y up); ports not named hold nothing. A step may also carry
"poke": [{"addr": "0x8015...", "f32" | "u32" | "u16" | "u8": value}], RAM writes made before its
first frame, to set up a situation pads cannot reach quickly (a test, not the game's own state).

Scripts from akiemu's input recorder (F8 in its window) also carry "load_pads", the pads fed on the
frame step that follows the load (the game reads a pad one tick after it is fed), and "ram_crc",
the CRC of the whole RDRAM at each trace record; the recorder then checks each record's RAM
against it (akiemu /crc) and reports the first difference.
"""
import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools", "recomp_dbg"))
sys.path.insert(0, HERE)
import akidbg  # noqa: E402
import fields  # noqa: E402
import trace  # noqa: E402

BUTTONS = {"a": 0x8000, "b": 0x4000, "z": 0x2000, "start": 0x1000, "up": 0x0800, "down": 0x0400,
           "left": 0x0200, "right": 0x0100, "l": 0x0020, "r": 0x0010, "cup": 0x0008, "cdown": 0x0004,
           "cleft": 0x0002, "cright": 0x0001}
CLOCK = {"nomercy": 0x8016B930}
RAM = 0x800000


def parse_pad(s):
    """(held, sx, sy, exact): exact is the (nx, ny) stick, or None."""
    held, sx, sy, exact = 0, 0, 0, None
    for part in filter(None, s.lower().split("+")):
        if part.startswith("sx="):
            sx = int(part[3:])
        elif part.startswith("sy="):
            sy = int(part[3:])
        elif part[:3] in ("nx=", "ny="):
            exact = list(exact or (0, 0))
            exact[part[1] == "y"] = int(part[3:])
        else:
            held |= BUTTONS[part]
    return held, sx, sy, exact


def set_pads(d, pads):
    for port in range(4):
        held, sx, sy, exact = pads.get(port, (0, 0, 0, None))
        if exact:
            d.call("/pad", port=port, held=held, nx=exact[0], ny=exact[1], frames=1)
        else:
            d.pad(port, held, sx, sy, frames=1)


def match_tick(d, game):
    m, s, f = struct.unpack(">iii", d.read(CLOCK[game], 12))
    return (m * 60 + s) * 30 + f


def record(d, game, steps, out, note="", state=None, inputs=None, strict=True, load_pads=None, ram_crc=None):
    regions = [dict(name=r["name"], addr=r["addr"], size=r["size"]) for r in fields.field_list(game)["regions"]]
    d.pause()
    if state:
        d.load(file=state)
        # The first frame step after a load stops at a flip whose frame had already run before the
        # save; one step lines the pause up with the match frames.
        if load_pads:
            set_pads(d, {int(k): parse_pad(v) for k, v in load_pads.items()})
        d.step(1)
    image = b"".join(d.read(0x80000000 + k, 0x100000) for k in range(0, RAM, 0x100000))
    w = trace.Writer(out, dict(game=game, regions=regions, note=note, state=state, inputs=inputs or steps,
                               mode="emulator"), image)

    ram_bad = []

    def sample():
        st = d.status()
        k = sample.n  # the record this sample is
        if ram_crc is not None and k < len(ram_crc) and d.call("/crc")["crc"] != ram_crc[k]:
            ram_bad.append(k)
        sample.n += 1
        if st.get("ticks_in_frame") not in (None, 1):  # akiemu --tick-hook: two ticks in one step
            print("warning: frame %d: %d match ticks in one step" % (st["frame"], st["ticks_in_frame"]),
                  file=sys.stderr)
        w.frame(match_tick(d, game), st["frame"], b"".join(d.read(r["addr"], r["size"]) for r in regions))
        return match_tick(d, game)

    sample.n = 0
    last = sample()
    n = 0
    for step in steps:
        pads = {int(k): parse_pad(v) for k, v in step.get("pads", {}).items()}
        for poke in step.get("poke", []):
            addr = int(str(poke["addr"]), 0)
            for kind, fmt in (("f32", ">f"), ("u32", ">I"), ("u16", ">H"), ("u8", ">B")):
                if kind in poke:
                    d.write(addr, struct.pack(fmt, poke[kind]))
        for _ in range(step["frames"]):
            set_pads(d, pads)
            d.step(1)
            tick = sample()
            n += 1
            if strict and tick != last + 1:
                print("warning: frame %d: match tick %d after %d (not one frame of play)" % (n, tick, last),
                      file=sys.stderr)
            last = tick
    d.pad_clear()
    w.close()
    if ram_crc is not None:
        checked = min(n + 1, len(ram_crc))
        if ram_bad:
            print("RAM differs from the recording at %d of %d records, first at record %d"
                  % (len(ram_bad), checked, ram_bad[0]), file=sys.stderr)
        else:
            print("RAM matches the recording at all %d records" % checked, file=sys.stderr)
    return n


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--port", type=int, default=akidbg.PORT)
    ap.add_argument("--script")
    ap.add_argument("--state")
    ap.add_argument("--game", default=None)
    ap.add_argument("--frames", type=int, help="with no script: frames to run with no input")
    a = ap.parse_args()
    script = {}
    if a.script:
        with open(a.script, encoding="utf-8") as f:
            script = json.load(f)
    game = a.game or script.get("game", "nomercy")
    state = a.state or script.get("state")
    if state:
        state = os.path.abspath(os.path.join(REPO, state))
    steps = script.get("steps") or [dict(frames=a.frames or 600)]
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    n = record(akidbg.Debugger(port=a.port), game, steps, a.out, script.get("note", ""), state,
               load_pads=script.get("load_pads"), ram_crc=script.get("ram_crc"))
    print("recorded %d frames to %s" % (n, a.out))


if __name__ == "__main__":
    main()
