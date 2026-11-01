"""akiemu: World Tour in the libretro mupen64plus_next core, with the aki_debug HTTP API.

Runs the original game in an emulator (so save states are exact), headless or in a simple
window, and serves the same API as the recomp's aki_debug mod on 127.0.0.1 (default port 5664):
pause, step, read/write RAM, value breakpoints, pad overrides and save states. See README.md.

    py tools\\n64emu\\akiemu.py                         # window, boots to the title screen
    py tools\\n64emu\\akiemu.py --quickmatch            # walks the menus to a CPU-vs-CPU match
    py tools\\n64emu\\akiemu.py --headless --state _tools\\libretro\\states\\pin.state --paused
"""
import argparse
import ctypes as C
import http.server
import json
import os
import struct
import sys
import threading
import time
import urllib.parse
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import libretro  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
TOOLS = os.path.join(REPO, "_tools", "libretro")
DEFAULT_ROM = os.path.join(REPO, "_roms", "WCW vs. nWo - World Tour (USA).z64")
RAM_SIZE = 0x800000

# World Tour addresses (see _re_docs). A game frame is one pass of the frame loop: the game logic
# runs at 30 fps, every second VI. The graphics thread flips to the next of its three framebuffers
# once per game frame (index at 0x8002D2B0, 0..2), in the menus and in a match, so a change of that
# index marks a new game frame. (Measured in a match: 59 flips in 120 VIs, 60 match ticks.)
FB_INDEX = 0x8002D2B0

# --no-idle-rng: the idle thread calls rng_main in a busy loop (`jal rng_main` at 0x80000584), so
# on hardware and here its sequence depends on spare CPU time. The recomp patched that loop to
# draw once; with this option the call becomes a NOP, so rng_main only advances when game code
# draws, like in the recomp and the library (_re_docs/state_comparison.md).
IDLE_RNG_CALL = 0x80000584
JAL_RNG_MAIN = 0x0C0046FC  # jal 0x80011BF0

# --game revenge: the same three addresses in WCW/nWo Revenge (found 2026-11-01 from its code:
# the graphics thread entry 0x80002CC4 keeps the framebuffer index at 0x8002EEC8, and the idle
# thread's loop calls rng_main 0x8001C580 at 0x80000560). There is no quick match for Revenge.
GAMES = {
    "wcw": dict(rom="WCW vs. nWo - World Tour (USA).z64", title="WCW vs. nWo World Tour",
                FB_INDEX=0x8002D2B0, IDLE_RNG_CALL=0x80000584, JAL_RNG_MAIN=0x0C0046FC),
    "revenge": dict(rom="WCW-nWo Revenge (USA).z64", title="WCW/nWo Revenge",
                    FB_INDEX=0x8002EEC8, IDLE_RNG_CALL=0x80000560, JAL_RNG_MAIN=0x0C007160),
}
GAME_TITLE = GAMES["wcw"]["title"]

# N64 button bit -> RetroPad joypad id, as mupen64plus_next maps them by default.
N64_TO_RETROPAD = {0x8000: 0, 0x4000: 1, 0x2000: 12, 0x1000: 3, 0x0800: 4, 0x0400: 5, 0x0200: 6,
                   0x0100: 7, 0x0020: 10, 0x0010: 11}
# C buttons come from the right analog stick: bit -> (axis, sign).
N64_C_BUTTONS = {0x0008: (1, -1), 0x0004: (1, 1), 0x0002: (0, -1), 0x0001: (0, 1)}

# Quick match: the menu walk of the recomp's Quick Match mod (AkiMods mods/quickmatch, World Tour).
QM_MODE = 0x80034820          # s32: nonzero while leaving the menus
QM_MENU_SIG = 0x800A9910      # ovl_a's menu state machine is loaded when this word is...
QM_MENU_SIG_WORD = 0x3C04800E
QM_MENU_SCREEN = 0x80034824   # s16 screen id
QM_MENU_SCREENS = 0x800DD430  # screen records, 0x138 bytes each, +5 = cursor
QM_MENU_STRIDE = 0x138
QM_VENUE_ADDR = 0x8038311C    # rules screen (17): arena/mat, also +1, +2
# screen -> (cursor or None, buttons). Screen 10 (corners) takes a sequence instead.
QM_STEPS = {1: (1, 0x8000), 9: (0, 0x8000), 23: (None, 0x8000), 24: (None, 0x8000),
            17: (None, 0x8000), 22: (None, 0x8000)}
