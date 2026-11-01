# sym: reverse-engineering knowledge index

There are three layers:

1. **Hand-written** (in git): `_re_docs/symbols/<game>/*.toml` and `types.h`. These hold names,
   prototypes, descriptions, status, notes, port location, struct field comments and enums.
2. **Computed by IDA** (git-ignored): `_tools/ida_<game>/export.json`. It holds every
   function's callers, callees, data xrefs, struct-field reads and writes, and Hex-Rays
   pseudocode. It is produced by the IDA database build.
3. **Index** (git-ignored): `_tools/sym/<game>.sqlite`, which merges layers 1 and 2.

```bash
py tools/sym/build.py --ida
```
That rebuilds the IDA database, the export and the index, and takes about a minute. Every
command takes `--game` (default `wcw`); `--game revenge` uses `tools/ida/revenge_build_idb.py`
and `_re_docs/symbols/revenge/`. After
editing only TOML or `types.h`, `sym.py` re-indexes by itself in a second or so. Rebuild with
`--ida` when prototypes or types change, so the pseudocode and field xrefs pick them up.

## Queries

| Command | Answers |
|---------|---------|
| `sym.py show NAME\|ADDR` | docs, prototype, callers, callees, globals used, fields read and written |
| `sym.py code NAME` | pseudocode |
| `sym.py tree NAME [-d N] [--up]` | call tree down, or callers up |
| `sym.py struct Wrestler` | field layout with descriptions and how many functions touch each field |
| `sym.py field Wrestler.spirit` | functions writing and reading a field |
| `sym.py enum [NAME]` | documented enum values |
| `sym.py find TEXT` | names, descriptions and fields matching TEXT |
| `sym.py todo [-n N]` | undocumented functions most connected to documented ones (what to name next) |
| `sym.py stats` / `check` | coverage, and `types.h` vs IDA layout |

In lists, `*` marks a documented function. Undocumented ones show as `sub_<addr>`.

## Workflow when documenting a function

1. `sym.py todo` or `sym.py show <caller>` to choose a function, then `sym.py code sub_XXXXXXXX`.
2. Add a `[[func]]` entry (and any fields or enums) to the right TOML file or `types.h`.
3. `py tools/sym/build.py --ida` if you added a prototype or type, so it propagates.
4. When the function is ported, set `port = "path/file.cpp"` and tag the C++ code with
   `// N64 0x800A0860`.

## Cross-game matching

- `match_consts.py`: pairs functions of two games by their constant sets.
- `match_calls.py`: spreads names from already-paired functions along the call graph (constants,
  size and agreeing neighbours); output TSV `name, src addr, dst addr, doc, score`.
