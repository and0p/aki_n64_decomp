"""Record in the symbol files which No Mercy functions the decompilation has ported.

    py tools/decomp/port_status.py          # update _re_docs/symbols/nomercy/*.toml
    py tools/decomp/port_status.py --list   # print the table only
    py tools/decomp/port_status.py --protos # also write each ported function's C prototype into `proto`,
                                            # and mark a fully ported function `confirmed`

Scans src/nomercy/*.cpp for the `// N64 0x800E5890` tag in front of each function and sets that
function's `port` (the file). A function whose body still has an NM_UNPORTED path (a branch the
traces have not needed yet) also gets `notes = "Port: partial ..."`; when the last such path is
ported the note goes away on the next run. Notes not written by this tool are left alone.
"""
import argparse
import glob
import os
import re

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(REPO, "src", "nomercy")
SYMS = os.path.join(REPO, "_re_docs", "symbols", "nomercy")
TAG = re.compile(r"^// N64 (0x[0-9A-Fa-f]{8})", re.M)
PARTIAL = "Port: partial (paths the traces have not reached yet stop a replay with NM_UNPORTED)."


def is_stub(body):
    """True when a function does nothing but stop at NM_UNPORTED."""
    code = re.sub(r"//.*", "", body)
    code = code[code.find("{") + 1:code.rfind("}")]
    stmts = [x.strip() for x in code.split(";") if x.strip()]
    return all(x.startswith("(void)") or x.startswith("NM_UNPORTED") for x in stmts)


SIG = re.compile(r"^(?:static\s+|inline\s+)*([A-Za-z_][\w ]*?(?:\s*\*)?)\s*\b([A-Za-z_]\w*)\(([^)]*)\)\s*\{", re.M | re.S)


def c_proto(sig_type, name, params):
    """A C++ signature as the symbol files write prototypes (C style: struct tags, no defaults)."""
    def fix(s):
        s = " ".join(s.split())
        for st in ("Wrestler", "WrestlerCtx", "Vec3", "MoveRecord", "Body", "Contact", "ModelBlock", "AnimStream",
                   "ModelNode", "InputSlot", "Pad"):
            s = re.sub(r"(?<![\w.])%s(?=[\s*\[])" % st, "struct " + st, s)
        s = s.replace("nullptr", "0")
        return s
    ps = [fix(x) for x in params.split(",")] if params.strip() else ["void"]
    rt = fix(sig_type)
    return "%s%s%s(%s)" % (rt, "" if rt.endswith("*") else " ", name, ", ".join(ps))


def scan():
    """{addr: (file, partial, proto)} from the source tags."""
    out = {}
    for path in sorted(glob.glob(os.path.join(SRC, "*.cpp"))):
        text = open(path, encoding="utf-8").read()
        rel = os.path.relpath(path, REPO).replace("\\", "/")
        tags = list(TAG.finditer(text))
        for i, m in enumerate(tags):
            # The function's body: from its first '{' after the tag to the next tag (or the end).
            end = tags[i + 1].start() if i + 1 < len(tags) else len(text)
            body = text[m.end():end]
            addr = int(m.group(1), 16)
            partial = "NM_UNPORTED" in body
            if partial and is_stub(body):
                continue  # only a marker so far: not ported
            proto = None
            sig = SIG.search(body)
            if sig and "(" not in sig.group(1):
                proto = c_proto(sig.group(1), sig.group(2), sig.group(3))
            if addr in out:  # a second definition at the same address (e.g. a memory variant)
                partial = partial or out[addr][1]
                proto = out[addr][2] or proto
            out[addr] = (rel, partial, proto)
    return out


def update(ports, protos=False):
    done = set()
    for path in sorted(glob.glob(os.path.join(SYMS, "*.toml"))):
        lines = open(path, encoding="utf-8").read().split("\n")
        out, i, changed = [], 0, False
        while i < len(lines):
            line = lines[i]
            if line.strip() != "[[func]]":
                out.append(line)
                i += 1
                continue
            j = i + 1
            while j < len(lines) and lines[j].strip() and not lines[j].startswith("[["):
                j += 1
            block = lines[i:j]
            addr = None
            for b in block:
                m = re.match(r"addr\s*=\s*(0x[0-9A-Fa-f]+)", b)
                if m:
                    addr = int(m.group(1), 16)
            if addr in ports:
                file, partial, proto = ports[addr]
                new = [b for b in block if not b.startswith("port =") and not b.startswith('notes = "Port:')]
                if protos and proto:
                    has = any(b.startswith("proto =") for b in new)
                    new = [b for b in new if not b.startswith("proto =")]
                    k = [i for i, b in enumerate(new) if b.startswith("name =")][0] + 1
                    new.insert(k, 'proto = "%s"' % proto)
                    if not partial:
                        new = [('status = "confirmed"' if b.startswith("status =") else b) for b in new]
                    del has
                new.append('port = "%s"' % file)
                if partial and not any(b.startswith("notes =") for b in new):
                    new.append('notes = "%s"' % PARTIAL)
                if new != block:
                    changed = True
                block = new
                done.add(addr)
            out += block
            i = j
        if changed:
            open(path, "w", encoding="utf-8", newline="\n").write("\n".join(out))
    return done


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--protos", action="store_true")
    a = ap.parse_args()
    ports = scan()
    if a.list:
        for addr, (file, partial, proto) in sorted(ports.items()):
            print("%08X %-28s %-8s %s" % (addr, file, "partial" if partial else "full", proto or ""))
        return
    done = update(ports, a.protos)
    missing = sorted(set(ports) - done)
    full = sum(1 for f, p, _ in ports.values() if not p)
    print("%d functions ported (%d full, %d partial); %d tags have no [[func]] entry: %s" %
          (len(ports), full, len(ports) - full, len(missing), " ".join("%08X" % m for m in missing)))


if __name__ == "__main__":
    main()