QM_CORNERS = (0x0400, 0x0100, 0x0400, 0x8000)
# Title (screen 0): with no Controller Pak the game first asks "play without saving?" (Up = Yes, A),
# then the title takes Start. Cycled until the screen changes.
QM_TITLE = (0x0800, 0x8000, 0x1000)


class Emu:
    def __init__(self, args):
        self.args = args
        self.log_lines = []
        options = {
            "mupen64plus-rdp-plugin": "angrylion",  # software renderer: no GPU context needed
            "mupen64plus-rsp-plugin": "hle",
            "mupen64plus-cpucore": args.cpu,
            "mupen64plus-angrylion-multithread": "all threads",
            "mupen64plus-pak1": "none",  # no Controller Pak: skips the "make a game note?" prompt
        }
        self.options = options
        self.core = None  # created on the emulation thread: the core must stay on one thread
        self.ram_base = None
        self.ready = threading.Event()

        self.lock = threading.Condition()
        self.vi = 0               # VIs run since boot (or as the loaded state says)
        self.frame = 0            # game frames (passes of the frame loop) seen
        self.paused = False
        self.steps_left = 0       # game frames to run before pausing
        self.vi_steps_left = 0    # VIs to run before pausing
        self.run_until = 0
        self.reason = ""
        self.breaks = []
        self.pads = [None] * 4    # overrides: dict(held, sx, sy, frames)
        self.snaps = {}
        self.events = []
        self.event_seq = 0
        self.jobs = []
        self.quit = False
        self.window_keys = 0      # N64 buttons held from the window's keyboard
        self.window_stick = [0, 0]
        self.qm = args.quickmatch
        self.qm_state = {"screen": -1, "idle": 0, "since": 999, "seq": 0, "press": 0}
        self.last_fb = None

    def _log(self, line):
        self.log_lines.append(line)
        del self.log_lines[:-200]
        if self.args.verbose:
            print("[core]", line, file=sys.stderr)

    # ---- RAM (N64 big-endian view of the core's word-swapped RDRAM) ----

    def _base(self):
        if self.ram_base is None:
            base, size = self.core.ram()
            if base and size >= RAM_SIZE:
                self.ram_base = base
        return self.ram_base

    def read(self, addr, n):
        p = addr & 0x1FFFFFFF
        if p + n > RAM_SIZE or self._base() is None:
            raise ValueError("address out of RDRAM")
        a0, a1 = p & ~3, (p + n + 3) & ~3
        raw = C.string_at(self.ram_base + a0, a1 - a0)
        swapped = b"".join(raw[i:i + 4][::-1] for i in range(0, len(raw), 4))
        return swapped[p - a0:p - a0 + n]

    def write(self, addr, data):
        p = addr & 0x1FFFFFFF
        if p + len(data) > RAM_SIZE or self._base() is None:
            raise ValueError("address out of RDRAM")
        for i, b in enumerate(data):
            C.memset(self.ram_base + ((p + i) ^ 3), b, 1)

    def u(self, addr, size):
        return int.from_bytes(self.read(addr, size), "big")

    # ---- input ----

    def _set_input(self):
        for port in range(4):
            held, sx, sy = 0, 0, 0
            if port == 0:
                held, (sx, sy) = self.window_keys, self.window_stick
            o = self.pads[port]
            if o:
                held, sx, sy = o["held"], o["sx"], o["sy"]
            if port == 0 and self.qm_state["press"]:
                held |= self.qm_state["press_buttons"]
            bits = 0
            for b, id_ in N64_TO_RETROPAD.items():
                if held & b:
                    bits |= 1 << id_
            right = [0, 0]
            for b, (axis, sign) in N64_C_BUTTONS.items():
                if held & b:
                    right[axis] = sign * 32767
            self.core.buttons[port] = bits
            # The stick: 72 units on the N64 for 30000 here (measured), y up on the N64.
            self.core.analog[port] = [max(-32768, min(32767, sx * 416)), max(-32768, min(32767, -sy * 416)),
                                      right[0], right[1]]

    # ---- quick match: walk the menus like the recomp's Quick Match mod ----

    def _quickmatch(self):
        q = self.qm_state
        if q["press"]:
            q["press"] -= 1
            return
        if self.u(QM_MENU_SIG, 4) != QM_MENU_SIG_WORD:
            q["screen"] = -1
            return
        screen = int.from_bytes(self.read(QM_MENU_SCREEN, 2), "big", signed=True)
        busy = self.u(QM_MODE, 4) != 0
        if screen != q["screen"] or busy:
            if screen != q["screen"]:
                q["seq"] = 0
            q["screen"], q["idle"] = screen, 0
        else:
            q["idle"] += 1
        q["since"] += 1
        if busy or (screen not in QM_STEPS and screen not in (0, 10)):
            return
        if q["idle"] < 40 or q["since"] < 60:  # in VIs: let the screen settle, repeat now and then
            return
        if screen == 10:
            buttons = QM_CORNERS[min(q["seq"], len(QM_CORNERS) - 1)]
            q["seq"] += 1
        elif screen == 0:
            buttons = QM_TITLE[q["seq"] % len(QM_TITLE)]
            q["seq"] += 1
        else:
            cursor, buttons = QM_STEPS[screen]
            if cursor is not None:
                self.write(QM_MENU_SCREENS + screen * QM_MENU_STRIDE + 5, bytes([cursor]))
        if screen == 17 and self.args.venue is not None:
            self.write(QM_VENUE_ADDR, bytes([self.args.venue] * 3))
        q["press"], q["press_buttons"], q["since"] = 4, buttons, 0

    # ---- running ----

    def _game_frame_started(self):
        """True on the VI where the graphics thread flipped to a new frame (a game frame passed)."""
        word = self.read(FB_INDEX, 2)
        changed = self.last_fb is not None and word != self.last_fb
        self.last_fb = word
        return changed

    def run_vi(self):
        """Runs one VI. Returns True if a game frame started during it."""
        self._set_input()
        self.core.run()
        self.vi += 1
        if self.args.no_idle_rng and self.u(IDLE_RNG_CALL, 4) == JAL_RNG_MAIN:
            self._patch_idle_rng()
        if self.qm:
            self._quickmatch()
        return self._frame_tick()

    def _patch_idle_rng(self):
        self.write(IDLE_RNG_CALL, bytes(4))
        # Writes from outside don't reach the dynarec's compiled blocks; a state round trip
        # flushes them, so the patched loop is recompiled.
        self.core.unserialize(self.core.serialize())
        self.log_lines.append("akiemu: idle-loop rng_main call patched out")

    def _frame_tick(self):
        base = self._base()
        if base is None:
            return False
        return self._game_frame_started()

    def add_event(self, what):
        self.event_seq += 1
        self.events.append({"seq": self.event_seq, "frame": self.frame, "vi": self.vi, "what": what})
        del self.events[:-32]
        print("[akiemu] frame %d: %s" % (self.frame, what), file=sys.stderr)

    def _check_stop(self):
        if self.steps_left > 0:
            self.steps_left -= 1
            if self.steps_left == 0:
                self.reason = "step"
                return True
        if self.run_until and self.frame >= self.run_until:
            self.run_until = 0
            self.reason = "run_until"
            return True
        for b in self.breaks:
            v = self.u(b["addr"], b["size"])
            hit = v == b["value"] if b["kind"] == "eq" else v != b["value"]
            if b["kind"] == "change":
                b["value"] = v
            if hit:
                self.reason = "break 0x%08X = 0x%X" % (b["addr"], v)
                return True
        return False

    def start_core(self):
        os.makedirs(os.path.join(TOOLS, "system"), exist_ok=True)
        os.makedirs(os.path.join(TOOLS, "saves"), exist_ok=True)
        self.core = libretro.Core(self.args.core, self.options, os.path.join(TOOLS, "system"),
                                  os.path.join(TOOLS, "saves"), log=self._log)
        self.core.load_game(self.args.rom)
        self.run_vi()  # the core maps RAM once it has run
        if self.args.state:
            self.load_state(file=self.args.state)
        self.paused = self.args.paused
        self.ready.set()

    def loop(self):
        try:
            self.start_core()
        except Exception as e:
            print("akiemu: could not start the core: %s" % e, file=sys.stderr)
            hard_exit()
        vi_time = 1.0 / 60
        next_t = time.perf_counter()
        while not self.quit:
            with self.lock:
                for job in self.jobs:
                    job()
                self.jobs.clear()
                self.lock.notify_all()
                if self.paused:
                    self.lock.wait(0.05)
                    next_t = time.perf_counter()
                    continue
                if self.run_vi():
                    self.frame += 1
                    for o in self.pads:
                        if o and o["frames"] is not None:
                            o["frames"] -= 1
                    self.pads = [o if not o or o["frames"] is None or o["frames"] > 0 else None for o in self.pads]
                    if self._check_stop():
                        self.paused = True
                        self.lock.notify_all()
                if self.vi_steps_left > 0:
                    self.vi_steps_left -= 1
                    if self.vi_steps_left == 0:
                        self.paused, self.reason = True, "vi_step"
                        self.lock.notify_all()
            if not self.args.fast:
                next_t += vi_time
                delay = next_t - time.perf_counter()
                if delay > 0:
                    time.sleep(delay)
                else:
                    next_t = time.perf_counter()

    # ---- state ----

    def save_state(self, slot=None, file=None):
        data = self.core.serialize()
        header = json.dumps({"frame": self.frame, "vi": self.vi}).encode()
        blob = b"AKIEMU1\0" + struct.pack("<I", len(header)) + header + data
        if slot:
            self.snaps[slot] = blob
        if file:
            with open(file, "wb") as f:
                f.write(blob)

    def load_state(self, slot=None, file=None):
        if slot:
            if slot not in self.snaps:
                raise ValueError("no such snapshot")
            blob = self.snaps[slot]
        else:
            with open(file, "rb") as f:
                blob = f.read()
        if blob[:8] == b"AKIEMU1\0":
            n = struct.unpack("<I", blob[8:12])[0]
            meta = json.loads(blob[12:12 + n])
            data = blob[12 + n:]
        else:  # a bare core state (e.g. from RetroArch with the same core version)
            meta, data = {}, blob
        self.core.unserialize(data)
        self.frame = meta.get("frame", self.frame)
        self.vi = meta.get("vi", self.vi)
        self.last_fb = self.read(FB_INDEX, 2)
        for b in self.breaks:
            if b["kind"] == "change":
                b["value"] = self.u(b["addr"], b["size"])

    def status(self):
        idle_rng = self._base() is not None and self.u(IDLE_RNG_CALL, 4) == JAL_RNG_MAIN
        return {"backend": "akiemu", "frame": self.frame, "vi": self.vi, "paused": self.paused,
                "idle_rng": idle_rng,
                "at_hook": self.paused, "attached": self._base() is not None, "reason": self.reason,
                "steps_left": self.steps_left, "run_until": self.run_until,
                "breaks": [{"addr": "0x%X" % b["addr"], "size": b["size"], "kind": b["kind"],
                            "value": "0x%X" % b["value"]} for b in self.breaks],
                "snapshots": [{"slot": k, "frame": json.loads(v[12:12 + struct.unpack("<I", v[8:12])[0]])["frame"]}
                              for k, v in self.snaps.items()],
                "events": self.events}

    def run_job(self, fn, timeout=10):
        """Runs fn on the emulation thread between VIs and returns its result (or raises its error)."""
        box = {}

        def job():
            try:
                box["ok"] = fn()
            except Exception as e:  # handed back to the HTTP thread
                box["err"] = e
        with self.lock:
            self.jobs.append(job)
            self.lock.notify_all()
            end = time.time() + timeout
            while "ok" not in box and "err" not in box:
                if not self.lock.wait(max(0.01, end - time.time())) and time.time() > end:
                    raise TimeoutError("emulation thread busy")
        if "err" in box:
            raise box["err"]
        return box["ok"]

    def wait_paused(self, timeout):
        end = time.time() + timeout
        with self.lock:
            while not self.paused and time.time() < end:
                self.lock.wait(0.05)
            return self.paused

    def png(self):
        f = self.core.frame_rgb()
        if not f:
            return None
        w, h, rgb = f
        rows = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

        def chunk(t, d):
            return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


