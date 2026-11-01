"""Build the SQLite query index from the symbol files and IDA's export.

    py tools/sym/build.py            # index only (seconds); uses the last IDA export
    py tools/sym/build.py --ida      # rebuild the IDA database + export first (~1 min)

Inputs:  _re_docs/symbols/<game>/*.toml, types.h      (hand-written, in git)
         _tools/ida_<game>/export.json               (computed by IDA, git-ignored)
Output:  _tools/sym/<game>.sqlite                    (git-ignored)

sym.py calls index() itself when any input is newer than the index.
"""
import argparse
import json
import os
import sqlite3
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import symdb  # noqa: E402

SCHEMA = """
CREATE TABLE func   (addr INTEGER PRIMARY KEY, name TEXT, size INTEGER, documented INTEGER,
                     file TEXT, proto TEXT, desc TEXT, status TEXT, notes TEXT, port TEXT);
CREATE TABLE data   (addr INTEGER PRIMARY KEY, name TEXT, type TEXT, desc TEXT, status TEXT, file TEXT);
CREATE TABLE name   (addr INTEGER PRIMARY KEY, name TEXT);
CREATE TABLE calls  (caller INTEGER, callee INTEGER);
CREATE TABLE dref   (func INTEGER, addr INTEGER);
CREATE TABLE field  (struct TEXT, name TEXT, offset INTEGER, size INTEGER, ctype TEXT, desc TEXT);
CREATE TABLE access (func INTEGER, struct TEXT, offset INTEGER, rw TEXT);
CREATE TABLE enum   (enum TEXT, key TEXT, value INTEGER, desc TEXT);
CREATE TABLE code   (addr INTEGER PRIMARY KEY, text TEXT);
CREATE TABLE meta   (key TEXT PRIMARY KEY, value TEXT);
CREATE INDEX calls_callee ON calls(callee);
CREATE INDEX calls_caller ON calls(caller);
CREATE INDEX dref_addr ON dref(addr);
CREATE INDEX access_field ON access(struct, offset);
CREATE INDEX name_name ON name(name);
"""


def paths(game):
    sym_dir = symdb.symbols_dir(game)
    export = os.path.join(symdb.REPO, "_tools", "ida_" + game, "export.json")
    db = os.path.join(symdb.REPO, "_tools", "sym", game + ".sqlite")
    return sym_dir, export, db


def inputs(game):
    sym_dir, export, _ = paths(game)
    files = [os.path.join(sym_dir, f) for f in os.listdir(sym_dir) if f.endswith((".toml", ".h"))]
    return files + ([export] if os.path.exists(export) else [])


def stale(game):
    _, _, db = paths(game)
    if not os.path.exists(db):
        return True
    t = os.path.getmtime(db)
    return any(os.path.getmtime(p) > t for p in inputs(game))


def tidy(text):
    """IDA's auto names carry the sign-extended address (sub_FFFFFFFF800A7300); drop the prefix."""
    return re.sub(r"_FFFFFFFF(?=[0-9A-F]{8})", "_", text) if text else text


def index(game="wcw", quiet=False):
    sym_dir, export, db = paths(game)
    sym = symdb.Symbols(game)
    ex = {"funcs": [], "structs": {}, "names": {}}
    if os.path.exists(export):
        with open(export, encoding="utf-8") as f:
            ex = json.load(f)
    os.makedirs(os.path.dirname(db), exist_ok=True)
    tmp = db + ".tmp"
    if os.path.exists(tmp):
        os.remove(tmp)
    con = sqlite3.connect(tmp)
    con.executescript(SCHEMA)

    hand = {f["addr"]: f for f in sym.funcs}
    seen = set()
    for f in ex["funcs"]:
        h = hand.get(f["addr"], {})
        seen.add(f["addr"])
        con.execute("INSERT INTO func VALUES (?,?,?,?,?,?,?,?,?,?)",
                    (f["addr"], h.get("name", tidy(f["name"])), f["size"], int(bool(h)), h.get("file"),
                     h.get("proto"), h.get("desc"), h.get("status"), h.get("notes"), h.get("port")))
        con.executemany("INSERT INTO calls VALUES (?,?)", [(f["addr"], c) for c in f["callees"]])
        con.executemany("INSERT INTO dref VALUES (?,?)", [(f["addr"], d) for d in f["drefs"]])
        con.executemany("INSERT INTO access VALUES (?,?,?,?)", [(f["addr"], s, o, rw) for s, o, rw in f["fields"]])
        if f.get("pseudocode"):
            con.execute("INSERT INTO code VALUES (?,?)", (f["addr"], tidy(f["pseudocode"])))
    for addr, h in hand.items():  # documented but not (yet) in the export
        if addr not in seen:
            con.execute("INSERT INTO func VALUES (?,?,?,?,?,?,?,?,?,?)",
                        (addr, h["name"], None, 1, h["file"], h.get("proto"), h.get("desc"),
                         h.get("status"), h.get("notes"), h.get("port")))
    for d in sym.data:
        con.execute("INSERT INTO data VALUES (?,?,?,?,?,?)",
                    (d["addr"], d["name"], d.get("type"), d.get("desc"), d.get("status"), d["file"]))
    for addr, n in ex["names"].items():
        con.execute("INSERT OR REPLACE INTO name VALUES (?,?)", (int(addr), tidy(n)))

    # struct fields: offsets from IDA's layout, descriptions from types.h comments
    descs = {(s, n): (t, c) for s, n, t, c in sym.struct_fields()}
    for sname, lay in ex["structs"].items():
        for name, off, size in lay["members"]:
            t, c = descs.get((sname, name), ("", ""))
            con.execute("INSERT INTO field VALUES (?,?,?,?,?,?)", (sname, name, off, size, t, c))
    for e in sym.enums:
        for k, v in e.get("values", {}).items():
            con.execute("INSERT INTO enum VALUES (?,?,?,?)", (e["name"], k, v, e.get("desc", "")))
    con.execute("INSERT INTO meta VALUES ('game', ?)", (game,))
    con.commit()
    con.close()
    os.replace(tmp, db)
    if not quiet:
        print("indexed %d functions (%d documented), %d globals -> %s" % (
            len(ex["funcs"]), len(hand), len(sym.data), db))
    return db


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", default="wcw")
    ap.add_argument("--ida", action="store_true", help="rebuild the IDA database and export first")
    args = ap.parse_args()
    if args.ida:
        r = subprocess.run([sys.executable, os.path.join(symdb.REPO, "tools", "ida", "%s_build_idb.py" % args.game)])
        if r.returncode:
            return r.returncode
    index(args.game)


if __name__ == "__main__":
    sys.exit(main())
