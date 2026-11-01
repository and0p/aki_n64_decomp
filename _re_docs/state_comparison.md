# Comparing game state with our recreation (ideas)

A sketch, not a plan yet (2026-10-31). The goal from `AGENTS.md`: game logic, AI and animation are
frame-exact with the original. That needs a way to run the original and our library side by side
and say, frame by frame, where they differ. These are the pieces that seem worth building, roughly
in the order they'd pay off.

## The reference: the original game in the emulator

`tools/n64emu` (libretro mupen64plus_next) is the reference runner. It has exact, deterministic
save states, frame stepping and full RAM access through the `aki_debug` HTTP API. The recomp is
less useful as an oracle: its snapshots can't be loaded reliably, and it runs the other N64
threads natively, so its timing isn't the console's.

- **Frame alignment.** The emulator counts a game frame on each framebuffer flip (`0x8002D2B0`).
  In a match there's a better clock: a u32 at `0x800E678C` went up by exactly one per match tick
  over 120 VIs (found by searching RAM; not yet named or traced). Whatever its meaning, traces
  should key on a counter like that, not on VIs.
- **What a frame is.** A trace sample is the state at the start of a frame, before `pads_read`,
  which is where the library's `step()` would begin too. The emulator pauses just after a flip,
  which isn't exactly there; trace the field values against the match tick so the offset doesn't
  matter, or find a better hook point.

## 1. A shared description of "match state"

One machine-readable list of the fields that make up the match, used by both sides:

- generated from `_re_docs/symbols/wcw/` (`types.h` and the TOML globals): name, address, type,
  count, plus a **comparison rule** per field;
- the recorder reads those fields out of RDRAM; the library exports the same names from its own
  structs (its layout can be anything, so the mapping lives in one exporter function);
- the rules: `exact` (ints, flags, states, ids), `bits` (floats compared bit for bit), `ignore`
  (render-only, host pointers, padding), `index` (an N64 pointer converted to what it points at:
  wrestler slot, animation id, move slot), and `unknown` (recorded, reported but not failed, so new
  fields can be watched before they're understood).

Starting set: the clock, `rng_match` state, `g_match_phase` and flags, the four `Wrestler`
records (state, step, position, facing, animation id and frame, spirit, damage, grapple
context, counters), the AI blocks, pin and count-out counts. `match_state.md` and the symbol
files already cover most of it.

## 2. Golden traces from the original

A recorder (`akidbg.py` over the emulator API) that, from a save state and an input script, steps
N frames and writes one line per frame: the frame/tick number, the inputs that frame, and the
state fields above (JSON Lines, or a compact binary with the field list in a header).

- **Inputs** are part of the trace: the four pads as the game read them (`g_pads`), and for CPU
  wrestlers the AI's chosen buttons (`g_ai[slot]`), so a mismatch can be pinned on the AI or on
  the logic separately.
- **Scenarios** come from save states made at interesting moments (a pin, a grapple exchange, a
  tag, outside the ring, each AI mode). F4 marks and F5 states from the window make collecting
  them cheap. A short name and a note per state in a manifest.
- Traces and states hold game data, so like everything ROM-derived they stay out of git
  (`_tools/`), and a script regenerates them.

## 3. Replay in the library and diff

The library side runs the same scenario: load the starting state, feed the recorded inputs, and
export the same fields every frame. A diff tool reports the **first** frame and field that
differ, with a few frames of context, which is what you want for a fix. Later differences are
usually consequences.

- **Starting mid-match** needs an importer: build the library's match state from an emulator RAM
  snapshot, using the same field list. This also checks the field list: anything the library
  needs but the importer can't fill is a gap in the RE notes. Every scenario then starts from
  the original's exact state instead of replaying from the bell.
- **Randomness.** `rng_match` is a plain LCG and will match. `rng_main` is also advanced by the
  idle thread's busy loop whenever the CPU is free (`frame_loop.md`): about 21,600 extra draws per
  match frame in the emulator, a count that follows the CPU's spare time, not the game. The recomp
  patched that loop to a single draw, and `akiemu --no-idle-rng` NOPs the call (`jal rng_main` at
  `0x80000584`), after which `rng_main` advances only through game code: exactly 16 draws per frame
  in a CPU-vs-CPU match (measured 2026-10-31). So the library leaves the idle-loop draws out too,
  and traces recorded with `--no-idle-rng` must match it exactly, with no draws to record. (The
  random sequence then differs from a console's, which can't be reproduced anyway.)
- **Floats.** MIPS single precision is IEEE; the library should match bit for bit if the compiler
  doesn't fuse or reorder (`-ffp-contract=off`, no fast-math, and the same order of operations as
  the original code). Compare positions and angles with the `bits` rule and loosen it only if a
  difference is explained.

## 4. Function-level checks with the recompiled code as oracle

Whole-match traces find that something differs; finding which function needs a finer tool. The
N64Recomp output (`RecompiledFuncs/` from the recomp's build) is the original MIPS code as C
functions over an RDRAM buffer. A test harness could:

1. load an emulator RAM snapshot into an RDRAM buffer;
2. call one recompiled function (`ai_think` for one slot, `wrestler_tick`, a hit test) through its
   `recomp_context`;
3. run the library's port of the same function on the imported state;
4. compare the fields that function writes.

That gives unit tests for each ported function, with real game states as inputs, without the
whole frame loop. Libultra calls inside those functions would need stubs. `RecompiledFuncs/` is
generated from the ROM, so it stays out of git like the rest.

## 5. Animation and pose

Animation is part of "frame-exact". Compare the animation id and frame and the 19 joint transforms
(`file_formats/animations.md`) per wrestler per frame, not pixels: our renderer will differ on
purpose. The joints are in the wrestler record's node tree, so they fit the same trace format.
Screenshots from `/screenshot` stay useful for humans looking at a divergence.

## 6. Sound events

The sound ids triggered each frame (animation frame events, crowd cues) can be traced and compared
the same way, once the sound queue is mapped. The audio itself isn't compared.

## Order of work

1. The field list for the clock, RNG and wrestler basics; a recorder; a one-scenario trace.
2. Name `0x800E678C` (or find the frame loop's own counter) and settle frame alignment.
3. The RAM-snapshot importer, as the library's match state takes shape.
4. The diff tool, wired into CTest as one test per scenario.
5. Record every trace with `akiemu --no-idle-rng`.
6. The recompiled-code oracle harness, when there are ported functions to check one by one.
