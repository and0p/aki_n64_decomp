"""Client for the aki_debug server in a running WCWRecompiled (see README.md).

Works with both backends: the recomp's aki_debug mod and the emulator (tools/n64emu/akiemu.py),
which also has vi_step and screenshot. Set AKIDBG_PORT to talk to another port.

As a CLI:
    py tools/recomp_dbg/akidbg.py status
    py tools/recomp_dbg/akidbg.py pause
    py tools/recomp_dbg/akidbg.py step 10
    py tools/recomp_dbg/akidbg.py read 0x800F07F0 0x40
    py tools/recomp_dbg/akidbg.py u16 0x800F16EC
    py tools/recomp_dbg/akidbg.py write 0x800F0800 0001
    py tools/recomp_dbg/akidbg.py break 0x800F16EC 2 change
    py tools/recomp_dbg/akidbg.py save a [file]   /   load a
    py tools/recomp_dbg/akidbg.py resume

As a library:
    import sys; sys.path.insert(0, "tools/recomp_dbg"); import akidbg
    d = akidbg.Debugger(); d.pause(); d.u16(0x800F16EC); d.step(1)
"""
import json
import os
import struct
import sys
import urllib.error
import time
import urllib.parse
import urllib.request

PORT = int(os.environ.get("AKIDBG_PORT", "5664"))


class DebuggerError(Exception):
    pass


class Debugger:
    def __init__(self, host="127.0.0.1", port=PORT, timeout=30):
        self.base = "http://%s:%d" % (host, port)
        self.timeout = timeout

    def call(self, path, raw=False, data=None, **q):
        wait = max(self.timeout, int(q.get("timeout", 0)) / 1000 + 5)
        q = {k: (hex(v) if isinstance(v, int) and not isinstance(v, bool) else str(v)) for k, v in q.items()}
        url = self.base + path + ("?" + urllib.parse.urlencode(q) if q else "")
        # One connection a request: a long recording can run out of local ports (WinError 10048) until
        # closed ones time out, so that one is waited out.
        for attempt in range(180):
            try:
                with urllib.request.urlopen(url, data=data, timeout=wait) as r:
                    body = r.read()
                break
            except urllib.error.HTTPError as e:
                raise DebuggerError(json.loads(e.read()).get("error", str(e))) from None
            except urllib.error.URLError as e:
                if getattr(e.reason, "winerror", None) == 10048 and attempt < 179:
                    time.sleep(1)
                    continue
                raise DebuggerError("no aki_debug server at %s (%s)" % (self.base, e.reason)) from None
        return body if raw else json.loads(body)

    # Control. Each returns the status dict (frame, paused, reason, ...).
    def status(self): return self.call("/status")
    def pause(self, timeout_ms=10000): return self.call("/pause", timeout=timeout_ms)
    def resume(self): return self.call("/resume")
    # Waits for the game to stop again; the default timeout allows ~10 game frames per second.
    def step(self, n=1, timeout_ms=None):
        return self.call("/step", n=n, timeout=timeout_ms or 10000 + 100 * n)
    def run_to(self, frame, timeout_ms=None):
        ahead = max(0, frame - self.status()["frame"])
        return self.call("/run", frame=frame, timeout=timeout_ms or 10000 + 100 * ahead)

    # Memory. Addresses are N64 virtual (0x80xxxxxx) or physical; data is big-endian as on the N64.
    def read(self, addr, n): return self.call("/read", raw=True, addr=addr, len=n, binary=1)
    def write(self, addr, data): return self.call("/write", data=bytes(data), addr=addr)
    def u8(self, a): return self.read(a, 1)[0]
    def s8(self, a): return struct.unpack(">b", self.read(a, 1))[0]
    def u16(self, a): return struct.unpack(">H", self.read(a, 2))[0]
    def s16(self, a): return struct.unpack(">h", self.read(a, 2))[0]
    def u32(self, a): return struct.unpack(">I", self.read(a, 4))[0]
    def s32(self, a): return struct.unpack(">i", self.read(a, 4))[0]
    def f32(self, a): return struct.unpack(">f", self.read(a, 4))[0]

    # Breakpoints: pause at the start of the first frame where the value matches.
    def break_eq(self, addr, size, value): return self.call("/break", addr=addr, size=size, eq=value)
    def break_ne(self, addr, size, value): return self.call("/break", addr=addr, size=size, ne=value)
    def break_change(self, addr, size): return self.call("/break", addr=addr, size=size, change=1)
    def break_clear(self): return self.call("/break", clear=1)

    # Controller override for port 0..3 (N64 button bits, stick -128..127), applied after the
    # game reads its pads. frames=None holds until pad_clear().
    def pad(self, port, held=0, sx=0, sy=0, frames=None):
        q = dict(port=port, held=held, sx=sx & 0xFF, sy=sy & 0xFF)
        if frames is not None:
            q["frames"] = frames
        return self.call("/pad", **q)
    def pad_clear(self): return self.call("/pad", clear=1)

    # Snapshots (only while paused). slot keeps it in memory; file also writes it to disk.
    def save(self, slot=None, file=None): return self.call("/save", **_opt(slot=slot, file=file))
    def load(self, slot=None, file=None): return self.call("/load", **_opt(slot=slot, file=file))
    def drop(self, slot): return self.call("/drop", slot=slot)
    def regs(self): return self.call("/regs")
    def quit(self): return self.call("/quit")

    # Emulator backend (tools/n64emu/akiemu.py) only.
    def vi_step(self, n=1): return self.call("/vi_step", n=n)
    def screenshot(self, path):
        with open(path, "wb") as f:
            f.write(self.call("/screenshot", raw=True))


