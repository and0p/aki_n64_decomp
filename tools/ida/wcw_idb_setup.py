"""IDAPython script run by wcw_build_idb.py inside idat (headless).

Splits the flat image into N64 segments, defines functions from jal targets and IDO
prologues, applies the symbol files (_re_docs/symbols/<AKI_GAME>, default wcw), then exports what IDA computes
(callers, callees, data xrefs, struct-field accesses, pseudocode) to export.json for
tools/sym/build.py. Exits saving the database.
"""
import json
import os
import re
import sys

import ida_auto
import ida_bytes
import ida_funcs
import ida_hexrays
import ida_name
import ida_pro
import ida_segment
import ida_typeinf
import ida_xref
import idautils
import idc

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "sym"))
import symdb  # noqa: E402

EXPORT = os.environ.get("WCW_EXPORT") or os.path.join(os.path.dirname(idc.get_idb_path()), "export.json")

GAME = os.environ.get("AKI_GAME", "wcw")
OVERLAY = os.environ.get("AKI_OVERLAY", "e")  # nomercy only: e (the match) or c (the menus)

if GAME == "revenge":
    # From the recomp's splat config (disasm/revenge.yaml): main is code with data pockets,
    # then the match overlay ovl_b (ovl_a, the other overlay, shares 0x80090000 and is left out).
    SEGMENTS = [
        (0x80000000, 0x80000400, "lowmem", "BSS"),
        (0x80000400, 0x8003BB70, "main_text", "CODE"),
        (0x8003BB70, 0x80090000, "main_bss", "BSS"),
        (0x80090000, 0x800E2B10, "ovlb_text", "CODE"),
        (0x800E2B10, 0x800E77B0, "ovlb_data", "DATA"),
        (0x800E77B0, 0x800FAFA0, "ovlb_bss", "BSS"),
        (0x800FAFA0, 0x80100000, "heap_hi", "BSS"),
    ]
    TEXT = []  # main mixes code and data, so functions come only from the recomp's list
elif GAME == "nomercy" and OVERLAY == "b":
    # Main plus the story overlay ovl_b (second slot); main-segment symbols only, as for ovl_c.
    SEGMENTS = [
        (0x80000000, 0x80000400, "lowmem", "BSS"),
        (0x80000400, 0x80040380, "main_text", "CODE"),
        (0x80040380, 0x80056670, "main_data", "DATA"),
        (0x80056670, 0x800BD960, "main_bss", "BSS"),
        (0x800BD960, 0x80106760, "heap_lo", "BSS"),
        (0x80106760, 0x8011EBD0, "ovlb_text", "CODE"),
        (0x8011EBD0, 0x801338E0, "ovlb_data", "DATA"),
        (0x801338E0, 0x8016CDD0, "ovlb_bss", "BSS"),
        (0x8016CDD0, 0x80170000, "heap_hi", "BSS"),
    ]
    TEXT = []
elif GAME == "nomercy" and OVERLAY == "c":
    # Main plus the menu overlay ovl_c (second slot). Only the main-segment symbols apply: ovl_e's
    # addresses overlap ovl_c's, so they are filtered out in main().
    SEGMENTS = [
        (0x80000000, 0x80000400, "lowmem", "BSS"),
        (0x80000400, 0x80040380, "main_text", "CODE"),
        (0x80040380, 0x80056670, "main_data", "DATA"),
        (0x80056670, 0x800BD960, "main_bss", "BSS"),
        (0x800BD960, 0x80106760, "heap_lo", "BSS"),
        (0x80106760, 0x80147EE0, "ovlc_text", "CODE"),
        (0x80147EE0, 0x80155620, "ovlc_data", "DATA"),
        (0x80155620, 0x8015BA00, "ovlc_bss", "BSS"),
        (0x8015BA00, 0x80170000, "heap_hi", "BSS"),
    ]
    TEXT = []
