"""Helpers for reading N64 RDRAM out of a paused N64Recomp process from IDA.

Load from IDA's Python console (or the IDA MCP py_eval tool):
    import sys; sys.path.insert(0, r"<repo>\\tools\\ida"); import n64mem as m
    m.find_rdram(); m.u16(0x800F16EC)
"""
import struct
import idc
import ida_segment

RB = None  # host address of RDRAM 0x80000000 (32-bit words stored little-endian)

# World Tour entry point code (0x80000400, ROM 0x1000) as it sits in recomp memory.
ENTRY_SIG = bytes.fromhex("0480083c0200093cf0870825")


def find_rdram():
    """Scan debugger segments for the word-swapped entry code and set RB."""
    global RB
    for i in range(ida_segment.get_segm_qty()):
        s = ida_segment.getnseg(i)
        if s.end_ea - s.start_ea < 0x800000:
            continue
        b = idc.read_dbg_memory(s.start_ea + 0x400, len(ENTRY_SIG))
        if b == ENTRY_SIG:
            RB = s.start_ea
            return RB
    return None


def rblk(a, n):
    a0 = a & ~3
    n0 = ((a + n + 3) & ~3) - a0
    raw = idc.read_dbg_memory(RB + (a0 & 0x7fffffff), n0)
    out = bytearray()
    for k in range(0, n0, 4):
        out += raw[k:k + 4][::-1]
    return bytes(out[a - a0:a - a0 + n])


def u8(a): return rblk(a, 1)[0]
def s8(a): return struct.unpack(">b", rblk(a, 1))[0]
def u16(a): return struct.unpack(">H", rblk(a, 2))[0]
def s16(a): return struct.unpack(">h", rblk(a, 2))[0]
def u32(a): return struct.unpack(">I", rblk(a, 4))[0]
def f32(a): return struct.unpack(">f", rblk(a, 4))[0]


def dump(a, n):
    b = rblk(a, n)
    lines = []
    for k in range(0, n, 16):
        lines.append("%08x %s" % (a + k, b[k:k + 16].hex(' ')))
    return "\n".join(lines)


LD = {0x20: 'lb', 0x24: 'lbu', 0x21: 'lh', 0x25: 'lhu', 0x23: 'lw', 0x31: 'lwc1', 0x35: 'ldc1',
      0x28: 'sb', 0x29: 'sh', 0x2b: 'sw', 0x39: 'swc1', 0x3d: 'sdc1', 0x09: 'addiu'}
STORES = (0x28, 0x29, 0x2b, 0x39, 0x3d)

SEGS = [(0x80000400, 0x800387F0), (0x80090000, 0x800E9400)]
_code = {}


def code():
    if not _code:
        for lo, hi in SEGS:
            _code[lo] = rblk(lo, hi - lo)
    return _code


def word(pc):
    for lo, mem in code().items():
        if lo <= pc < lo + len(mem):
            return struct.unpack(">I", mem[pc - lo:pc - lo + 4])[0]
    return struct.unpack(">I", rblk(pc, 4))[0]


def scan():
    """Absolute-address loads/stores/addiu built from lui pairs."""
    refs = []
    for base, mem in code().items():
        regs = {}
        for i in range(0, len(mem) - 3, 4):
            w = struct.unpack(">I", mem[i:i + 4])[0]
            op = w >> 26; rs = (w >> 21) & 31; rt = (w >> 16) & 31; imm = w & 0xffff
            simm = imm - 0x10000 if imm & 0x8000 else imm
            pc = base + i
            if op == 0x0f:
                regs[rt] = (imm << 16) & 0xffffffff
                continue
            if op in LD and rs in regs:
                a = (regs[rs] + simm) & 0xffffffff
                refs.append((a, pc, LD[op]))
                if op == 0x09:
                    regs[rt] = a
                elif op not in STORES:
                    regs.pop(rt, None)
                continue
            if w == 0x03e00008:
                regs = {}
            if op == 0:
                regs.pop((w >> 11) & 31, None)
            elif op in (0x08, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e) or op in LD:
                regs.pop(rt, None)
    return refs


_refs = []


def refs_in(lo, hi):
    if not _refs:
        _refs.extend(scan())
    return sorted(r for r in _refs if lo <= r[0] < hi)


