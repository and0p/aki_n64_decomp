"""Merge Revenge -> No Mercy name matches into _re_docs/symbols/nomercy/from_revenge.toml.

    py tools/sym/merge_matches.py GAMES.toml CONSTS.tsv [CALLS.tsv ...]

Inputs are the outputs of match_games.py (--out FILE.toml), match_consts.py and match_calls.py
(--out FILE.tsv), all run with --src revenge --dst nomercy. A pair is dropped when its address or
name is claimed by another pair, or is already named in another No Mercy symbol file. Re-run
match_calls.py after each merge (sym.py re-indexes first) until it finds nothing new.
"""
import sqlite3, sys, tomllib, os, re
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.chdir(REPO)
rv = sqlite3.connect("_tools/sym/revenge.sqlite")
rdesc = {n: (a, d or "") for a, n, d in rv.execute("select addr,name,desc from func where documented=1")}
sys.path.insert(0, os.path.join(REPO, "tools", "sym"))
import symdb  # noqa: E402
sym_dir = symdb.symbols_dir("nomercy")
own = {}  # read directly: symdb refuses to load while from_revenge.toml still clashes
for fn in os.listdir(sym_dir):
    if fn.endswith(".toml") and fn != "from_revenge.toml":
        with open(os.path.join(sym_dir, fn), "rb") as fp:
            own.update((f["addr"], f["name"]) for f in tomllib.load(fp).get("func", []))
cands = []  # (dst, name, method, status)
g = tomllib.load(open(sys.argv[1], "rb"))
for f in g.get("func", []):
    cands.append((f["addr"], f["name"], "identical machine code", "likely"))
for line in open(sys.argv[2], encoding="utf-8").read().splitlines()[1:]:
    n, s, d, sc, sz = line.split("\t")
    if n.startswith("sub_"): continue
    sc = float(sc); a, b = map(int, sz.split("/"))
    st = "likely" if sc >= 0.8 and min(a, b) / max(a, b) >= 0.6 else "guess"
    how = "constants (score %.2f)" % sc if sc > 0 else "constants, through paired neighbours"
    cands.append((int(d, 16), n, how, st))
for p in sys.argv[3:]:
    for line in open(p, encoding="utf-8").read().splitlines()[1:]:
        parts = line.split("\t")
        n, s, d = parts[0], parts[1], parts[2]
        if n.startswith("sub_"): continue
        sc = float(parts[-1]) if re.match(r"^[\d.]+$", parts[-1]) else 1.0
        cands.append((int(d, 16), n, "call graph (score %.2f)" % sc, "likely" if sc >= 1.0 else "guess"))
by_dst, by_name = {}, {}
for d, n, how, st in cands:
    by_dst.setdefault(d, set()).add(n); by_name.setdefault(n, set()).add(d)
out, seen, dropped = [], set(), 0
for d, n, how, st in cands:
    if len(by_dst[d]) > 1 or len(by_name[n]) > 1 or d in own or n in own.values():
        dropped += (d, n) not in seen; seen.add((d, n)); continue
    if (d, n) in seen: continue
    seen.add((d, n)); out.append((d, n, how, st))
out.sort()
with open("_re_docs/symbols/nomercy/from_revenge.toml", "w", encoding="utf-8", newline="\n") as f:
    f.write("# No Mercy functions named after their Revenge match (tools/sym/match_games.py, match_consts.py,\n"
            "# match_calls.py, --src revenge --dst nomercy). The first method that found a pair is named in desc.\n"
            "# `guess` entries are unchecked; when one is checked, move it to its subsystem file.\n")
    for d, n, how, st in out:
        ra, rd = rdesc.get(n, (0, ""))
        desc = "As Revenge %s (0x%08X), matched by %s. Revenge: %s" % (n, ra, how, rd)
        f.write('\n[[func]]\naddr = 0x%08X\nname = "%s"\ndesc = %s\nstatus = "%s"\n' % (d, n, '"' + desc.replace("\\", "\\\\").replace('"', '\\"') + '"', st))
print("written", len(out), "dropped (conflicts or already named)", dropped)
