# trace: golden traces from the emulator, and the diff

The harness from `_re_docs/state_comparison.md`, built for WWF No Mercy (2026-11-02).

| File | Does |
|------|------|
| `fields.py` | The shared description of match state. Lays out `_re_docs/symbols/<game>/types.h` (N64 alignment, 4-byte pointers) and the regions in `<game>_regions.toml` into a field list with a comparison rule per field (`exact`, `bits` for floats, `unknown` for `_unk*` and unnamed bytes, `ignore`). `header` writes the decompilation's `src/nomercy/nm_types.h` (the same structs with offset checks and byte-swap tables) |
| `nomercy_regions.toml` | The RAM ranges a trace records each frame (wrestlers, the match globals around them, objects and clock, pads, model blocks, animation streams, rng_main) and rule overrides |
| `trace.py` | The trace file: a JSON header (regions, script), the 8 MB RDRAM image at the start, then per frame the match tick, the emulator frame and the regions' bytes |
| `record.py` | Records a trace over the debug API (`tools/recomp_dbg/akidbg.py`) from a script of pad inputs (`scripts/*.json`) |
| `scripts/` | `nm_walk_run.json` (walking, runs, rebounds), `nm_strikes.json` (strikes, a block, a knockdown), `nm_grapples.json` and `nm_lockup_moves.json` (the grapple attempt, the lock-up, front / rear moves, the clash), `nm_pins.json` (a roll-up, a second lock-up, walking round a downed opponent), `nm_pin_move.json` (a strong grapple into a pin, the count, the kick-out), `nm_cover.json` (a slam that puts both down, the lying wrestler holds the stick to stay down, the other covers from standing), `nm_count3.json` (the same with the stick held through the count: the three, the fall, the end phases), `nm_match_end.json` (the same recorded on through the winner's pose and the results copy). A script may start from a save state (`_tools/libretro/states/nm_*.state`); `sx=` / `sy=` give the control stick, the d-pad names are the d-pad |
| (recorded scripts) | akiemu's input recorder (F8 in its window, `tools/n64emu/README.md`) writes a script and its save state from a match you play: the pads the game read each tick, the stick as exact N64 values (`nx=` / `ny=`), `load_pads` for the step after the load, and `ram_crc`, the RDRAM CRC at every record, which `record.py` checks the replay against |
| `dump.py` | Prints chosen wrestler fields of a trace frame by frame (state, step, position, flags), to find the frames and positions a script from a save state has to work with |
| `diff.py` | Compares two traces field by field; prints the first failing frame with every differing field and its last few values; exit 1 on a failure |
| `replay_test.cmake` | The CTest driver: `nm_replay` over a trace, then `diff.py` |

```
py tools/n64emu/akiemu.py --game nomercy --headless --fast --no-idle-rng --port 5671 --paused
py tools/trace/record.py --port 5671 --script tools/trace/scripts/nm_walk_run.json _tools/traces/nm_walk_run.akt
_build/native/nm_replay.exe _tools/traces/nm_walk_run.akt _tools/traces/out.akt --step --rom "_roms/WWF No Mercy (USA) (Rev A).z64"   (or --free)
py tools/trace/diff.py _tools/traces/nm_walk_run.akt _tools/traces/out.akt
ctest --preset native
```

Traces hold game data, so they live in `_tools/traces` (git-ignored); the scripts and save states
regenerate them. A trace records 600 frames in about a minute.

Notes:

- Frame alignment: akiemu counts a No Mercy game frame when libultra's next VI context gets a new
  framebuffer (`__osViNext` at 0x80054BF4, +4). A paused emulator then sits between two match
  frames (checked: the state is the same on both VIs of a frame), and the recorder warns when the
  match tick (the clock at 0x8016B930: (minutes * 60 + seconds) * 30 + frames) does not go up by one.
  The first step after a load is skipped to line the pauses up.
- `--no-idle-rng` patches the idle loop's `jal rng_main` (0x80000570). rng_main is also drawn by the
  camera outside the match logic (a camera cut on a move's first frame, for one), so the replay takes
  its state from the trace every frame and `g_rng_main_state` is `ignore` in the regions file: the
  match logic's own draws are checked through everything they decide, not through the state word.
- A button press counts only on the first frame it goes down (`input_press_kind`), so a script's
  2-frame press right after a step change can be missed by the game; hold it for 3 or more.
- The disassembler annotates only the `lui` half of a float constant, so read both halves (0x41ED999A
  is 29.7f, not 29.625; 0x42903333 is 72.3f). `py tools/sym/sym.py --game nomercy code NAME` gives the
  pseudocode with the names and types applied, which is the faster read; the asm is for float order.
- Record 0 is the state before the first scripted frame; record k is the state after k frames.
- `nm_replay --step --keep-going` writes a frame that reaches unported code as recorded and goes on,
  then prints how many frames each unported spot stopped (and lists them on stderr). With it a long
  recording checks everything that is ported, frame by frame: the 4-CPU tag recording
  (`nm_tag_cpu.akt`) runs 1,085 of 1,500 frames and its four AI blocks match the game's except the
  distances, which follow other wrestlers' unported moves.
- With four wrestlers a record caught the game between `wrestlers_update_all` and `match_clock_tick`
  (the logic runs past the VI the recorder stops at), so the match clock moved 0 or 2 between such records
  and what follows it in `match_tick` (the once-a-second damage history) lagged a frame. **Record with
  `akiemu --tick-hook`**, which ends a frame only between two whole ticks: checked 2026-11-03, the clock
  goes up by one on every record (0 warnings from `nm_4_fighters_all_cpu.state`, 900 frames, and from
  `nm_p1_in_place_for_pin_over_writhing_p2.state`, 300 frames; 409 and 74 without the hook). The recorder
  warns if two ticks ever finish inside one step (not seen in 3,300 ticks).
  `nm_replay` runs a record as two (up to four) ticks when the clock went up by that much, which
  covers those records (one in the 4-CPU recording `nm_tag_cpu.json`).