R = ['zero', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7',
     's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 't8', 't9', 'k0', 'k1', 'gp', 'sp', 'fp', 'ra']
OPS = {0x02: 'j', 0x03: 'jal', 0x04: 'beq', 0x05: 'bne', 0x06: 'blez', 0x07: 'bgtz', 0x08: 'addi', 0x09: 'addiu',
       0x0a: 'slti', 0x0b: 'sltiu', 0x0c: 'andi', 0x0d: 'ori', 0x0e: 'xori', 0x0f: 'lui', 0x14: 'beql', 0x15: 'bnel',
       0x16: 'blezl', 0x17: 'bgtzl', 0x20: 'lb', 0x21: 'lh', 0x23: 'lw', 0x24: 'lbu', 0x25: 'lhu', 0x28: 'sb',
       0x29: 'sh', 0x2b: 'sw', 0x31: 'lwc1', 0x35: 'ldc1', 0x39: 'swc1', 0x3d: 'sdc1'}
SPECIAL = {0x00: 'sll', 0x02: 'srl', 0x03: 'sra', 0x04: 'sllv', 0x06: 'srlv', 0x07: 'srav', 0x08: 'jr', 0x09: 'jalr',
           0x10: 'mfhi', 0x12: 'mflo', 0x18: 'mult', 0x19: 'multu', 0x1a: 'div', 0x1b: 'divu', 0x20: 'add',
           0x21: 'addu', 0x22: 'sub', 0x23: 'subu', 0x24: 'and', 0x25: 'or', 0x26: 'xor', 0x27: 'nor', 0x2a: 'slt',
           0x2b: 'sltu'}
REGIMM = {0: 'bltz', 1: 'bgez', 2: 'bltzl', 3: 'bgezl', 0x11: 'bgezal'}
FMT = {16: 's', 17: 'd', 20: 'w'}
COP1F = {0: 'add', 1: 'sub', 2: 'mul', 3: 'div', 4: 'sqrt', 5: 'abs', 6: 'mov', 7: 'neg', 0x0d: 'trunc.w',
         0x20: 'cvt.s', 0x21: 'cvt.d', 0x24: 'cvt.w', 0x32: 'c.eq', 0x3c: 'c.lt', 0x3e: 'c.le'}


def dis1(pc):
    w = word(pc)
    op = w >> 26; rs = (w >> 21) & 31; rt = (w >> 16) & 31; rd = (w >> 11) & 31; sa = (w >> 6) & 31
    imm = w & 0xffff; simm = imm - 0x10000 if imm & 0x8000 else imm
    if w == 0:
        return 'nop'
    if op == 0:
        f = w & 63; n = SPECIAL.get(f, 'sp%02x' % f)
        if f in (0, 2, 3):
            return '%s %s,%s,%d' % (n, R[rd], R[rt], sa)
        if f == 8:
            return 'jr %s' % R[rs]
        if f in (0x10, 0x12):
            return '%s %s' % (n, R[rd])
        if f in (0x18, 0x19, 0x1a, 0x1b):
            return '%s %s,%s' % (n, R[rs], R[rt])
        return '%s %s,%s,%s' % (n, R[rd], R[rs], R[rt])
    if op == 1:
        return '%s %s,%x' % (REGIMM.get(rt, 'regimm'), R[rs], pc + 4 + simm * 4)
    if op in (2, 3):
        return '%s %08x' % (OPS[op], 0x80000000 | ((w & 0x3ffffff) << 2))
    if op == 0x11:
        if rs == 0: return 'mfc1 %s,f%d' % (R[rt], rd)
        if rs == 4: return 'mtc1 %s,f%d' % (R[rt], rd)
        if rs == 8: return 'bc1%s %x' % ('tf'[rt & 1], pc + 4 + simm * 4)
        fd = sa; fs = rd; ft = rt; f = w & 63
        return '%s.%s f%d,f%d,f%d' % (COP1F.get(f, 'f%02x' % f), FMT.get(rs, '?'), fd, fs, ft)
    n = OPS.get(op, 'op%02x' % op)
    if op in (4, 5, 0x14, 0x15):
        return '%s %s,%s,%x' % (n, R[rs], R[rt], pc + 4 + simm * 4)
    if op in (6, 7, 0x16, 0x17):
        return '%s %s,%x' % (n, R[rs], pc + 4 + simm * 4)
    if op == 0x0f:
        return 'lui %s,0x%x' % (R[rt], imm)
    if op in (0x31, 0x35, 0x39, 0x3d):
        return '%s f%d,%d(%s)' % (n, rt, simm, R[rs])
    if op >= 0x20:
        return '%s %s,%d(%s)' % (n, R[rt], simm, R[rs])
    if op in (0x0c, 0x0d, 0x0e):
        return '%s %s,%s,0x%x' % (n, R[rt], R[rs], imm)
    return '%s %s,%s,%d' % (n, R[rt], R[rs], simm)


def dis(pc, n):
    return "\n".join('%08x  %s' % (pc + 4 * i, dis1(pc + 4 * i)) for i in range(n))


def func_start(pc):
    """Walk back to the instruction after the previous jr ra + delay slot."""
    p = pc
    while p > 0x80000400:
        if word(p - 8) == 0x03e00008:
            return p
        p -= 4
    return p