elif GAME == "nomercy":
    # From the recomp's splat config (disasm/nomercy.yaml, Rev 1): main text and data, then the
    # match overlay ovl_e, which spans both overlay slots (ovl_a-ovl_d are left out).
    SEGMENTS = [
        (0x80000000, 0x80000400, "lowmem", "BSS"),
        (0x80000400, 0x80040380, "main_text", "CODE"),
        (0x80040380, 0x80056670, "main_data", "DATA"),
        (0x80056670, 0x800BD960, "main_bss", "BSS"),
        (0x800BD960, 0x800D9960, "heap_lo", "BSS"),
        (0x800D9960, 0x80144670, "ovle_text", "CODE"),
        (0x80144670, 0x80151ED0, "ovle_data", "DATA"),
        (0x80151ED0, 0x8016B9F0, "ovle_bss", "BSS"),
        (0x8016B9F0, 0x80170000, "heap_hi", "BSS"),
    ]
    TEXT = []  # functions come only from the recomp's list, as for Revenge
elif GAME == "wm2000":
    # From the recomp's splat config (disasm/wm2k.yaml, V1.2): main is two runs of code each
    # followed by data, then the match overlay ovl_d, which spans both overlay slots.
    SEGMENTS = [
        (0x80000000, 0x80000400, "lowmem", "BSS"),
        (0x80000400, 0x80038840, "main_text", "CODE"),
        (0x80038840, 0x800395D8, "main_data", "DATA"),
        (0x800395D8, 0x8003BA28, "main_text2", "CODE"),
        (0x8003BA28, 0x8004B4C0, "main_data2", "DATA"),
        (0x8004B4C0, 0x800B1390, "main_bss", "BSS"),
        (0x800B1390, 0x800E1B90, "heap_lo", "BSS"),
        (0x800E1B90, 0x8014C640, "ovld_text", "CODE"),
        (0x8014C640, 0x80153F10, "ovld_data", "DATA"),
        (0x80153F10, 0x8016F170, "ovld_bss", "BSS"),
        (0x8016F170, 0x80170000, "heap_hi", "BSS"),
    ]
    TEXT = []
else:
    SEGMENTS = [
        # start, end, name, class
        (0x80000000, 0x80000400, "lowmem", "BSS"),
        (0x80000400, 0x8002A3B0, "main_text", "CODE"),
        (0x8002A3B0, 0x800387F0, "main_data", "DATA"),
        (0x800387F0, 0x80062090, "main_bss", "BSS"),
        (0x80062090, 0x80090000, "heap_lo", "BSS"),
        (0x80090000, 0x800E5F70, "ovlb_text", "CODE"),
        (0x800E5F70, 0x800E9400, "ovlb_data", "DATA"),
        (0x800E9400, 0x800F8AC0, "ovlb_bss", "BSS"),
        (0x800F8AC0, 0x80100000, "heap_hi", "BSS"),
    ]
    TEXT = [(0x80000400, 0x8002A3B0), (0x80090000, 0x800E5F70)]

# Optional extra function starts (JSON list of N64 addresses), e.g. from a recomp's symbol dump.
SEED = os.environ.get("AKI_SEED_FUNCS")

# MIPS64 sign-extends 0x8xxxxxxx addresses, so Hex-Rays only resolves globals when the
# segments live at 0xFFFFFFFF8xxxxxxx. Symbols are kept as 32-bit N64 addresses and widened here.
KSEG = 0xFFFFFFFF00000000


def K(ea):
    return ea | KSEG


def N(ea):
    return ea & 0xFFFFFFFF


def log(*a):
    print("[%s]" % GAME, *a)


def setup_segments():
    first = ida_segment.get_first_seg()
    while first:
        ida_segment.del_segm(first.start_ea, ida_segment.SEGMOD_KEEP)
        first = ida_segment.get_first_seg()
    for start, end, name, cls in SEGMENTS:
        seg = ida_segment.segment_t()
        seg.start_ea = start
        seg.end_ea = end
        seg.bitness = 2  # Hex-Rays MIPS only decompiles 64-bit segments; pointers stay __ptr32 in types
        seg.perm = 7
        ida_segment.add_segm_ex(seg, name, cls, ida_segment.ADDSEG_NOSREG | ida_segment.ADDSEG_QUIET)
    ida_segment.rebase_program(KSEG, ida_segment.MSF_FIXONCE)


