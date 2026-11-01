"""Load the hand-written symbol files (_re_docs/symbols/<game>/).

Shared by the IDA build script (runs inside IDA's Python) and the query index builder, so
it uses only the standard library.
"""
import os
import re
import tomllib

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def symbols_dir(game="wcw"):
    return os.path.join(REPO, "_re_docs", "symbols", game)


class Symbols:
    def __init__(self, game="wcw"):
        self.dir = symbols_dir(game)
        self.funcs, self.data, self.enums = [], [], []
        for fn in sorted(os.listdir(self.dir)):
            if not fn.endswith(".toml"):
                continue
            with open(os.path.join(self.dir, fn), "rb") as f:
                doc = tomllib.load(f)
            src = fn[:-5]
            for kind, out in (("func", self.funcs), ("data", self.data), ("enum", self.enums)):
                for e in doc.get(kind, []):
                    e["file"] = src
                    out.append(e)
        with open(os.path.join(self.dir, "types.h"), encoding="utf-8") as f:
            self.types_c = f.read()
        self._check()

    def _check(self):
        seen = {}
        for e in self.funcs + self.data:
            for key in ("addr", "name"):
                v = e[key]
                if v in seen:
                    raise ValueError("duplicate %s %r in %s and %s" % (key, v, seen[v], e["file"]))
                seen[v] = e["file"]

    def struct_fields(self):
        """[(struct, field, c_type, comment)] parsed from types.h, in declaration order."""
        out = []
        cur = None
        for line in self.types_c.splitlines():
            m = re.match(r"\s*struct\s+(\w+)\s*\{", line)
            if m and "}" not in line:
                cur = m.group(1)
                continue
            if cur and re.match(r"\s*\};", line):
                cur = None
                continue
            if not cur:
                continue
            m = re.match(r"\s*(.+?)\s*;\s*(?:/\*\s*(.*?)\s*\*/)?\s*$", line)
            if not m:
                continue
            decl, comment = m.group(1), m.group(2) or ""
            for part in decl_names(decl):
                out.append((cur, part[1], part[0], comment))
        return out


def decl_names(decl):
    """'u8 f04, f05' / 'struct Vec4 joints[19]' / 'void *p' -> [(type, name)]."""
    m = re.match(r"((?:struct\s+)?\w+)\s+(.*)", decl)
    if not m:
        return []
    base, rest = m.groups()
    out = []
    for item in rest.split(","):
        item = item.strip()
        nm = re.match(r"(\**)\s*(\w+)(.*)", item)
        if nm:
            out.append(((base + " " + nm.group(1) + nm.group(3)).strip(), nm.group(2)))
    return out


def ida_c(decl):
    """Plain C -> IDA C for the MIPS64 database: every pointer becomes __ptr32."""
    decl = re.sub(r"/\*.*?\*/", "", decl, flags=re.S)  # comments contain '*'
    return re.sub(r"\*(?!\s*__ptr32)", "*__ptr32 ", decl)
