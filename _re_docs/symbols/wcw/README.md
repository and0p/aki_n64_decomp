# World Tour symbol files

The hand-maintained source of truth for what we know about the game's code. The query tool
(`tools/sym/sym.py`) and the IDA database build both read these files. Callers, callees and
xrefs are **not** written here; they are computed from the database by `tools/sym/build.py`.

- `types.h`: recovered structs. Each field's trailing comment is its description.
- `*.toml`: one file per subsystem, holding functions, globals and enums. `bulk.toml` holds the
  short helpers named in one pass over the match overlay (2026-10-31); move an entry to its
  subsystem file when it is studied properly.

## Entry format

```toml
[[func]]
addr   = 0x800A0860            # N64 address (overlay functions are ovl_b addresses)
name   = "wrestler_add_spirit"
proto  = "void wrestler_add_spirit(struct Wrestler *w, s16 delta)"   # plain C
desc   = "One line: what it does."
status = "confirmed"           # confirmed | likely | guess
notes  = "Optional longer notes, edge cases, open questions."
port   = ""                    # later: where the port implements it, e.g. "src/match/spirit.cpp"

[[data]]
addr = 0x800F16EC
name = "g_match_clock"
type = "struct MatchClock"     # plain C, may be an array: "s16[4]"
desc = "..."
status = "confirmed"

[[enum]]
name = "WrestlerState"
desc = "..."
values = { ATTACK_MOVE = 0x42, RECEIVE_MOVE = 0x43 }
```

Status values: `confirmed` means it is backed by code reading, or by code plus a memory
snapshot. `likely` means there is strong but indirect evidence. `guess` is anything weaker.
