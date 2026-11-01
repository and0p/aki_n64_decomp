"""Spread function names from one game to another along the call graph.

    py tools/sym/match_calls.py [--src wcw] [--dst revenge] [--out FILE.tsv] [--rounds 8]

Seeds are the documented destination functions that have a source function's name, or name
one in their description ("(state_09_turnbuckle)", "(0x800A7C80)", "World Tour mode 12"). For every seeded pair the calls
in each function's pseudocode are read in order; runs of calls between two already-paired calls
(or the ends) that have the same length are paired by position. A candidate is accepted when all
its votes agree and it does not clash with another pairing; accepted pairs become seeds for the
next round. Prints the new pairs (source name, both addresses, votes).
"""
import argparse
import os
import re
import sqlite3
import sys
from collections import defaultdict


HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from match_consts import consts  # noqa: E402

CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
SUB = re.compile(r"^(?:sub|func)_([0-9A-Fa-f]{8})$")


def load(game):
    c = sqlite3.connect(os.path.join(REPO, "_tools", "sym", game + ".sqlite"))
    funcs = {a: (n, d) for a, n, d in c.execute("select addr, name, documented from func")}
    descs = {a: t or "" for a, t in c.execute("select addr, desc from func where documented = 1")}
    sizes = {a: z for a, z in c.execute("select addr, size from func")}
    cs = {}
    by_name = {n: a for a, (n, d) in funcs.items()}
    calls = {}
    for a, text in c.execute("select addr, text from code"):
        cs[a] = consts(text)
        seq = []
        for line in text.split("\n")[1:]:
            if "; //" in line or line.strip().startswith("//"):
                continue
            for m in CALL.findall(line):
                s = SUB.match(m)
                t = int(s.group(1), 16) if s else by_name.get(m)
                if t is not None and t in funcs and t != a:
                    seq.append(t)
        # repeated calls to the same function count once, in order of first use
        seen, out = set(), []
        for t in seq:
            if t not in seen:
                seen.add(t)
                out.append(t)
        calls[a] = out
    return funcs, by_name, calls, descs, sizes, cs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="wcw")
    ap.add_argument("--dst", default="revenge")
    ap.add_argument("--out")
    ap.add_argument("--rounds", type=int, default=8)
    ap.add_argument("--min-ratio", type=float, default=0.6, help="smallest size ratio")
    ap.add_argument("--min-score", type=float, default=0.9)
    ap.add_argument("--margin", type=float, default=0.15)
    args = ap.parse_args()
    sf, sn, sc, _, ssz, scs = load(args.src)
    df, dn, dc, dd, dsz, dcs = load(args.dst)

    pairs = {}  # src addr -> dst addr
    scores = {}
    modes = {}
    for a, (n, d) in sf.items():
        m = re.match(r"ai_mode_(\d+)(?:_|$)", n)
        if m:
            modes[int(m.group(1))] = a
    for a, (n, d) in df.items():
        if not d:
            continue
        # the same name, or the source function named in the description:
        # "(state_09_turnbuckle)", "(0x800A7C80)", "World Tour mode 12"
        src = sn.get(n)
        desc = dd.get(a, "")
        for m in re.finditer(r"\((\w+)\)|\((0x8[0-9A-Fa-f]{7})\)", desc):
            if m.group(1) in sn:
                src = sn[m.group(1)]
            elif m.group(2) and int(m.group(2), 16) in sf:
                src = int(m.group(2), 16)
        m = re.search(r"World Tour mode (\d+)", desc)
        if m and int(m.group(1)) in modes:
            src = modes[int(m.group(1))]
        if src is not None and src not in pairs:
            pairs[src] = a
    seeds = dict(pairs)
    rev = {v: k for k, v in pairs.items()}
    scall = {a: set(v) for a, v in sc.items()}
    dcall = {a: set(v) for a, v in dc.items()}
    scaller, dcaller = defaultdict(set), defaultdict(set)
    for a, v in scall.items():
        for t in v:
            scaller[t].add(a)
    for a, v in dcall.items():
        for t in v:
            dcaller[t].add(a)

    def jac(a, b):
        return len(a & b) / len(a | b) if a | b else 0.0

    def score(x, y):
        # constants, size, and agreement of the already-paired callers and callees
        r = dsz.get(y, 1) / max(ssz.get(x, 1), 1)
        if not args.min_ratio <= r <= 1 / args.min_ratio:
            return -1
        a, b = scs.get(x, frozenset()), dcs.get(y, frozenset())
        k = jac(a, b) if a and b else (0.1 if not a and not b else 0.0)
        nb = 0.0
        for sx, dy in ((scall.get(x, set()), dcall.get(y, set())), (scaller[x], dcaller[y])):
            known = [pairs[t] for t in sx if t in pairs]
            if known:
                agree = sum(1 for t in known if t in dy) / max(len(known), len([t for t in dy if t in rev]))
                # paired callees that the candidate does not call at all count against it
                nb += agree if agree or sx is not scall.get(x) else -1.0
        # shape: the sizes of the callees, sorted, compared pairwise (and their count)
        z1 = sorted(ssz.get(t, 0) for t in scall.get(x, ()))
        z2 = sorted(dsz.get(t, 0) for t in dcall.get(y, ()))
        n = max(len(z1), len(z2))
        if n:
            sims = [min(p, q) / max(p, q, 1) for p, q in zip(z1, z2)]
            shape = sum(sims) / n
        else:
            shape = 1.0
        return k + 0.5 * nb + 0.4 * shape + 0.2 * min(r, 1 / r)

    for rnd in range(args.rounds):
        cand = defaultdict(set)
        for s, d in list(pairs.items()):
            for xs, ys in ((scall.get(s, set()), dcall.get(d, set())), (scaller[s], dcaller[d])):
                for x in xs:
                    if x in pairs:
                        continue
                    for y in ys:
                        if y not in rev:
                            cand[x].add(y)
        best = {}
        for x, ys in cand.items():
            sc_ = sorted(((score(x, y), y) for y in ys), reverse=True)
            if sc_ and sc_[0][0] >= args.min_score and (len(sc_) == 1 or sc_[0][0] - sc_[1][0] >= args.margin):
                best[x] = sc_[0]
        # keep only mutual bests
        back = defaultdict(list)
        for x, (v, y) in best.items():
            back[y].append((v, x))
        new = 0
        for y, xs in back.items():
            xs.sort(reverse=True)
            if len(xs) == 1 or xs[0][0] - xs[1][0] >= args.margin:
                v, x = xs[0]
                pairs[x] = y
                rev[y] = x
                scores[x] = v
                new += 1
        if not new:
            break
    rows = []
    for x, y in pairs.items():
        if x in seeds:
            continue
        sname, sdoc = sf[x]
        rows.append((sname, x, y, sdoc, scores.get(x, 0)))
    rows.sort(key=lambda r: (not r[3], r[1]))
    lines = ["%s	%08X	%08X	%s	%.2f" % (n, x, y, "doc" if d else "-", v) for n, x, y, d, v in rows]
    print("seeds %d, new %d (documented in %s: %d)" % (len(seeds), len(rows), args.src, sum(1 for r in rows if r[3])))
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
    else:
        print("\n".join(lines))


if __name__ == "__main__":
    main()
