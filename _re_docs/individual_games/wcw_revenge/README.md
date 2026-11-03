# WCW/nWo Revenge

Started 2026-10-31. Written as differences from World Tour (`../wcw_world_tour/`); anything
not mentioned in these pages is not checked yet, not known to be the same.

## Pages

| Page | Contents |
|------|----------|
| `states.md` | the 146 action states: alignment with World Tour, the new states and what starts them, a CPU vs CPU run |
| `wrestler.md` | the wrestler struct, the flag bits, the 64-byte header and its trait bits |
| `strikes.md` | R counters, the three kinds of strike counter, the strike combos |
| `weapons.md` | weapon types, objects, holding flags, fetching, stealing |
| `matches.md` | match types and rules, falls, run-ins, the battle royal, tag, pins and mashing |
| `ai.md` | the CPU AI: modes, reflexes, favourite moves, level |
| `moves.md` | move sets, roster words, input slots (fitted to the GameFAQs guide) |
| `controls.md` | controls and unlocks |
| `models.md` | wrestler models, attires, palette recolouring (skin, two colours), the previewer |
| `arena.md` | the 6 arenas |

## Tooling

- IDA database: `py tools/sym/build.py --game revenge --ida` (runs
  `tools/ida/revenge_build_idb.py`, which reuses `tools/ida/wcw_idb_setup.py` with
  `AKI_GAME=revenge`). Output in `_tools/ida_revenge/` (git-ignored). Function starts come
  from the Revenge recomp's `syms/dump.toml`; 1,336 functions, all decompile.
- Symbols: `_re_docs/symbols/revenge/` (same format as World Tour's), queried with
  `py tools/sym/sym.py --game revenge ...`. `types.h` is a copy of World Tour's and is
  **not** valid for Revenge yet (the wrestler struct is laid out differently, below).
- `tools/sym/match_games.py` matches functions with identical normalised machine code. Between
  World Tour and Revenge it finds only 12, because **Revenge is compiled with optimisation**
  (World Tour's functions spill every value to the stack; Revenge keeps them in registers,
  e.g. `rng_match` is 136 bytes in World Tour and 100 in Revenge). So matching has to go by
  behaviour: constants, tables, call shape. The script may work better between Revenge and the
  later games.

## Memory layout

From the recomp's splat config (`_recomps/WCWnWoRevengeRecomp/disasm/revenge.yaml`):

| Piece | ROM | VRAM | World Tour |
|-------|-----|------|------------|
| main (code with data pockets) | 0x1000–0x3C770 | 0x80000400–0x8003BB70 | 0x80000400–0x80062090 incl. bss |
| ovl_a (menus?) | 0x3C770–0x834A0 | 0x80090000 | |
| **ovl_b (the match)** | 0x834A0–0xDAC50 | text 0x80090000–0x800E2B10, data –0x800E77B0, bss –0x800FAFA0 | text –0x800E5F70, data –0x800E9400, bss –0x800F8AC0 |

The two overlays share 0x80090000, as in World Tour (the recomp's header comment saying "no
overlays" is out of date; its segment list has both). The IDA build loads only main + ovl_b.
`ovl_b` is the match overlay: World Tour's `rng_match` multiplier is there.

## First landmarks

| What | Revenge | World Tour | Difference |
|------|---------|------------|------------|
| `rng_match` (LCG ×0x5D588B65) | 0x800D4204 | 0x800A0468 | same algorithm |
| `wrestler_update` | 0x8009D8D0 | 0x800A7C80 | |
| `g_state_handlers` | 0x800E2FF0, **146 states** | 0x800E723C, 120 | 26 more action states |
| `ai_think` | 0x800A9924 | 0x800B5780 | |
| AI mode table | 0x800E3660, **62 modes** | 0x800E7AE0, 48 | 14 more AI modes |
| `g_wrestlers` | 0x800F9D10, **944 bytes each** | 0x800F07F0, 928 | 16 bytes more per wrestler |
| model joints per wrestler | 2,852 bytes | 2,852 | same |

The wrestler struct is laid out differently; see below.

## Assets: same archive format, reordered files

- Same archive: u32 offset table with bit 0 = LZSS, files relative to a data base. Revenge:
  **7,657 files**, table at ROM `0xCE2752` (copied by `func_800034A0`), data at `0xDAC50`
  (loader `func_80003684`). `tools/rom/aki_archive.py` now detects the game from the ROM hash;
  `verify` passes for all 7,657.
- Animations use the same 98-byte keyframe layout (4,017 files from 3,641 on parse as
  animations). **661 of World Tour's 2,598 animations are byte-identical** in Revenge, but
  renumbered (no single offset; +854, +896, +1883 … in places), so animation ids differ between
  the games and the asset converter needs a per-game id table.
- Texture and palette runs look the same as World Tour's; models and move sets not checked.

## Function matching with World Tour

Tools: `tools/sym/match_consts.py` pairs functions by their constants and propagates through
calls (162 pairs, mostly rendering and loading code); the state alignment was done with the
same constant sets and an order-keeping alignment (not yet a script).

`tools/sym/match_calls.py` (2026-11-01) spreads names from the matched functions along the
call graph: each unpaired caller / callee of a pair is scored against the neighbours of its
partner by shared constants, size, the sizes of its callees, and how many of its already-paired
callers and callees agree; mutual best pairs with a clear margin are taken, round after round.
Results in `symbols/revenge/from_wcw.toml` (272 names; score >= 1.0 `likely`, below `guess`).

- **Accuracy.** A first, looser version (constant-free functions scored as similar, size ratio
  down to 0.4) got about 1 in 25 wrong on small functions, mainly where the optimised build
  inlined a helper (`angle_diff` landed on a function that inlines it; `recover_tick`, which
  Revenge has twice, was taken for `move_texture_stamp`). The current scoring fixes those;
  the few wrong ones found by hand since (`wrestler_add_health`, `chance_check`) are corrected
  in `bulk.toml`. Treat `guess` entries as unchecked.
- **Independent agreement.** 36 state bodies paired by the call graph land exactly on the
  handlers the state-table alignment predicts, so those handlers are marked `likely` in
  `symbols/revenge/states_aligned.toml` (all 146 handlers and their bodies are named there from
  the alignment: 128 new entries, the rest `guess`).

Totals: 537 of 1336 functions documented, **473 of the 653 in the match overlay**. The other
150 or so call-graph pairs are World Tour main-segment functions with no name yet.

## Next steps

1. Finish the wrestler struct and write a Revenge `types.h`.
2. Confirm the weak parts of the state alignment and name the remaining matched handlers;
   look at what the new states do.
3. Map the other ~1,940 Revenge animations to World Tour's (many are probably re-exported
   versions of the same motion: compare decoded poses, not bytes).
4. The AI: confirm the new modes in RAM, find the difficulty table, read the decision tables.
5. Decode the model tables and render Revenge wrestlers in the viewer; then the move-set files.
