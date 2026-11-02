# WCW/nWo Revenge: weapons

Part of the WCW/nWo Revenge notes (index: `README.md`). Written as differences from World Tour
(`../wcw_world_tour/`); anything not mentioned is not checked yet, not known to be the same.

World Tour only had the crowd handing a weapon over (`../wcw_world_tour/outside_ring.md`).
Revenge has 11 weapon types, wrestlers who bring their own, running weapon attacks, fetching a
weapon and stealing one on a counter.

**Weapons:** hi 0x10000 means "holding a weapon" (set by `weapon_give`, `0x800E1920`, which picks
a weapon object and a random type 3–10; called by the crowd-weapon state 0x7D, the run-in 0x8F
and 0x8D). With a weapon, C-up next to a downed opponent gives 0x24 instead of 0x55.

Weapon types and objects, confirmed against a set of GameShark "have weapon" codes (Andrew,
2026-11-01) and `weapon_give`:

- **Weapon objects** are 48-byte records from `0x800F98E0` (byte +0 and +1 the type, +2 a state).
  A wrestler's object index is `0x800F99A0[slot]` (a byte per slot).
- **Types:** 0 mallet, 1 blue microphone, 2 black microphone, 3 trophy, 4 black baseball bat,
  5 spotted baseball bat, 6 steel chair, 7 table piece, 8 trash can, 9 suitcase, 10 stop sign.
- **Holding flags** (hi word, byte `+0x21` of the wrestler): 0x10000 = holding a weapon, plus
  0x20000 for the **one-handed types 0–5**; the codes write 3 (both bits) for those and 1 for the
  two-handed 6–10. `weapon_give` sets 0x20000 for types below 6, and the running weapon attack
  0x08 picks its swing by it (animation 4137 / generic move 42 one-handed, 4141 / 41 two-handed).
- **Which type:** crowd weapons are random 3–10 (trophy to stop sign). Roster `0x703` (Dake Ken)
  always gets the mallet (0); with header +0x38 bits 0xC0 a wrestler gets their own weapon
  (type 4 or 6, from `weapon_give`'s flag argument 4).

`0x8007F07C` (u16) is the unlock mask: 0xFFFF unlocks everyone (moves.md).

## States and functions

- `weapon_give` (`0x800E1920`): picks a free weapon object and a type; used by the crowd-weapon
  state 0x7D, the run-in 0x8F and 0x8D.
- `weapon_take` (`0x800E2090`): moves the object and hi 0x30000 from one wrestler to another
  (the steal on a counter, from state 0x5A).
- `weapon_flags_clear` (`0x800E212C`): clears hi 0x30000 (guess: dropping it).
- States 0x08 (running weapon attack) and 0x8D (fetching a weapon); see `states.md`.
