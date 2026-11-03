# WWF No Mercy: the decompilation of the match overlay

A faithful, annotated translation of the match code (overlay ovl_e, USA Rev 1) into C-style C++17:
plain functions and structs, no classes, exceptions, RTTI or threads, float operations in the
original's order (single precision where the game uses it, double where it converts). It is the
oracle for the later port (a C++ or Rust ECS); the port is then a refactor checked against this.

- **One function per original function**, under the original's name from `_re_docs/symbols/nomercy/`,
  tagged `// N64 0x800E5890`. `py tools/decomp/port_status.py` writes each tagged function's file
  into the symbol's `port` field (and a "Port: partial" note while it still has unported paths).
- **Paths not translated yet** call `NM_UNPORTED("what")`. A replay that reaches one stops and names
  it, so the work goes in the order the traces need.
- **State.** The structs the code works on (`Wrestler`, `InputSlot`, `Pad`, ...) are generated from
  `types.h` into `nm_types.h` with the N64 offsets (checked by `static_assert`) and host byte order.
  The globals the code uses all the time are host variables (`nm_state.h`); everything else (the
  overlay's tables, the collision walls, the move sets, the attribute records, file 703's animation
  records) stays in an N64-order image of RDRAM read with `mem_*()`. `state_import()` /
  `state_export()` move the host variables to and from the image, so a replay starts from a RAM
  snapshot and writes the same regions the emulator trace has. Fields the code needs that had no
  name in `types.h` got one there (so the RE notes grow with the decompilation).
- **The host** (`nm_host.h`) supplies the presentation the oracle ignores (HUD, sound, rope shapes,
  the special frame events) and the ROM: the animation keyframes are streamed from it
  (`rom_read`), as the original DMAs them. The replay reads the ROM given with `--rom`.

Files: `nm_frame.cpp` (match_tick's play phase, run-ins, the pin count, the clocks), `nm_wrestler.cpp`
(the update passes, wrestler_tick, wrestler_update, state setters and the per-frame helpers),
`nm_stand.cpp` (state 0x01 and its input checks), `nm_walk.cpp` (walking, speeds, walk
animations), `nm_bounds.cpp` (arena walls, body push-apart), `nm_anim.cpp` (animation player,
posing calls, head look), `nm_run.cpp` (states 0x03 / 0x04 / 0x30 and the run's helpers),
`nm_replay_rec.cpp` (the instant-replay recorder), `nm_model.cpp` (the main segment's model posing:
the animation stream, keyframe decode and interpolation, the blend, skin matrices and render tree,
root anchoring, frame events), `nm_strike.cpp` (strikes: the hit test, damage, spirit, the chance
rolls, timed presses, the reactions 0x22 / 0x23 / 0x2D / 0x33 / 0x35 / 0x4C), `nm_grapple.cpp` (the
grapple attempt 0x50-0x52, the lock-up, the pair records, the move states 0x55 / 0x56, move chains
and follow-ups), `nm_down.cpp` (lying 0x31, getting up 0x4F, the counts, mashing and escape times,
the pin states 0x44 / 0x45 and 0x4A / 0x4B, 0x4E, 0x6B), `nm_hold.cpp` (the holds 0x48 / 0x49, the crawl for
the ropes, giving up, `fall_count_add`), `nm_tag.cpp` (tag rules, the tag 0x93 / 0x94, the apron partner
0x95, the illegal man's walk back 0x92), `nm_ai.cpp` (the CPU's frame: `ai_update`, perception, situations,
the mode bookkeeping, every decider and helper, and the 26 modes a 4-CPU recording uses), `nm_rng.cpp`,
`nm_math.cpp`, `nm_todo.cpp` (objects and the CPU, only as far as an empty arena needs).

## Status (2026-11-02)

Milestone 1 reached: the 600-frame two-player walk / run trace (`tools/trace/scripts/nm_walk_run.json`:
standing, walking in all directions, runs, rope rebounds, a runner turning off the other wrestler)
replays exactly, in step mode (each frame from the recorded one) and free-running from frame 0, on
every recorded region (`ctest --preset native`), including the posed model blocks and the animation
streams now that `model_animate` is decompiled (nothing is taken from the trace per frame any more
but the pads and rng_main).

Milestones 2-4 (the same day): strikes (`nm_strikes.json`: punches, kicks, a strong strike, hits,
a block, a knockdown), grapples (`nm_grapples.json`, `nm_lockup_moves.json`: the attempt, the lock-up
walk and timer, front and rear moves, the clash, letting go), the down / get-up states and the pin from
a move (`nm_pins.json`: a roll-up pin, a second lock-up, walking round a downed opponent;
`nm_pin_move.json`: the strong grapple that rolls into a pin, the count, the kick-out) all replay
exactly, step and free, as does the cover from standing (`nm_cover.json`: a slam that puts both
down, the lying wrestler holding the stick to stay down, the walk beside it on its approach curve,
the cover and the count; milestone 5), and a finished fall (`nm_count3.json`: the same cover with
the lying wrestler holding the stick through the count: the three, match_end's results bookkeeping,
match_tick's end phases; milestone 6) and the whole match end (`nm_match_end.json`: the winner's
post-match pose in the taunt state 0x1B, the end timer to 271, the results copy; milestone 7). 16
CTest cases. 295 functions are tagged, 244 complete; the rest stop a replay at an `NM_UNPORTED`
path (the other lock-up kinds, double teams, the apron and weapon inputs, the cage, objects, the tag /
elimination / multi-fall results, the end replay). `py tools/decomp/port_status.py --protos` writes each tagged function's C
prototype into the symbol's `proto` and marks a complete one `confirmed`.

2026-11-03, a change of approach (understanding first): new systems are written as readable,
annotated C from the code and checked against the emulator with recorded traces and `dump.py`, but
they do not have to replay; parity comes back when the engine is built. The 16 CTest cases still pass.
In this form so far: the holds (0x48 / 0x49, `nm_hold.cpp`) and every escape_time op, checked in RAM
(`nm_ground_hold.json`, `nm_submission.json`, `nm_corner_lockup.json`; not replayed, since the ground
grapple 0x46 / 0x47 in front of them is not ported); every lock-up kind in `lockup_input`; tag matches
(`nm_tag.cpp`); the count-out and the time limit (`countout_tick`, `match_clock_tick`, `match_result`);
the CPU (`nm_ai.cpp`: its situation classes checked against a 4-CPU recording; all deciders and helpers
and 26 modes in C, the other modes still stop a replay by number).

A singles match from the lock-up to the results now replays end to end. Next, in the order a
port needs them: holds and submissions, the remaining lock-up kinds, the other fall causes
(count-outs, DQ, the time limit: match_result), then the CPU (for which a CPU-vs-CPU trace with the
AI's outputs as an oracle is the plan).
