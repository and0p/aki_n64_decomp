"""Print a first draft of a No Mercy function in the port's style, from IDA's pseudocode.

    py tools/decomp/hr_draft.py NAME|ADDR [...]

Reads the pseudocode in _tools/ida_nomercy/export.json (types.h applied), strips the casts and sign
extensions Hex-Rays adds for 64-bit MIPS, and names the usual locals. It is a draft to check, not C to
compile. Hex-Rays drops f32 arguments on MIPS (they show as uninitialised v-variables) and the return
through $ra shows as a call, so after each function it lists the float constants and calls in asm order, to
fill those in from. Jump tables and goto structure stay as Hex-Rays leaves them.
"""
import json
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXPORT = os.path.join(REPO, "_tools", "ida_nomercy", "export.json")


def clean(code):
    # Casts that only re-extend a pointer or a value.
    code = re.sub(r"\(const struct (\w+) \*\)\(int\)", "", code)
    code = re.sub(r"\(struct (\w+) \*\)\(int\)", "", code)
    code = re.sub(r"\(\(void \(\*\)\(void\)\)\w+\)\(\);\n", "", code)
    code = re.sub(r"\(__int64\)", "", code)
    code = re.sub(r"(\b0x[0-9A-Fa-f]+|\b\d+)LL\b", r"\1", code)
    code = re.sub(r"(\b0x[0-9A-Fa-f]+)u\b", r"\1", code)
    code = code.replace("ctx->self", "w").replace("ctx->rec_a", "o")
    code = re.sub(r"\*\(_DWORD \*\)\(\(int\)(\w+)->move \+ 0x20\)", r"mem_u32(\1->move + 0x20)", code)
    # The saved-$ra locals and declarations the C does not need.
    code = re.sub(r"\n  (int|__int64) (v\d+|vars\d+); // \[sp[^\n]*", "", code)
    code = re.sub(r"\n  int v\d+; // \$ra", "", code)
    code = re.sub(r"\n  v\d+ = v\d+;\n(?=\s*self)", "\n", code)
    return code


def main(names):
    with open(EXPORT, encoding="utf-8") as f:
        funcs = json.load(f)["funcs"]
    by_name = {x["name"]: x for x in funcs}
    by_addr = {x["addr"]: x for x in funcs}
    for key in names:
        fn = by_name.get(key)
        if fn is None and key.lower().startswith("0x"):
            fn = by_addr.get(int(key, 16))
        if fn is None:
            print("// not found: " + key)
            continue
        print("// N64 0x%X size 0x%X %s" % (fn["addr"], fn["size"], fn["name"]))
        print(clean(fn.get("pseudocode") or "// (no pseudocode)"))
        out = subprocess.run([sys.executable, os.path.join(REPO, "tools", "decomp", "mips_dis.py"), fn["name"]],
                             capture_output=True, text=True).stdout
        notes = []
        for line in out.splitlines():
            m = re.search(r"; (\(f32 [^)]*\)|0x[0-9A-F]{8} = .*)$", line.rstrip())
            call = re.search(r"jal\s+\S+\s+; (\w+)", line)
            if m:
                notes.append(m.group(1))
            elif call:
                notes.append(call.group(1))
        print("// asm order: " + ", ".join(notes))
        print()


if __name__ == "__main__":
    main(sys.argv[1:])
