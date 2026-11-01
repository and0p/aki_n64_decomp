"""Query the reverse-engineering index with short, compact answers.

    py tools/sym/sym.py show  NAME|ADDR      function or global: docs, callers, callees, globals, fields
    py tools/sym/sym.py code  NAME|ADDR      Hex-Rays pseudocode (from the last IDA export)
    py tools/sym/sym.py tree  NAME [-d N] [--up]   call tree down (callees) or up (callers)
    py tools/sym/sym.py struct NAME          fields with offsets, descriptions and accessor counts
    py tools/sym/sym.py field STRUCT.FIELD   functions reading / writing a field
    py tools/sym/sym.py enum  [NAME]         documented enum values
    py tools/sym/sym.py find  TEXT           search names and descriptions
    py tools/sym/sym.py todo  [-n N]         undocumented functions most connected to documented ones
    py tools/sym/sym.py stats                coverage numbers
    py tools/sym/sym.py check                types.h vs IDA layout, unknown names in docs

The index is rebuilt automatically when the symbol files or the IDA export changed.
"""
import argparse
import os
import sqlite3
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build  # noqa: E402

MAX_LIST = 25


def connect(game):
    if build.stale(game):
        build.index(game, quiet=True)
    con = sqlite3.connect(build.paths(game)[2])
    con.row_factory = sqlite3.Row
    return con


def h(a):
    return "%08X" % a if a is not None else "-"


def resolve(con, key):
    """Name or hex address -> ('func'|'data'|'name', row)."""
    try:
        addr = int(key, 16)
    except ValueError:
        addr = None
    for table in ("func", "data"):
        q = "SELECT * FROM %s WHERE %s" % (table, "addr=?" if addr is not None else "name=?")
        r = con.execute(q, (addr if addr is not None else key,)).fetchone()
        if r:
            return table, r
    q = "SELECT * FROM name WHERE %s" % ("addr=?" if addr is not None else "name=?")
    r = con.execute(q, (addr if addr is not None else key,)).fetchone()
    if r:
        return "name", r
    return None, None


def fname(con, addr):
    r = con.execute("SELECT name FROM func WHERE addr=?", (addr,)).fetchone() or \
        con.execute("SELECT name FROM name WHERE addr=?", (addr,)).fetchone()
    return r[0] if r else h(addr)


def label(con, addr):
    """name, marked with * when documented."""
    r = con.execute("SELECT name, documented FROM func WHERE addr=?", (addr,)).fetchone()
    if r:
        return r[0] + ("*" if r[1] else "")
    return fname(con, addr)


def short_list(items):
    items = list(items)
    s = ", ".join(items[:MAX_LIST])
    return s + (" ... +%d" % (len(items) - MAX_LIST) if len(items) > MAX_LIST else "")


def field_name(con, struct, off):
    r = con.execute("SELECT name FROM field WHERE struct=? AND offset<=? ORDER BY offset DESC LIMIT 1",
                    (struct, off)).fetchone()
    return r[0] if r else "+0x%X" % off


def show(con, key):
    kind, r = resolve(con, key)
    if not kind:
        print("not found:", key)
        return 1
    if kind == "data" or kind == "name" and not con.execute("SELECT 1 FROM func WHERE addr=?", (r["addr"],)).fetchone():
        return show_data(con, r, kind)
    a = r["addr"]
    print("%s %s  [%s]  %s" % (h(a), r["name"], r["status"] or "undocumented",
                               "%s.toml" % r["file"] if r["file"] else ""))
    if r["proto"]:
        print("  " + r["proto"])
    for k in ("desc", "notes", "port"):
        if r[k]:
            print("  %s: %s" % (k, r[k]))
    if r["size"]:
        print("  size: 0x%X" % r["size"])
    callers = [x[0] for x in con.execute("SELECT DISTINCT caller FROM calls WHERE callee=?", (a,))]
    callees = [x[0] for x in con.execute("SELECT DISTINCT callee FROM calls WHERE caller=?", (a,))]
    print("  callers (%d): %s" % (len(callers), short_list(label(con, c) for c in callers)))
    print("  callees (%d): %s" % (len(callees), short_list(label(con, c) for c in callees)))
    named = []
    for (d,) in con.execute("SELECT DISTINCT addr FROM dref WHERE func=?", (a,)):
        n = con.execute("SELECT name FROM data WHERE addr=?", (d,)).fetchone()
        if n:
            named.append(n[0])
    if named:
        print("  globals: " + short_list(sorted(named)))
    acc = {}
    for s, o, rw in con.execute("SELECT struct, offset, rw FROM access WHERE func=?", (a,)):
        acc.setdefault("%s.%s" % (s, field_name(con, s, o)), set()).add(rw)
    if acc:
        print("  fields: " + short_list("%s(%s)" % (k, "".join(sorted(v))) for k, v in sorted(acc.items())))
    print("  (* = documented)")
    return 0


