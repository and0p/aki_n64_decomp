"""The shared description of "match state": field lists generated from the symbol files.

    py tools/trace/fields.py --game nomercy json  [OUT]    field list (JSON) for the recorder and diff
    py tools/trace/fields.py --game nomercy header OUT     C++ header of the N64-layout structs
    py tools/trace/fields.py --game nomercy show [REGION]  print the regions and their fields

Structs come from `_re_docs/symbols/<game>/types.h` (layout computed with the N64's natural
alignment, pointers 4 bytes), globals from the TOML `[[data]]` entries. Which RAM ranges make up a
trace, and the comparison rule overrides, are in `tools/trace/<game>_regions.toml`.

Comparison rules (`_re_docs/state_comparison.md`):
    exact    integers, flags, states, ids, N64 addresses
    bits     floats, compared bit for bit
    unknown  recorded and reported, but a difference does not fail (`_unk*` fields, unnamed bytes)
    ignore   not compared (render-only, host-side, padding)
"""
import argparse
import json
import os
import re
import sys
import tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools", "sym"))
import symdb  # noqa: E402

BASE = {"u8": (1, "u8"), "s8": (1, "s8"), "u16": (2, "u16"), "s16": (2, "s16"), "u32": (4, "u32"),
        "s32": (4, "s32"), "f32": (4, "f32"), "char": (1, "s8"), "int": (4, "s32"), "float": (4, "f32")}


def parse_types(text):
    """types.h -> {struct: {"size", "align", "fields": [{name, off, type, count, size, comment, ptr}]}}."""
    text = re.sub(r"//[^\n]*", "", text)
    structs = {}
    # Comments are kept per field: split the text into declarations with their trailing comment.
    for m in re.finditer(r"struct\s+(\w+)\s*\{(.*?)\}\s*;", text, re.S):
        name, body = m.group(1), m.group(2)
        decls = []
        for dm in re.finditer(r"([^;{}]+?);[ \t]*(/\*.*?\*/)?", re.sub(r"^\s*/\*.*?\*/", "", body, flags=re.S), re.S):
            decl = re.sub(r"/\*.*?\*/", "", dm.group(1), flags=re.S).strip()
            comment = (dm.group(2) or "")[2:-2].strip()
            if decl:
                decls.append((" ".join(decl.split()), " ".join(comment.split())))
        structs[name] = decls
    out = {}

    def layout(name):
        if name in out:
            return out[name]
        fields, off, align = [], 0, 1
        for decl, comment in structs[name]:
            for ctype, fname in symdb.decl_names(decl):
                dims = [int(x, 0) for x in re.findall(r"\[(\w+)\]", ctype)]
                base = re.sub(r"\s*\[.*", "", ctype).strip()
                ptr = "*" in base
                count = 1
                for d in dims:
                    count *= d
                if ptr:
                    esize, ealign, etype = 4, 4, "ptr:" + base.replace("*", "").replace("struct", "").strip()
                elif base.startswith("struct "):
                    sub = layout(base.split()[1])
                    esize, ealign, etype = sub["size"], sub["align"], base.split()[1]
                else:
                    esize, etype = BASE[base]
                    ealign = esize
                off = (off + ealign - 1) // ealign * ealign
                fields.append(dict(name=fname, off=off, type=etype, count=count, dims=dims, size=esize * count,
                                   comment=comment))
                off += esize * count
                align = max(align, ealign)
        size = (off + align - 1) // align * align
        out[name] = dict(size=size, align=align, fields=fields)
        return out[name]

    for name in structs:
        layout(name)
    return out


def default_rule(field):
    if field["name"].startswith(("_unk", "_pad")):
        return "unknown"
    if field["type"] == "f32":
        return "bits"
    return "exact"


def load(game):
    syms = symdb.Symbols(game)
    structs = parse_types(syms.types_c)
    with open(os.path.join(HERE, "%s_regions.toml" % game), "rb") as f:
        cfg = tomllib.load(f)
    return syms, structs, cfg


def type_size(structs, t):
    """'struct Wrestler[4]' / 'u8[4][176]' / 's32' -> (element type, count, element size)."""
    m = re.match(r"(?:struct\s+)?(\w+)((?:\[\w+\])*)$", t.strip())
    base, dims = m.group(1), [int(x, 0) for x in re.findall(r"\[(\w+)\]", m.group(2))]
    count = 1
    for d in dims:
        count *= d
    esize = structs[base]["size"] if base in structs else BASE[base][0]
    return base, count, esize


def flatten(structs, typ, count, prefix, off, rules, out, comment=""):
    """Expands a typed range into leaf fields (scalars and scalar arrays)."""
    if typ in structs:
        s = structs[typ]
        for i in range(count):
            p = prefix + ("[%d]" % i if count > 1 else "")
            for f in s["fields"]:
                if f["type"] in structs:
                    flatten(structs, f["type"], f["count"], p + "." + f["name"], off + i * s["size"] + f["off"],
                            rules, out, f["comment"])
                else:
                    key = "%s.%s" % (typ, f["name"])
                    rule = rules.get(key, default_rule(f))
                    out.append(dict(name=p + "." + f["name"], off=off + i * s["size"] + f["off"],
                                    type="u32" if f["type"].startswith("ptr:") else f["type"], count=f["count"],
                                    rule=rule, key=key))
    else:
        f = dict(name=prefix, type=typ)
        out.append(dict(name=prefix, off=off, type=typ, count=count, rule=rules.get(prefix, default_rule(f)),
                        key=prefix))


