"""Pair functions between two games by the constants they use (for builds whose machine code
differs, e.g. World Tour unoptimised vs Revenge optimised).

    py tools/sym/match_consts.py [--src wcw] [--dst revenge] [--min 0.6] [--out FILE.tsv]

For every function, the set of numeric literals in its pseudocode is collected, leaving out
addresses (0x8xxxxxxx and their signed forms), small numbers (< 16) and the wrestler strides.
Each source function is paired with the destination function of highest Jaccard similarity
when that score is at least --min, clearly ahead of the runner-up, and the pairing is mutual.
Prints the pairs for documented source functions (name, both addresses, score, size ratio).
"""
import argparse
import json
import os
import re
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import symdb  # noqa: E402

NUM = re.compile(r"(?<![\w.])(-?0x[0-9A-Fa-f]+|-?\d+)(?:LL|u|i64)?\b")
SKIP = {928, 944, 2852}


def consts(code):
    out = set()
    for line in code.split("\n"):
        s = line.strip()
        if s.startswith("//") or "; //" in s:
            continue
        for m in NUM.findall(re.sub(r"\b\w+_[0-9A-F]{6,}\b", "", s)):
            v = int(m, 0)
            v &= 0xFFFFFFFF
            if v >= 0x80000000 and v < 0x80400000:
                continue
            if v >= 0xFFFF0000 or v < 16 or v in SKIP:
                continue
            if 2146000000 <= v <= 2148000000:
                continue
            out.add(v)
    return frozenset(out)


CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")


def call_seq(fn, by_name):
    """Called functions in the order they appear in the pseudocode (first occurrence only)."""
    seq, seen = [], set()
    body = fn["pseudocode"].split("\n", 2)[-1]
    for name in CALL.findall(body):
        a = by_name.get(name)
        if a is not None and a not in seen and a != fn["addr"]:
            seen.add(a)
            seq.append(a)
    return seq


def propagate(pairs, src, dst):
    """Match callees of matched pairs position by position when both call the same number of
    distinct functions; repeat until nothing new. A destination is used once."""
    def names(funcs):
        m = {}
        for a, f in funcs.items():
            m[f["name"]] = a
            m[re.sub(r"_FFFFFFFF(?=[0-9A-F]{8})", "_", f["name"])] = a
        return m
    ns, nd = names(src), names(dst)
    seq_s = {a: call_seq(f, ns) for a, f in src.items()}
    seq_d = {a: call_seq(f, nd) for a, f in dst.items()}
    m = {a: d for a, d, _ in pairs}
    used = set(m.values())
    score = {a: s for a, _, s in pairs}
    changed = True
    while changed:
        changed = False
        for a, d in list(m.items()):
            ss, sd = seq_s.get(a, []), seq_d.get(d, [])
            if len(ss) != len(sd) or not ss:
                continue
            for x, y in zip(ss, sd):
                if x in m or y in used:
                    continue
                if not (0.4 <= (dst[y]["size"] + 16) / (src[x]["size"] + 16) <= 2.5):
                    continue
                m[x] = y
                used.add(y)
                score[x] = 0.0  # propagated
                changed = True
    return [(a, d, score[a]) for a, d in m.items()]


def load(game):
    with open(os.path.join(REPO, "_tools", "ida_" + game, "export.json"), encoding="utf-8") as f:
        return {fn["addr"]: fn for fn in json.load(f)["funcs"]}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", default="wcw")
    ap.add_argument("--dst", default="revenge")
    ap.add_argument("--min", type=float, default=0.6)
    ap.add_argument("--out")
    args = ap.parse_args()
    src, dst = load(args.src), load(args.dst)
    cs = {a: consts(f["pseudocode"]) for a, f in src.items()}
    cd = {a: consts(f["pseudocode"]) for a, f in dst.items()}
    cs = {a: c for a, c in cs.items() if len(c) >= 3}
    cd = {a: c for a, c in cd.items() if len(c) >= 3}
    inv = defaultdict(set)
    for a, c in cd.items():
        for v in c:
            inv[v].add(a)

    def best(c, pool_inv, pool):
        score = defaultdict(int)
        for v in c:
            for a in pool_inv[v]:
                score[a] += 1
        ranked = sorted(((n / len(c | pool[a]), a) for a, n in score.items()), reverse=True)
        return ranked[:2]

    inv_s = defaultdict(set)
    for a, c in cs.items():
        for v in c:
            inv_s[v].add(a)
    documented = {f["addr"]: f["name"] for f in symdb.Symbols(args.src).funcs}
    pairs = []
    for a, c in cs.items():
        r = best(c, inv, cd)
        if not r or r[0][0] < args.min or (len(r) > 1 and r[1][0] > r[0][0] - 0.15):
            continue
        d = r[0][1]
        back = best(cd[d], inv_s, cs)
        if not back or back[0][1] != a:
            continue
        pairs.append((a, d, r[0][0]))
    print("constant pairs:", len(pairs))
    pairs = propagate(pairs, src, dst)
    doc = [p for p in pairs if p[0] in documented]
    print("pairs: %d, of documented %s functions: %d" % (len(pairs), args.src, len(doc)))
    lines = ["%s\t0x%08X\t0x%08X\t%.2f\t%d/%d" % (documented.get(a, "sub_%08X" % a), a, d, s,
                                                  src[a]["size"], dst[d]["size"]) for a, d, s in sorted(pairs)]
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write("name\tsrc\tdst\tscore\tsizes\n" + "\n".join(lines) + "\n")
        print("wrote", args.out)
    else:
        print("\n".join(l for l in lines if not l.startswith("sub_")))


if __name__ == "__main__":
    main()