def num(q, key, default=None):
    v = q.get(key)
    return default if v is None else int(v, 0)


def make_handler(emu):
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def reply(self, code, body, ctype="application/json"):
            if not isinstance(body, (bytes, bytearray)):
                body = json.dumps(body).encode()
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            self.do_GET()

        def do_GET(self):
            u = urllib.parse.urlparse(self.path)
            q = {k: v[-1] for k, v in urllib.parse.parse_qs(u.query, keep_blank_values=True).items()}
            body = b""
            if self.command == "POST":
                body = self.rfile.read(int(self.headers.get("Content-Length") or 0))
            try:
                r = self.handle_api(u.path, q, body)
            except ValueError as e:
                return self.reply(400, {"error": str(e)})
            except TimeoutError as e:
                return self.reply(504, {"error": str(e)})
            if isinstance(r, tuple):
                return self.reply(200, r[1], r[0])
            return self.reply(200, r)

        def handle_api(self, p, q, body):
            e = emu
            timeout = num(q, "timeout", 10000) / 1000
            if p in ("/", "/status"):
                with e.lock:
                    return e.status()
            if p == "/pause":
                with e.lock:
                    if not e.paused:
                        e.paused, e.reason = True, "pause"
                return e.status()
            if p == "/resume":
                with e.lock:
                    e.paused, e.steps_left, e.vi_steps_left, e.reason = False, 0, 0, ""
                    e.lock.notify_all()
                return e.status()
            if p in ("/step", "/run", "/vi_step"):
                with e.lock:
                    if p == "/step":
                        n = num(q, "n", 1)
                        if n < 1:
                            raise ValueError("n must be >= 1")
                        e.steps_left = n
                    elif p == "/vi_step":
                        e.vi_steps_left = num(q, "n", 1)
                    else:
                        f = num(q, "frame")
                        if f is None or f <= e.frame:
                            raise ValueError("run needs frame=<a future frame>")
                        e.run_until = f
                    e.paused, e.reason = False, ""
                    e.lock.notify_all()
                e.wait_paused(timeout)
                with e.lock:
                    return e.status()
            if p == "/read":
                addr, n = num(q, "addr"), num(q, "len", 4)
                if addr is None:
                    raise ValueError("read needs addr")
                with e.lock:
                    data = e.read(addr, n)
                if "binary" in q:
                    return ("application/octet-stream", data)
                return {"addr": "0x%X" % (0x80000000 | addr & 0x1FFFFFFF), "len": n, "hex": data.hex()}
            if p == "/write":
                addr = num(q, "addr")
                data = bytes.fromhex(q["hex"]) if "hex" in q else body
                if addr is None or not data:
                    raise ValueError("write needs addr and hex= (or a POST body)")
                with e.lock:
                    e.write(addr, data)
                return {"written": len(data)}
            if p == "/break":
                with e.lock:
                    if "clear" in q:
                        e.breaks.clear()
                        return e.status()
                    addr, size = num(q, "addr"), num(q, "size", 4)
                    if addr is None or size not in (1, 2, 4):
                        raise ValueError("break needs addr and size 1, 2 or 4 (or clear=1)")
                    addr = 0x80000000 | addr & 0x1FFFFFFF
                    if "eq" in q:
                        b = {"kind": "eq", "value": num(q, "eq")}
                    elif "ne" in q:
                        b = {"kind": "ne", "value": num(q, "ne")}
                    elif "change" in q:
                        b = {"kind": "change", "value": e.u(addr, size)}
                    else:
                        raise ValueError("break needs eq=, ne= or change=1")
                    b.update(addr=addr, size=size)
                    e.breaks.append(b)
                    return e.status()
            if p == "/pad":
                with e.lock:
                    if "clear" in q:
                        e.pads = [None] * 4
                        return e.status()
                    port = num(q, "port")
                    if port is None or not 0 <= port <= 3:
                        raise ValueError("pad needs port=0..3")
                    sx, sy = num(q, "sx", 0), num(q, "sy", 0)
                    e.pads[port] = {"held": num(q, "held", 0), "sx": sx - 256 if sx > 127 else sx,
                                    "sy": sy - 256 if sy > 127 else sy, "frames": num(q, "frames")}
                    return e.status()
            if p in ("/save", "/load"):
                slot, file = q.get("slot") or None, q.get("file") or None
                if not slot and not file:
                    raise ValueError("needs slot= and/or file=")
                fn = e.save_state if p == "/save" else e.load_state
                try:
                    e.run_job(lambda: fn(slot, file), timeout)
                except (OSError, RuntimeError) as err:
                    raise ValueError(str(err))
                with e.lock:
                    return e.status()
            if p == "/drop":
                with e.lock:
                    e.snaps.pop(q.get("slot"), None)
                    return e.status()
            if p == "/screenshot":
                png = e.run_job(e.png, timeout)
                if png is None:
                    raise ValueError("no frame yet")
                return ("image/png", png)
            if p == "/log":
                return {"lines": e.log_lines[-num(q, "n", 50):]}
            if p == "/quit":
                threading.Thread(target=lambda: (time.sleep(0.2), hard_exit())).start()
                return {"quit": True}
            if p == "/regs":
                raise ValueError("regs: not available in the emulator backend")
            raise ValueError("unknown endpoint " + p)
    return Handler