def region_fields(game, syms, structs, cfg):
    """[{name, addr, size, fields: [{name, off, type, count, rule}]}] for every trace region."""
    rules = cfg.get("rules", {})
    data = sorted(syms.data, key=lambda e: e["addr"])
    regions = []
    for r in cfg["region"]:
        addr, size = r["addr"], r["size"]
        fields = []
        if "type" in r:
            base, count, esize = type_size(structs, r["type"])
            flatten(structs, base, count, r["name"], 0, rules, fields)
        else:
            # Globals: the [[data]] symbols inside the range, plus the region's own extra fields.
            entries = [dict(name=e["name"], addr=e["addr"], type=e.get("type", "u32")) for e in data
                       if addr <= e["addr"] < addr + size]
            entries += [dict(name=k, addr=v["addr"], type=v["type"]) for k, v in r.get("fields", {}).items()]
            for e in sorted(entries, key=lambda e: e["addr"]):
                base, count, esize = type_size(structs, e["type"])
                if e["addr"] + esize * count > addr + size:
                    count = (addr + size - e["addr"]) // esize
                flatten(structs, base, count, e["name"], e["addr"] - addr, rules, fields)
        # Bytes no field covers become unknown runs, so every recorded byte is compared somehow.
        covered = bytearray(size)
        for f in fields:
            n = f["count"] * (structs[f["type"]]["size"] if f["type"] in structs else BASE[f["type"]][0])
            for k in range(f["off"], min(size, f["off"] + n)):
                covered[k] = 1
        k = 0
        while k < size:
            if covered[k]:
                k += 1
                continue
            j = k
            while j < size and not covered[j]:
                j += 1
            fields.append(dict(name="%s+0x%X" % (r["name"], k), off=k, type="u8", count=j - k,
                               rule=r.get("gap_rule", "unknown"), key=""))
            k = j
        fields.sort(key=lambda f: f["off"])
        regions.append(dict(name=r["name"], addr=addr, size=size, fields=fields, owner=r.get("owner", "")))
    return regions


def field_list(game):
    syms, structs, cfg = load(game)
    return dict(game=game, regions=region_fields(game, syms, structs, cfg))


# ---- C++ header ----

CT = {"u8": "u8", "s8": "s8", "u16": "u16", "s16": "s16", "u32": "u32", "s32": "s32", "f32": "f32"}


def header(game):
    syms, structs, cfg = load(game)
    host = set(cfg.get("header", {}).get("host_structs", []))
    lines = ["// Generated by tools/trace/fields.py from _re_docs/symbols/%s/types.h: do not edit." % game,
             "// N64-layout structs (offsets as on the N64, values in host byte order), the offset checks, and",
             "// the field tables nm_layout.cpp uses to convert between N64 RAM bytes and these structs.",
             "#pragma once", "#include <cstddef>", "#include \"nm_base.h\"", "", "namespace nm {", ""]
    for name, s in structs.items():
        lines.append("struct %s {" % name)
        for f in s["fields"]:
            if f["type"].startswith("ptr:"):
                t = f["type"][4:]
                ctype = ("%s *" % t) if name in host else "u32 "
            elif f["type"] in structs:
                ctype = f["type"] + " "
            else:
                ctype = CT[f["type"]] + " "
            dims = "".join("[%d]" % d for d in f["dims"])
            com = ("  // " + f["comment"]) if f["comment"] and len(f["comment"]) < 140 else ""
            lines.append("    %s%s%s;%s" % (ctype, f["name"], dims, com))
        lines.append("};")
        if name not in host:
            lines.append("static_assert(sizeof(%s) == 0x%X, \"%s size\");" % (name, s["size"], name))
            for f in s["fields"]:
                lines.append("static_assert(offsetof(%s, %s) == 0x%X, \"%s.%s\");" % (name, f["name"], f["off"], name,
                                                                                    f["name"]))
        lines.append("")
    # Field tables for byte swapping: element size per run (1, 2 or 4), nested structs expanded.
    lines.append("// Byte-swap tables: (offset, element size, count) runs covering each N64-layout struct.")
    for name, s in structs.items():
        if name in host:
            continue
        runs = []
        flatten(structs, name, 1, name, 0, {}, runs)
        lines.append("inline constexpr SwapRun k%sSwap[] = {" % name)
        for f in runs:
            lines.append("    {0x%X, %d, %d},  // %s" % (f["off"], BASE[f["type"]][0], f["count"], f["name"]))
        lines.append("};")
    lines += ["", "}  // namespace nm", ""]
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", default="nomercy")
    ap.add_argument("cmd", choices=["json", "header", "show"])
    ap.add_argument("arg", nargs="?")
    a = ap.parse_args()
    if a.cmd == "json":
        s = json.dumps(field_list(a.game), indent=1)
        if a.arg:
            with open(a.arg, "w", encoding="utf-8") as f:
                f.write(s)
        else:
            print(s)
    elif a.cmd == "header":
        with open(a.arg, "w", encoding="utf-8", newline="\n") as f:
            f.write(header(a.game))
    else:
        for r in field_list(a.game)["regions"]:
            if a.arg and r["name"] != a.arg:
                continue
            print("%-14s 0x%08X 0x%X  %d fields" % (r["name"], r["addr"], r["size"], len(r["fields"])))
            if a.arg:
                for f in r["fields"]:
                    print("   +0x%04X %-44s %-4s x%-4d %s" % (f["off"], f["name"], f["type"], f["count"], f["rule"]))


if __name__ == "__main__":
    main()
