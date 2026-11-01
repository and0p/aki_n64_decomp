# World Tour MIPS IDA database

A static IDA database of the game's own MIPS code. It gives Hex-Rays pseudocode with our names
and struct types, independent of the recomp and of any live debug session.

```bash
py tools/ida/wcw_build_idb.py
```

The build reads the US ROM from `_tools/WCWRecompiled/` (pass `--rom` to use another path) and
writes `_tools/ida_wcw/wcw_ovl_b.i64`. That folder is git-ignored because the database is
built from the ROM. A build takes about a minute with `idat.exe` from IDA 9.2.

## What goes in

| N64 range | Contents | Source |
|-----------|----------|--------|
| `0x80000400`–`0x8002A3B0` | main code | ROM `0x1000` |
| `0x8002A3B0`–`0x800387F0` | main data | |
| `0x800387F0`–`0x80062090` | main bss (empty) | |
| `0x80090000`–`0x800E5F70` | `ovl_b` code (the match overlay) | ROM `0xA69570` |
| `0x800E5F70`–`0x800E9400` | `ovl_b` data | |
| `0x800E9400`–`0x800F8AC0` | `ovl_b` bss: the wrestler array, clock, move tables… (empty) | |

The segment bounds come from the recomp's splat config (`disasm/wcw.yaml`). `ovl_a`, the
menus and front-end overlay, also loads at `0x80090000`. It is left out because the menus are
out of scope.

## Setup choices (following the ida-recomp-toolkit approach)

- **64-bit segments:** IDA 9.2's MIPS decompiler refuses 32-bit code ("only 64-bit functions
  can be decompiled"). The segments are therefore 64-bit, while pointers in our types are
  declared `__ptr32`, since the game uses the 32-bit o32 ABI.
- **Sign-extended addresses:** MIPS64 sign-extends `lui 0x8xxx`, so the database is rebased
  to `0xFFFFFFFF80000000`. Globals then resolve to names in the pseudocode. Our symbol files
  still use plain N64 addresses, and the setup script widens them.
- **Function discovery:** functions are seeded from every `jal` target and every IDO
  prologue (`addiu sp,sp,-N` straight after `jr ra`), then IDA's auto-analysis runs. Result:
  1178 functions.
- **Symbols:** names, types, prototypes and descriptions come from `_re_docs/symbols/wcw/`
  (the TOML files and `types.h`). Add new findings there and rebuild.
- **Pointer arguments:** each documented prototype with pointer arguments is decompiled twice,
  once with native pointers and once with `__ptr32`, and the build keeps whichever resolves
  more struct fields. `__ptr32` only sticks when the calling convention is explicit, so the
  build adds `__fastcall`.
- **Export:** at the end of the build, callers, callees, data xrefs, struct-field reads and
  writes, and pseudocode for all functions are written to `_tools/ida_wcw/export.json`.
  `tools/sym/` indexes that file.

## Known rough edges

- Table lookups written as `lui` + an indexed address sometimes show up as
  `x - 2146566144 + 0x74FF` instead of a symbol. 2146566144 is `0x800E0000`, so add the two
  numbers to get the address.
- Functions return through `((__int64 (*)(void))varsN)()`. That is how Hex-Rays renders the
  IDO epilogue here; read it as `return`.
- Some functions warn "local variable allocation has failed". The output is still usable.
- Functions that copy a pointer argument to the stack first (IDO's unoptimised pattern, for
  example `wrestler_set_anim`) lose the struct type and show raw offsets such as
  `*(_WORD *)((int)w + 8LL)`. Field xrefs miss those accesses.
- Field xrefs only cover expressions whose type Hex-Rays knows: our typed globals such as
  `g_cur_wrestler`, and documented prototypes. Writing more prototypes widens the coverage.

## First thing it showed

`state_42_attack_move` (the attacker's state while a move plays) contains what looks like a
**reversal**. If the opponent has flag `0x80000` in `flags_lo` and the attacker's `+0x006`
equals byte 14 of its animation player, the attacker takes the receiver's animation from the
*opponent's* move set and switches to state 0x43. In other words the roles swap. Not
confirmed yet, but it is the clearest lead on how counters work.