def find_functions(sym):
    starts = set()
    for lo, hi in TEXT:
        for ea in range(lo, hi, 4):
            w = ida_bytes.get_wide_dword(K(ea))
            if w >> 26 == 3:  # jal
                t = 0x80000000 | ((w & 0x3FFFFFF) << 2)
                if any(a <= t < b for a, b in TEXT):
                    starts.add(t)
            # IDO prologue right after "jr ra; <delay>"
            if (w >> 16) == 0x27BD and (w & 0x8000) and ida_bytes.get_wide_dword(K(ea - 8)) == 0x03E00008:
                starts.add(ea)
    starts.update(f["addr"] for f in sym.funcs)
    if SEED:
        with open(SEED, encoding="utf-8") as fp:
            starts.update(json.load(fp))
    for t in sorted(starts):
        idc.create_insn(K(t))
        ida_funcs.add_func(K(t))
    log("seeded", len(starts), "function starts")
    sizes = os.environ.get("AKI_SEED_SIZES")
    if sizes:
        # Some leaf functions right after another's "jr ra" are refused by add_func until it is
        # given their end (No Mercy: three AI mode handlers); retry those with the dump's size.
        with open(sizes, encoding="utf-8") as fp:
            sizes = {int(k): v for k, v in json.load(fp).items()}
        fixed = 0
        for t, n in sorted(sizes.items()):
            if not ida_funcs.get_func(K(t)):
                idc.create_insn(K(t))
                fixed += bool(ida_funcs.add_func(K(t), K(t + n)))
        log("created", fixed, "more functions from the dump's sizes")


def data_decl(name, ctype):
    if "[" in ctype:
        base, dims = ctype.split("[", 1)
        return "%s %s[%s;" % (base, name, dims)
    return "%s %s;" % (ctype, name)


def apply_symbols(sym):
    errs = ida_typeinf.idc_parse_types(symdb.ida_c(sym.types_c), 0)
    log("type parse errors:", errs)
    for d in sym.data:
        ea = K(d["addr"])
        ida_name.set_name(ea, d["name"], ida_name.SN_FORCE)
        if d.get("type"):
            decl = symdb.ida_c(data_decl(d["name"], d["type"]))
            tif = ida_typeinf.tinfo_t()
            if ida_typeinf.parse_decl(tif, None, decl, ida_typeinf.PT_SIL) is not None:
                ida_bytes.del_items(ea, 0, tif.get_size())  # clear auto-made items in the way
            if not idc.SetType(ea, decl):
                log("SetType failed", d["name"], d["type"])
    for f in sym.funcs:
        ea = K(f["addr"])
        if not ida_name.set_name(ea, f["name"], ida_name.SN_FORCE):
            log("set_name failed", f["name"])
        if f.get("proto") and not idc.SetType(ea, f["proto"] + ";"):
            log("prototype failed", f["name"], f["proto"])
        if f.get("desc"):
            idc.set_func_cmt(ea, f["desc"], 0)


def pick_pointer_width(sym):
    """Pointer arguments arrive in 64-bit registers but IDO code truncates them with 32-bit
    ops. Depending on the function, Hex-Rays types fields best with either native or
    __ptr32 pointer arguments, so decompile both ways and keep whichever resolves more
    struct fields."""
    def score(ea):
        ida_hexrays.mark_cfunc_dirty(ea)
        try:
            text = str(ida_hexrays.decompile(ea, flags=ida_hexrays.DECOMP_NO_CACHE))
        except ida_hexrays.DecompilationFailure:
            return -1
        return text.count("->") - 100 * ("allocation has failed" in text)

    switched = 0
    for f in sym.funcs:
        proto = f.get("proto")
        if not proto or "*" not in proto:
            continue
        ea = K(f["addr"])
        native = score(ea)
        # IDA drops __ptr32 from arguments unless the calling convention is explicit
        p32 = re.sub(r"(\w+)\s*\(", r"__fastcall \1(", symdb.ida_c(proto), count=1)
        idc.SetType(ea, p32 + ";")
        if score(ea) > native:
            switched += 1
        else:
            idc.SetType(ea, proto + ";")
    log("prototypes using __ptr32 arguments:", switched)