def show_data(con, r, kind):
    a = r["addr"]
    if kind == "data":
        print("%s %s  [%s]  %s.toml" % (h(a), r["name"], r["status"] or "-", r["file"]))
        if r["type"]:
            print("  type: " + r["type"])
        if r["desc"]:
            print("  desc: " + r["desc"])
    else:
        print("%s %s  [undocumented global]" % (h(a), r["name"]))
    users = [x[0] for x in con.execute("SELECT DISTINCT func FROM dref WHERE addr=?", (a,))]
    print("  referenced by (%d): %s" % (len(users), short_list(label(con, u) for u in users)))
    return 0


def code(con, key):
    kind, r = resolve(con, key)
    row = con.execute("SELECT text FROM code WHERE addr=?", (r["addr"],)).fetchone() if r else None
    if not row:
        print("no pseudocode for", key)
        return 1
    print(row[0])
    return 0


def tree(con, key, depth, up):
    kind, r = resolve(con, key)
    if not r:
        print("not found:", key)
        return 1
    q = "SELECT DISTINCT caller FROM calls WHERE callee=?" if up else "SELECT DISTINCT callee FROM calls WHERE caller=?"
    seen = set()

    def walk(a, d, indent):
        print("  " * indent + label(con, a) + ("  (seen)" if a in seen and indent else ""))
        if a in seen or d == 0:
            return
        seen.add(a)
        for (b,) in con.execute(q, (a,)):
            walk(b, d - 1, indent + 1)

    walk(r["addr"], depth, 0)
    return 0


def struct(con, name):
    rows = con.execute("SELECT * FROM field WHERE struct=? ORDER BY offset", (name,)).fetchall()
    if not rows:
        print("unknown struct:", name)
        return 1
    for f in rows:
        n = con.execute("SELECT COUNT(DISTINCT func) FROM access WHERE struct=? AND offset>=? AND offset<?",
                        (name, f["offset"], f["offset"] + max(f["size"], 1))).fetchone()[0]
        print("+%03X %-18s %-14s %3d fn  %s" % (f["offset"], f["name"], f["ctype"], n, f["desc"]))
    return 0


def field(con, spec):
    sname, _, fld = spec.partition(".")
    f = con.execute("SELECT * FROM field WHERE struct=? AND name=?", (sname, fld)).fetchone()
    if not f:
        print("unknown field:", spec)
        return 1
    print("%s.%s  +0x%X %s  %s" % (sname, fld, f["offset"], f["ctype"], f["desc"]))
    for rw, title in (("w", "writes"), ("r", "reads")):
        fs = [x[0] for x in con.execute(
            "SELECT DISTINCT func FROM access WHERE struct=? AND offset>=? AND offset<? AND rw=?",
            (sname, f["offset"], f["offset"] + max(f["size"], 1), rw))]
        print("  %s (%d): %s" % (title, len(fs), short_list(label(con, x) for x in fs)))
    return 0


def enum(con, name):
    q = "SELECT * FROM enum" + (" WHERE enum=?" if name else "") + " ORDER BY enum, value"
    for e in con.execute(q, (name,) if name else ()):
        print("%-14s %-22s 0x%X" % (e["enum"], e["key"], e["value"]))
    return 0


