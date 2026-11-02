# WCW/nWo Revenge: strikes, combos and counters

Part of the WCW/nWo Revenge notes (index: `README.md`). Written as differences from World Tour
(`../wcw_world_tour/`); anything not mentioned is not checked yet, not known to be the same.

The strike basics are World Tour's (`../wcw_world_tour/strikes.md`); the changes found so far:

- **R counters an attack** (states 0x34 / 0x35): R next to an opponent whose byte +0x37 bit 0
  is set; otherwise R next to a lying opponent picks them up (0x32 / 0x33), else it is the
  block 0x40.
- **Strike counters come in three kinds**, each with its own receiver state: 0x58 / 0x59,
  0x5A / 0x5B and 0x5D / 0x5E, chosen from the caught-strike record by `strike_counter_start`
  (`0x800C41F0`). World Tour had 0x51 / 0x52 only. Stealing a weapon goes through 0x5A.
- **State 0x27**, a strike variant when the strike connects under `strike_special_hit_check`
  (seen in the emulator, Nash in a battle royal).

## Strike combos ("multi-hit combos", 2026-11-01)

The same mechanic as World Tour's state 0x44 (`ai.md`, `grapples.md`), now states 0x4B / 0x4C
(`state_4B_strike_combo_body`), with more to it:

- **Who:** wrestlers with header trait bit 0 (`*(w + 0x364) + 0x3C & 1`): Goldberg, Glacier,
  Yugi Nagata, Ultimo Dragon, AKI Man, Shogun, Kim Chee, Han Zo Mon and the hidden Kanyon. The
  guide lists a "Combo Meter" move list for the same eight (Kanyon is not in it).
- **Start:** B from a strong front grapple (grapple context 0x42 == STRONG), `sub_800D00FC`.
- **Length:** 6 hits, or **10** with SPECIAL ready (World Tour 6 / 9). The combo's own animations
  come from slots 236–238 (World Tour 187–189).
- **Each hit** is picked by `strike_combo_finish` (`0x800AD2C4`) from the strong-grapple slots:
  B, B+Up, B+Down, **B+Left, B+Right** → 33, 38, 43, 48, 53 (left and right are new; World Tour
  had three), and the last hit uses that slot + 2. **A** (+Up / Down) ends the combo with the
  strong-grapple move 18 / 23 / 28; A+B → slot 60; SPECIAL ready → the special 58.
- Timing: the next input is read during the hit (`strike_combo_input`, as in World Tour); a
  per-slot meter (`strike_combo_meter`) is set from the hits left.