class FieldVisitor(ida_hexrays.ctree_visitor_t):
    """Collects struct member accesses (struct, offset, read/write) in one function."""

    def __init__(self):
        super().__init__(ida_hexrays.CV_PARENTS)
        self.hits = set()

    def visit_expr(self, e):
        if e.op not in (ida_hexrays.cot_memptr, ida_hexrays.cot_memref):
            return 0
        t = e.x.type
        if e.op == ida_hexrays.cot_memptr:
            t = t.get_pointed_object()
        sname = t.get_type_name()
        if not sname:
            return 0
        parent = self.parent_expr()
        write = False
        if parent is not None:
            if ida_hexrays.cot_asg <= parent.op <= ida_hexrays.cot_asgumod and parent.x == e:
                write = True
            elif parent.op in (ida_hexrays.cot_preinc, ida_hexrays.cot_predec,
                               ida_hexrays.cot_postinc, ida_hexrays.cot_postdec):
                write = True
        self.hits.add((sname, e.m, "w" if write else "r"))
        return 0


def struct_layout(sym):
    layout = {}
    for sname in dict.fromkeys(s for s, _, _, _ in sym.struct_fields()):
        tif = ida_typeinf.tinfo_t()
        if not tif.get_named_type(None, sname):
            continue
        udt = ida_typeinf.udt_type_data_t()
        tif.get_udt_details(udt)
        layout[sname] = {"size": tif.get_size(),
                         "members": [(m.name, m.offset // 8, m.size // 8) for m in udt]}
    return layout


def export(sym):
    funcs = []
    pseudo_fail = 0
    for ea in idautils.Functions():
        f = ida_funcs.get_func(ea)
        callers = sorted({N(ida_funcs.get_func(x.frm).start_ea) for x in idautils.XrefsTo(ea)
                          if x.iscode and ida_funcs.get_func(x.frm)})
        callees, drefs = set(), set()
        for head in idautils.FuncItems(ea):
            for x in idautils.XrefsFrom(head, 0):
                if x.iscode and x.type in (ida_xref.fl_CN, ida_xref.fl_CF):
                    callees.add(N(x.to))
                elif not x.iscode:
                    drefs.add(N(x.to))
        fields, code = [], ""
        try:
            cf = ida_hexrays.decompile(ea)
        except ida_hexrays.DecompilationFailure:
            cf = None
        if cf:
            v = FieldVisitor()
            v.apply_to(cf.body, None)
            fields = sorted(v.hits)
            code = str(cf)
        else:
            pseudo_fail += 1
        funcs.append({
            "addr": N(ea), "name": ida_funcs.get_func_name(ea), "size": f.end_ea - f.start_ea,
            "callers": callers, "callees": sorted(callees), "drefs": sorted(drefs),
            "fields": fields, "pseudocode": code,
        })
    names = {N(ea): n for ea, n in idautils.Names()}
    with open(EXPORT, "w", encoding="utf-8") as fp:
        json.dump({"funcs": funcs, "structs": struct_layout(sym), "names": names}, fp)
    log("exported", len(funcs), "functions;", pseudo_fail, "failed to decompile ->", EXPORT)


def main():
    sym = symdb.Symbols(GAME)
    if GAME == "nomercy" and OVERLAY in ("b", "c"):
        # keep the main-segment names only; the match overlay's addresses are ovl_c's here
        sym.funcs = [f for f in sym.funcs if f["addr"] < 0x800D9960]
        sym.data = [d for d in sym.data if d["addr"] < 0x800D9960]
        log("ovl_c build: %d main functions, %d globals kept" % (len(sym.funcs), len(sym.data)))
    ida_auto.set_ida_state(ida_auto.st_Work)
    setup_segments()
    find_functions(sym)
    ida_auto.auto_wait()
    apply_symbols(sym)
    ida_auto.auto_wait()
    if ida_hexrays.init_hexrays_plugin():
        pick_pointer_width(sym)
        export(sym)
    else:
        log("Hex-Rays unavailable, export skipped")
    ida_pro.qexit(0)


main()