def find(con, text):
    like = "%" + text + "%"
    for r in con.execute("SELECT addr, name, desc, status FROM func WHERE name LIKE ? OR desc LIKE ? OR notes LIKE ?",
                         (like, like, like)):
        print("%s func %-28s %s" % (h(r[0]), r[1], (r[2] or "")[:90]))
    for r in con.execute("SELECT addr, name, desc FROM data WHERE name LIKE ? OR desc LIKE ?", (like, like)):
        print("%s data %-28s %s" % (h(r[0]), r[1], (r[2] or "")[:90]))
    for r in con.execute("SELECT struct, name, offset, desc FROM field WHERE name LIKE ? OR desc LIKE ?", (like, like)):
        print("         field %s.%s +0x%X  %s" % (r[0], r[1], r[2], (r[3] or "")[:80]))
    return 0


def todo(con, n):
    rows = con.execute("""
        SELECT f.addr, f.name, f.size,
               (SELECT COUNT(*) FROM calls c JOIN func d ON d.addr=c.caller WHERE c.callee=f.addr AND d.documented) AS from_doc,
               (SELECT COUNT(*) FROM calls c JOIN func d ON d.addr=c.callee WHERE c.caller=f.addr AND d.documented) AS to_doc,
               (SELECT COUNT(*) FROM calls c WHERE c.callee=f.addr) AS callers
        FROM func f WHERE NOT f.documented
        ORDER BY from_doc + to_doc DESC, callers DESC LIMIT ?""", (n,)).fetchall()
    print("addr     name                   size  calls_from_doc calls_to_doc callers")
    for r in rows:
        print("%s %-22s %5X %8d %12d %8d" % (h(r[0]), r[1], r[2] or 0, r[3], r[4], r[5]))
    return 0


def stats(con):
    q = lambda s: con.execute(s).fetchone()[0]  # noqa: E731
    print("functions: %d, documented %d (confirmed %d, likely %d, guess %d), ported %d" % (
        q("SELECT COUNT(*) FROM func"), q("SELECT COUNT(*) FROM func WHERE documented"),
        q("SELECT COUNT(*) FROM func WHERE status='confirmed'"), q("SELECT COUNT(*) FROM func WHERE status='likely'"),
        q("SELECT COUNT(*) FROM func WHERE status='guess'"), q("SELECT COUNT(*) FROM func WHERE port<>''")))
    print("globals documented: %d" % q("SELECT COUNT(*) FROM data"))
    for s, total, unk in con.execute("SELECT struct, COUNT(*), SUM(name LIKE '\\_unk%' ESCAPE '\\') FROM field GROUP BY struct"):
        print("struct %-12s %3d fields, %3d unknown" % (s, total, unk))
    return 0


def check(con):
    import symdb
    sym = symdb.Symbols(con.execute("SELECT value FROM meta WHERE key='game'").fetchone()[0])
    ok = True
    declared = {(s, n) for s, n, _, _ in sym.struct_fields()}
    indexed = {(r[0], r[1]) for r in con.execute("SELECT struct, name FROM field")}
    for s, n in sorted(declared - indexed):
        print("field missing from IDA layout (rebuild with --ida?): %s.%s" % (s, n))
        ok = False
    for s, n in sorted(indexed - declared):
        print("field in IDA but not in types.h: %s.%s" % (s, n))
        ok = False
    print("ok" if ok else "mismatches found")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", default="wcw")
    ap.add_argument("cmd")
    ap.add_argument("arg", nargs="?")
    ap.add_argument("-d", "--depth", type=int, default=2)
    ap.add_argument("-n", type=int, default=20)
    ap.add_argument("--up", action="store_true")
    args = ap.parse_args()
    con = connect(args.game)
    c = args.cmd
    if c == "show":
        return show(con, args.arg)
    if c == "code":
        return code(con, args.arg)
    if c == "tree":
        return tree(con, args.arg, args.depth, args.up)
    if c == "struct":
        return struct(con, args.arg)
    if c == "field":
        return field(con, args.arg)
    if c == "enum":
        return enum(con, args.arg)
    if c == "find":
        return find(con, args.arg)
    if c == "todo":
        return todo(con, args.n)
    if c == "stats":
        return stats(con)
    if c == "check":
        return check(con)
    ap.error("unknown command " + c)


if __name__ == "__main__":
    sys.exit(main())
