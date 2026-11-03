"""Small MIPS (R4300i) disassembler for reading the No Mercy functions while decompiling them.

    py tools/decomp/mips_dis.py NAME|ADDR [...]          print each function (symbol names applied)
    py tools/decomp/mips_dis.py --out DIR NAME|ADDR ...   one .s file per function

Reads the flat image the IDA build writes (_tools/ida_nomercy/nomercy_ovl_e.bin, based at
0x80000000: main plus the match overlay) and the function table of the sym index. Calls and
lui/addiu (lui/lw ...) pairs are annotated with symbol names and addresses, and float loads from
constants show the value, which is what matters for keeping float order exact. It covers the
integer, COP0-free and COP1 instructions the game's C code uses; anything else prints as .word.
"""
import argparse
import os
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO, "tools", "sym"))
import sym  # noqa: E402

IMAGE = os.path.join(REPO, "_tools", "ida_nomercy", "nomercy_ovl_e.bin")
BASE = 0x80000000
R = ["zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
     "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]
F = ["f%d" % i for i in range(32)]


class Image:
    def __init__(self, path=IMAGE):
        with open(path, "rb") as f:
            self.data = f.read()

    def u32(self, a):
        return struct.unpack_from(">I", self.data, a - BASE)[0]

    def f32(self, a):
        return struct.unpack_from(">f", self.data, a - BASE)[0]

    def f64(self, a):
        return struct.unpack_from(">d", self.data, a - BASE)[0]


def sx16(v):
    return v - 0x10000 if v & 0x8000 else v


def dis(w, pc):
    """-> (mnemonic, operands, info) with info = {'kind': ..., ...} for annotation."""
    op, rs, rt, rd, sa, fn = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, w & 63
    imm, simm = w & 0xFFFF, sx16(w & 0xFFFF)
    info = {}
    if w == 0:
        return "nop", "", info
    if op == 0:
        names = {0x20: "add", 0x21: "addu", 0x22: "sub", 0x23: "subu", 0x24: "and", 0x25: "or", 0x26: "xor",
                 0x27: "nor", 0x2A: "slt", 0x2B: "sltu", 0x2C: "dadd", 0x2D: "daddu", 0x2E: "dsub", 0x2F: "dsubu",
                 0x04: "sllv", 0x06: "srlv", 0x07: "srav", 0x14: "dsllv", 0x16: "dsrlv", 0x17: "dsrav"}
        if fn in (0, 2, 3, 0x38, 0x3A, 0x3B, 0x3C, 0x3E, 0x3F):
            m = {0: "sll", 2: "srl", 3: "sra", 0x38: "dsll", 0x3A: "dsrl", 0x3B: "dsra", 0x3C: "dsll32",
                 0x3E: "dsrl32", 0x3F: "dsra32"}[fn]
            return m, "%s, %s, %d" % (R[rd], R[rt], sa), info
        if fn in (0x04, 0x06, 0x07, 0x14, 0x16, 0x17):
            return names[fn], "%s, %s, %s" % (R[rd], R[rt], R[rs]), info
        if fn in names:
            if fn == 0x25 and rt == 0:
                return "move", "%s, %s" % (R[rd], R[rs]), info
            return names[fn], "%s, %s, %s" % (R[rd], R[rs], R[rt]), info
        if fn == 8:
            return "jr", R[rs], {"kind": "jr"}
        if fn == 9:
            return "jalr", "%s, %s" % (R[rd], R[rs]), info
        if fn in (0x10, 0x12):
            return {0x10: "mfhi", 0x12: "mflo"}[fn], R[rd], info
        if fn in (0x11, 0x13):
            return {0x11: "mthi", 0x13: "mtlo"}[fn], R[rs], info
        if fn in (0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F):
            m = {0x18: "mult", 0x19: "multu", 0x1A: "div", 0x1B: "divu", 0x1C: "dmult", 0x1D: "dmultu",
                 0x1E: "ddiv", 0x1F: "ddivu"}[fn]
            return m, "%s, %s" % (R[rs], R[rt]), info
        if fn == 0x0D:
            return "break", "", info
        if fn in (0x30, 0x31, 0x32, 0x33, 0x34, 0x36):
            return {0x30: "tge", 0x31: "tgeu", 0x32: "tlt", 0x33: "tltu", 0x34: "teq", 0x36: "tne"}[fn], \
                "%s, %s" % (R[rs], R[rt]), info
        if fn == 0x0F:
            return "sync", "", info
    if op == 1:
        m = {0: "bltz", 1: "bgez", 2: "bltzl", 3: "bgezl", 0x10: "bltzal", 0x11: "bgezal"}.get(rt)
        if m:
            t = pc + 4 + (simm << 2)
            return m, "%s, 0x%08X" % (R[rs], t), {"kind": "branch", "target": t}
    if op in (2, 3):
        t = (pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
        return ("j" if op == 2 else "jal"), "0x%08X" % t, {"kind": "call" if op == 3 else "jump", "target": t}
    if op in (4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17):
        m = {4: "beq", 5: "bne", 6: "blez", 7: "bgtz", 0x14: "beql", 0x15: "bnel", 0x16: "blezl", 0x17: "bgtzl"}[op]
        t = pc + 4 + (simm << 2)
        if op in (4, 5, 0x14, 0x15):
            if op == 4 and rs == 0 and rt == 0:
                return "b", "0x%08X" % t, {"kind": "branch", "target": t}
            return m, "%s, %s, 0x%08X" % (R[rs], R[rt], t), {"kind": "branch", "target": t}
        return m, "%s, 0x%08X" % (R[rs], t), {"kind": "branch", "target": t}
    if op in (8, 9, 0x18, 0x19):
        m = {8: "addi", 9: "addiu", 0x18: "daddi", 0x19: "daddiu"}[op]
        if op == 9 and rs == 0:
            return "li", "%s, %d" % (R[rt], simm), info
        return m, "%s, %s, %d" % (R[rt], R[rs], simm), {"kind": "lo", "rs": rs, "rt": rt, "imm": simm}
    if op in (0xA, 0xB):
        return ("slti" if op == 0xA else "sltiu"), "%s, %s, %d" % (R[rt], R[rs], simm), info
    if op in (0xC, 0xD, 0xE):
        m = {0xC: "andi", 0xD: "ori", 0xE: "xori"}[op]
        if op == 0xD and rs == 0:
            return "li", "%s, 0x%X" % (R[rt], imm), info
        return m, "%s, %s, 0x%X" % (R[rt], R[rs], imm), {"kind": "ori" if op == 0xD else "", "rs": rs, "rt": rt,
                                                        "imm": imm}
    if op == 0xF:
        return "lui", "%s, 0x%X" % (R[rt], imm), {"kind": "lui", "rt": rt, "imm": imm}
    mem = {0x20: "lb", 0x21: "lh", 0x22: "lwl", 0x23: "lw", 0x24: "lbu", 0x25: "lhu", 0x26: "lwr", 0x27: "lwu",
           0x28: "sb", 0x29: "sh", 0x2A: "swl", 0x2B: "sw", 0x2E: "swr", 0x37: "ld", 0x3F: "sd", 0x1A: "ldl",
           0x1B: "ldr", 0x2C: "sdl", 0x2D: "sdr"}
    if op in mem:
        return mem[op], "%s, %d(%s)" % (R[rt], simm, R[rs]), {"kind": "mem", "rs": rs, "rt": rt, "imm": simm}
    fmem = {0x31: "lwc1", 0x35: "ldc1", 0x39: "swc1", 0x3D: "sdc1"}
    if op in fmem:
        return fmem[op], "%s, %d(%s)" % (F[rt], simm, R[rs]), {"kind": "fmem", "rs": rs, "rt": rt, "imm": simm,
                                                              "dbl": op in (0x35, 0x3D)}
    if op == 0x11:  # COP1
        fmt = rs
        ft, fs, fd = rt, rd, sa
        if fmt == 0:
            return "mfc1", "%s, %s" % (R[rt], F[fs]), info
        if fmt == 1:
            return "dmfc1", "%s, %s" % (R[rt], F[fs]), info
        if fmt == 2:
            return "cfc1", "%s, $%d" % (R[rt], fs), info
        if fmt == 4:
            return "mtc1", "%s, %s" % (R[rt], F[fs]), info
        if fmt == 5:
            return "dmtc1", "%s, %s" % (R[rt], F[fs]), info
        if fmt == 6:
            return "ctc1", "%s, $%d" % (R[rt], fs), info
        if fmt == 8:
            t = pc + 4 + (simm << 2)
            return {0: "bc1f", 1: "bc1t", 2: "bc1fl", 3: "bc1tl"}[rt & 3], "0x%08X" % t, {"kind": "branch", "target": t}
        sfx = {16: "s", 17: "d", 20: "w", 21: "l"}.get(fmt, "?")
        ar = {0: "add", 1: "sub", 2: "mul", 3: "div"}
        if fn in ar:
            return "%s.%s" % (ar[fn], sfx), "%s, %s, %s" % (F[fd], F[fs], F[ft]), info
        un = {4: "sqrt", 5: "abs", 6: "mov", 7: "neg", 8: "round.l", 9: "trunc.l", 10: "ceil.l", 11: "floor.l",
              12: "round.w", 13: "trunc.w", 14: "ceil.w", 15: "floor.w", 32: "cvt.s", 33: "cvt.d", 36: "cvt.w",
              37: "cvt.l"}
        if fn in un:
            return "%s.%s" % (un[fn], sfx), "%s, %s" % (F[fd], F[fs]), info
        if fn >= 48:
            c = ["f", "un", "eq", "ueq", "olt", "ult", "ole", "ule", "sf", "ngle", "seq", "ngl", "lt", "nge", "le",
                 "ngt"][fn & 15]
            return "c.%s.%s" % (c, sfx), "%s, %s" % (F[fs], F[ft]), info
    return ".word", "0x%08X" % w, info


class Namer:
    def __init__(self):
        self.con = sym.connect("nomercy")
        self.ranges = None

    def name(self, a):
        r = self.con.execute("SELECT name FROM func WHERE addr=?", (a,)).fetchone() or \
            self.con.execute("SELECT name FROM data WHERE addr=?", (a,)).fetchone() or \
            self.con.execute("SELECT name FROM name WHERE addr=?", (a,)).fetchone()
        return r[0] if r else None

    def near(self, a):
        """'g_wrestlers[1].state' for an address inside a known data symbol or trace field."""
        if self.ranges is None:
            self._load_ranges()
        best = None
        for start, size, name, fl in self.ranges:
            if start <= a < start + size:
                best = (start, name, fl)
        if best:
            start, name, fl = best
            for f in fl:
                if f["off"] <= a - start < f["off"] + f["n"]:
                    extra = a - start - f["off"]
                    return f["name"] + ("+0x%X" % extra if extra else "")
            return name + ("+0x%X" % (a - start) if a != start else "")
        return self.name(a)

    def _load_ranges(self):
        sys.path.insert(0, os.path.join(REPO, "tools", "trace"))
        import fields
        syms, structs, cfg = fields.load("nomercy")
        self.ranges = []
        for e in syms.data:
            if "type" not in e:
                continue
            try:
                base, count, esize = fields.type_size(structs, e["type"])
            except Exception:
                continue
            self.ranges.append((e["addr"], esize * count, e["name"], []))
        for r in fields.field_list("nomercy")["regions"]:
            fl = [dict(name=f["name"], off=f["off"], n=f["count"] * fields.BASE[f["type"]][0]) for f in r["fields"]
                  if not f["name"].startswith(r["name"] + "+")]
            self.ranges.append((r["addr"], r["size"], r["name"], fl))
        self.ranges.sort(key=lambda x: -x[1])  # small ranges last: they win

    def func(self, key):
        t, r = sym.resolve(self.con, key)
        if t != "func":
            raise SystemExit("not a function: %s" % key)
        return r["addr"], r["name"], r["size"]


def function(img, namer, key):
    addr, name, size = namer.func(key)
    lines = ["# %s 0x%08X size 0x%X" % (name, addr, size)]
    hi = {}  # register -> lui value
    labels = set()
    for pc in range(addr, addr + size, 4):
        _, _, info = dis(img.u32(pc), pc)
        if info.get("kind") == "branch":
            labels.add(info["target"])
    for pc in range(addr, addr + size, 4):
        w = img.u32(pc)
        m, ops, info = dis(w, pc)
        note = ""
        k = info.get("kind")
        if pc in labels:
            hi = {}  # values known only along straight-line code
        if k == "call":
            note = namer.name(info["target"]) or ""
        elif k == "jump":
            t = info["target"]
            note = namer.name(t) or ("" if addr <= t < addr + size else "tail?")
        elif k in ("lo", "mem", "fmem", "ori") and info["rs"] in hi:
            full = (hi[info["rs"]] + info["imm"]) & 0xFFFFFFFF
            note = namer.near(full) or "0x%08X" % full
            if k == "fmem" and m.startswith("l") and 0x80000000 <= full < 0x80170000:
                note += " = %r" % (img.f64(full) if info["dbl"] else img.f32(full))
        if k == "lui" and (0x3F00 <= info["imm"] < 0x4800 or 0xBF00 <= info["imm"] < 0xC800):
            note = "(f32 %r)" % struct.unpack(">f", struct.pack(">I", info["imm"] << 16))[0]
        # Track constant registers: lui sets one, addiu / ori from a known one sets its target,
        # anything else that writes a register forgets it; calls forget the caller-saved ones.
        op = w >> 26
        dest = None
        if k == "lui":
            hi[info["rt"]] = info["imm"] << 16
        elif k in ("lo", "ori") and info["rs"] in hi:
            hi[info["rt"]] = (hi[info["rs"]] + info["imm"]) & 0xFFFFFFFF
        elif op == 0 and (w & 63) == 0x21 and (((w >> 21) & 31) in hi or ((w >> 16) & 31) in hi)                 and ((w >> 21) & 31) != 0 and ((w >> 16) & 31) != 0:
            # addu of a known base and an index: an array access (the note says which array).
            hi[(w >> 11) & 31] = hi.get((w >> 21) & 31, hi.get((w >> 16) & 31))
        elif op == 0 and m not in ("jr", "mult", "multu", "div", "divu", "dmult", "dmultu", "ddiv", "ddivu",
                                   "mthi", "mtlo", "nop", "break", "sync"):
            dest = (w >> 11) & 31
        elif op in (8, 9, 0xA, 0xB, 0xC, 0xD, 0xE, 0x18, 0x19) or (k == "mem" and m.startswith("l")) or                 (op == 0x11 and ((w >> 21) & 31) in (0, 1, 2)):
            dest = (w >> 16) & 31
        if dest is not None and not (k in ("lo", "ori") and info.get("rs") in hi and dest == info.get("rt")):
            hi.pop(dest, None)
        if k == "call" or m == "jalr":
            for r in list(hi):
                if r < 16 or r in (24, 25, 31):
                    hi.pop(r)
        lab = "L%08X:" % pc if pc in labels else ""
        lines.append("%-10s %08X: %-8s %-30s%s" % (lab, pc, m, ops, ("  ; " + note) if note else ""))
    return name, addr, "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("funcs", nargs="+")
    ap.add_argument("--out")
    a = ap.parse_args()
    img, namer = Image(), Namer()
    for key in a.funcs:
        name, addr, text = function(img, namer, key)
        if a.out:
            os.makedirs(a.out, exist_ok=True)
            with open(os.path.join(a.out, "%08X_%s.s" % (addr, name)), "w") as f:
                f.write(text)
        else:
            print(text)


if __name__ == "__main__":
    main()