def hard_exit():
    # The core's own threads make a normal interpreter exit hang.
    sys.stdout.flush()
    sys.stderr.flush()
    k32 = C.windll.kernel32
    k32.GetCurrentProcess.restype = C.c_void_p
    k32.TerminateProcess.argtypes = [C.c_void_p, C.c_uint]
    k32.TerminateProcess(k32.GetCurrentProcess(), 0)


# ---- window ----

KEYMAP = {  # Tk keysym -> N64 button
    "x": 0x8000, "c": 0x4000, "z": 0x2000, "Return": 0x1000, "Up": 0x0800, "Down": 0x0400, "Left": 0x0200,
    "Right": 0x0100, "a": 0x0020, "s": 0x0010, "i": 0x0008, "k": 0x0004, "j": 0x0002, "l": 0x0001,
}


def run_window(emu, scale):
    import tkinter as tk
    root = tk.Tk()
    root.title("akiemu - " + GAME_TITLE)
    label = tk.Label(root, bg="black")
    label.pack()
    status = tk.Label(root, anchor="w", font=("Consolas", 9))
    status.pack(fill="x")
    img = {"photo": None, "vf": -1}

    def hotkey(name):
        with emu.lock:
            if name == "F2":
                if emu.paused:
                    emu.paused, emu.steps_left, emu.reason = False, 0, ""
                    emu.add_event("F2 resume")
                else:
                    emu.paused, emu.reason = True, "F2 pause"
                    emu.add_event("F2 pause")
            elif name == "F3":
                if emu.paused:
                    emu.steps_left, emu.paused, emu.reason = 1, False, ""
                else:
                    emu.paused, emu.reason = True, "hotkey"
            elif name == "F4":
                emu.paused, emu.reason = True, "F4 mark"
                emu.add_event("F4 mark")
            elif name == "F5":
                emu.save_state("quick", os.path.join(TOOLS, "states", "quick.state"))
                emu.add_event("F5 quick save (also _tools/libretro/states/quick.state)")
            elif name == "F9":
                try:
                    emu.load_state("quick" if "quick" in emu.snaps else None,
                                   None if "quick" in emu.snaps else os.path.join(TOOLS, "states", "quick.state"))
                    emu.add_event("F9 quick load")
                except (OSError, ValueError, RuntimeError) as err:
                    emu.add_event("F9 quick load failed: %s" % err)
            emu.lock.notify_all()

    def key(ev, down):
        k = ev.keysym
        if down and k in ("F2", "F3", "F4", "F5", "F9"):
            hotkey(k)
            return
        b = KEYMAP.get(k)
        if b:
            with emu.lock:
                emu.window_keys = emu.window_keys | b if down else emu.window_keys & ~b
    root.bind("<KeyPress>", lambda ev: key(ev, True))
    root.bind("<KeyRelease>", lambda ev: key(ev, False))
    root.protocol("WM_DELETE_WINDOW", hard_exit)

    def refresh():
        with emu.lock:
            vf = emu.core.video_frames
            f = emu.core.frame_rgb() if vf != img["vf"] else None
            st = "frame %d  vi %d  %s %s" % (emu.frame, emu.vi, "PAUSED" if emu.paused else "running", emu.reason)
        if f:
            img["vf"] = vf
            w, h, rgb = f
            photo = tk.PhotoImage(data=b"P6 %d %d 255\n" % (w, h) + rgb, format="PPM")
            yscale = scale * (2 if h <= 240 and w >= 480 else 1)  # 640x240: double the lines
            xs = max(1, scale)
            if xs > 1 or yscale > 1:
                photo = photo.zoom(xs, yscale)
            img["photo"] = photo
            label.configure(image=photo)
        status.configure(text=st + "   F2 pause  F3 step  F4 mark  F5 save  F9 load")
        root.after(33, refresh)
    refresh()
    root.mainloop()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", default="wcw", choices=sorted(GAMES), help="which game's addresses to use")
    ap.add_argument("--rom", help="ROM file (default: the game's ROM in _roms)")
    ap.add_argument("--core", default=os.path.join(TOOLS, "mupen64plus_next_libretro.dll"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("AKIDBG_PORT", "5664")))
    ap.add_argument("--headless", action="store_true", help="no window")
    ap.add_argument("--scale", type=int, default=1, help="window zoom")
    ap.add_argument("--fast", action="store_true", help="run as fast as possible instead of 60 VIs a second")
    ap.add_argument("--quickmatch", action="store_true", help="walk the menus to a CPU-vs-CPU single match")
    ap.add_argument("--venue", type=int, choices=range(5), help="quick match mat: 0 WCW, 1 NWO, 2 DOA, 3 IU, 4 random")
    ap.add_argument("--state", help="load this state file once booted")
    ap.add_argument("--paused", action="store_true", help="start paused (after loading --state)")
    ap.add_argument("--no-idle-rng", action="store_true",
                    help="patch out the idle loop's rng_main calls, so rng_main is deterministic like the recomp's")
    ap.add_argument("--cpu", default="dynamic_recompiler", choices=("dynamic_recompiler", "cached_interpreter", "pure_interpreter"))
    ap.add_argument("--verbose", action="store_true", help="print the core's log")
    args = ap.parse_args()
    g = GAMES[args.game]
    global FB_INDEX, IDLE_RNG_CALL, JAL_RNG_MAIN, GAME_TITLE
    FB_INDEX, IDLE_RNG_CALL, JAL_RNG_MAIN, GAME_TITLE = g["FB_INDEX"], g["IDLE_RNG_CALL"], g["JAL_RNG_MAIN"], g["title"]
    if args.rom is None:
        args.rom = os.path.join(REPO, "_roms", g["rom"])
    if args.quickmatch and args.game != "wcw":
        ap.error("--quickmatch is World Tour only")
    os.makedirs(os.path.join(TOOLS, "states"), exist_ok=True)

    emu = Emu(args)
    t = threading.Thread(target=emu.loop, daemon=True)
    t.start()
    emu.ready.wait()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(emu))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    print("akiemu: listening on 127.0.0.1:%d" % args.port, file=sys.stderr)
    try:
        if args.headless:
            while True:
                time.sleep(3600)
        else:
            run_window(emu, args.scale)
    except KeyboardInterrupt:
        pass
    hard_exit()


if __name__ == "__main__":
    main()
