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


# The AI block's fields, as nm_ai.cpp names them.
AI_FIELDS = {0x00: "AI_MODE", 0x04: "AI_STEP", 0x08: "AI_TIMER", 0x0C: "AI_HELD", 0x0E: "AI_PRESSED", 0x10: "AI_PENDING",
             0x12: "AI_STICK_X", 0x13: "AI_STICK_Y", 0x18: "AI_MODE_TIMER", 0x1C: "AI_LEVEL", 0x20: "AI_SKILL",
             0x24: "AI_SIT_SELF", 0x3C: "AI_HEALTH", 0x3E: "AI_SPIRIT", 0x40: "AI_QUAD_SELF", 0x42: "AI_ZONE_SELF",
             0x44: "AI_LAST_STATE", 0x48: "AI_SIT_OPP", 0x4C: "AI_OPP_HEALTH", 0x4E: "AI_OPP_SPIRIT", 0x50: "AI_OPP_DOWN",
             0x52: "AI_QUAD_OPP", 0x54: "AI_ZONE_OPP", 0x56: "AI_OPP_STATE", 0x58: "AI_APRON_TIME", 0x5C: "AI_RUN_TIME",
             0x5E: "AI_FOCUS_COUNT", 0x60: "AI_DIST_P", 0x64: "AI_ANGLE", 0x68: "AI_DIST", 0x7C: "AI_BODY_DIST",
             0x80: "AI_FLAGS", 0x82: "AI_WHERE", 0x86: "AI_RETURN_MODE", 0x88: "AI_MOVESET", 0x8C: "AI_LISTS", 0x90: "AI_HEIGHT_DIFF"}
AI_BLOCK = r"\(int\)dword_FFFFFFFF8015A6F0"
AI_TYPES = {"_DWORD": "mem_s32", "int": "mem_s32", "_WORD": "mem_u16", "__int16": "mem_s16", "unsigned __int16": "mem_u16",
            "_BYTE": "mem_u8", "unsigned __int8": "mem_u8", "char": "mem_s8", "float": "mem_f32"}


def clean_ai(code):
    def field(off):
        n = int(off, 0)
        return "blk(%s)" % AI_FIELDS.get(n, hex(n))
    code = re.sub(r"\*\((_DWORD|_WORD|_BYTE|__int16|unsigned __int16|unsigned __int8|char|float|int) \*\)\(" + AI_BLOCK + r" \+ (0x[0-9A-Fa-f]+|\d+)\)",
                  lambda m: "%s(%s)" % (AI_TYPES[m.group(1)], field(m.group(2))), code)
    code = re.sub(r"\*\((_DWORD|_WORD) \*\)" + AI_BLOCK, lambda m: "%s(blk(AI_MODE))" % AI_TYPES[m.group(1)], code)
    code = code.replace("dword_FFFFFFFF8016B92C", "g_ai_partner").replace("dword_FFFFFFFF80159618", "g_ai_opp")
    code = re.sub(r"\*\((_DWORD|_WORD|_BYTE|__int16|unsigned __int16|unsigned __int8|char) \*\)\(self \+ (0x[0-9A-Fa-f]+|\d+)\)",
                  lambda m: "S%s(%s)" % (AI_TYPES[m.group(1)][4:], m.group(2)), code)
    code = re.sub(r"\*\((_DWORD|_WORD|_BYTE|__int16|unsigned __int16|unsigned __int8|char) \*\)\(w \+ (0x[0-9A-Fa-f]+|\d+)\)",
                  lambda m: "O%s(%s)" % (AI_TYPES[m.group(1)][4:], m.group(2)), code)
    return code


def clean(code):
    code = re.sub(r"(\b0x[0-9A-Fa-f]+|\b\d+)LL\b", r"\1", code)
    code = clean_ai(code)
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