def _opt(**kw):
    return {k: (os.path.abspath(v) if k == "file" else v) for k, v in kw.items() if v is not None}


def _num(s):
    return int(s, 0)


def main(argv):
    if not argv or argv[0] in ("-h", "--help", "help"):
        print(__doc__)
        return 0
    d = Debugger()
    cmd, args = argv[0], argv[1:]
    try:
        if cmd in ("status", "pause", "resume", "regs", "break_clear", "pad_clear", "quit"):
            r = getattr(d, cmd)()
        elif cmd == "step":
            r = d.step(_num(args[0]) if args else 1)
        elif cmd == "run":
            r = d.run_to(_num(args[0]))
        elif cmd == "read":
            a, n = _num(args[0]), _num(args[1]) if len(args) > 1 else 16
            b = d.read(a, n)
            for k in range(0, n, 16):
                print("%08x  %s" % (a + k, b[k:k + 16].hex(" ")))
            return 0
        elif cmd in ("u8", "s8", "u16", "s16", "u32", "s32", "f32"):
            v = getattr(d, cmd)(_num(args[0]))
            print(v if cmd in ("s8", "s16", "s32", "f32") else "%d (0x%X)" % (v, v))
            return 0
        elif cmd == "write":
            r = d.write(_num(args[0]), bytes.fromhex(args[1]))
        elif cmd == "break":
            a, size, kind = _num(args[0]), _num(args[1]), args[2]
            r = d.break_change(a, size) if kind == "change" else \
                (d.break_eq if kind == "eq" else d.break_ne)(a, size, _num(args[3]))
        elif cmd == "pad":
            r = d.pad(_num(args[0]), _num(args[1]) if len(args) > 1 else 0,
                      frames=_num(args[2]) if len(args) > 2 else None)
        elif cmd in ("save", "load"):
            r = getattr(d, cmd)(args[0] if args and args[0] != "-" else None, args[1] if len(args) > 1 else None)
        elif cmd == "screenshot":
            d.screenshot(args[0])
            print("wrote", args[0])
            return 0
        elif cmd == "vi_step":
            r = d.vi_step(_num(args[0]) if args else 1)
        elif cmd == "drop":
            r = d.drop(args[0])
        else:
            print("unknown command %r; try --help" % cmd, file=sys.stderr)
            return 2
    except DebuggerError as e:
        print("error:", e, file=sys.stderr)
        return 1
    print(json.dumps(r, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
