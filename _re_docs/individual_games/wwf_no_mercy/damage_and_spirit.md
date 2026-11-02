# No Mercy: health, damage, the attitude meter and the odds

Read from the code (2026-11-02; `apply_damage`, `walk_speed`, `ai_perceive` and the chance tables
straight from the disassembly). Names in `symbols/nomercy/`; World Tour's equivalent is
`../wcw_world_tour/body_parts.md`.

## Health

- `health` (+0xAA) 0 to `health_max` (+0xAC), 255 at the start; `wrestler_add_health` caps and floors it.
- Every hit takes a quarter of its damage off health_max too (not below 64; skipped for moves with flags
  0x8000000), so a beaten wrestler cannot regain as much.
- `health_regen`: every 90 frames (60 with +0xF3 bit 2) adds attrs +3 / +4. (`run_in_fresh_clear`, once
  read as a regeneration gate, only clears a run-in's fresh flag flags_f1 0x02 when its health_max drops
  below 249; code 2026-11-02.)
- Thresholds in the code: 0 rolls a run-in for that side (below); below 49 the receiver gains 3 more
  spirit per exchange; below 50 the referee's count plays a sound; a fall raises health to at least 60
  (`fall_count_add`); below 100 weaker hit reactions (+14: the slouched fall); the CPU checks 64 and 128.
- **health_eff**: health, or health_max in finisher mode.

## Body regions

Five regions (`region[5]` at +0x2C4, floats). `wrestler_region_condition` returns the region's
condition, or 50 in finisher mode. Each hit lowers every region by the move's per-region damage
(hit_move +0x12 + i); the weakest region index is kept in +0xE7. The legs' condition (region 3)
slows walking (`movement.md`).

## Damage (`apply_damage`, 0x80112074, confirmed)

For the move in def->hit_move (copied from the attacker by `hit_move_copy`):

- D = hit_move +0x06 (or the alternate +0x1B when def +0x31E equals the move's non-zero s16 +0x1C; then
  flags8a 0x4000), r = +0x0B the region, k = +0x0C the hit kind.
- **Damage = max(0, trunc((condition(att, r) + 50) × D × 0.01))
  + max(0, (att.attrs[0x0A + r] − def.attrs[0x0F + k]) × D / 10)
  + max(0, (att.spirit − def.spirit) / 20)** (the last term only when D ≠ 0);
  **× 1.2 when the attacker is in finisher mode**.
- Then health − damage (mode 1 in 0x45 / 0x49 / 0x4B, 2 with move flags 0x8000000), the damage
  history, health_max − damage / 4 (at least 1 when D ≠ 0, not below 64), the region drops (bigger in
  the ground hold 0x49 through the wrestlers' attrs +0x16 classes, table 0x801461C0), and
  `spirit_exchange`.

attrs +0x0A-+0x0E are strength by body region, +0x0F-+0x13 defence by hit kind.

## The attitude meter (spirit, `spirit_update`)

- `spirit` (+0xAE) 0-100, a mode in flags_ee & 7 and a timer +0xB0.
- **Drift** (op 1, from standing): every 120 frames toward 50: +3 below 31, +2 below 50, −1 above 50;
  instead **−8 (to at least 1) while the wrestler is out of the ring and its opponent is in it**.
- **SPECIAL**: reaching 100 (`wrestler_add_spirit`) gives flags_ee 0x02 for **600 frames**, then 70 (99
  instead of SPECIAL when flags_e9 0x01).
- **Finisher mode**: a taunt while SPECIAL (op 5, also from 0x0A, 0x1C and run-ins) gives flags_ee 0x04
  for **600 frames**, then 60: damage × 1.2, health_eff reads health_max, regions read 50, most
  escape times are 0, the lock-up time limit drops to 90 and reversals need it.
- **Empty**: reaching 0 gives flags_ee 0x01 for **300 frames** (spirit 0), then 40; the TKO check
  forces it (op 4).
- op 3 takes 60 frames off a mode; op 7 (after a fall) sets 50. A mode blocks `wrestler_add_spirit`
  (SPECIAL does not on the cage wall).
- **Exchange** (`spirit_exchange`): after a hit or move both sides gain the move's +0x07 (attacker) and
  +0x08 (defender), 3 more for a receiver below 49 health (not in 0x49), plus per-wrestler bonuses from
  0x8014659C (by attrs +5) with +0xF3 bit 2.
- The HUD shows 99 at 100 outside finisher mode (`hud_spirit_value`; `finisher_full_spirit_roll` makes the
  same adjustment, so it is true only in finisher mode with full spirit).

## The odds: chance_roll (0x801128C8)

`chance_roll(kind, ...)` turns a stat into a per-mille chance and calls `chance_check` (capped 1000):

| Kind | Used by | Chance |
|------|---------|--------|
| 0 | ground grapple defence | spirit 100 / 81 / 61 / 31 / 11 / below: 500 / 333 / 250 / 125 / 62 / 31 |
| 1 | the lock-up reversal (`grapple_defender_input`) | spirit table 0x8014620E (as kind 0), ×2 for the bigger wrestler, ×4 / ×2 against a strong grapple at health_eff 192+ / 128+ |
| 2 | | health_eff ≥ 196: 3.9; 128: 7.8; 64: 15.6; 32: 31.25; else 62.5 |
| 3 | whip reversals, 0x04, 0x15 | spirit 100 / 81 / 61 / 31 / 11 / below: 1000 / 500 / 333 / 250 / 200 / 125 |
| 4 | bleeding (`move_aftermath_check`) | 1000 / (15 − the defender's attrs +2 − the move's bleed_class (+0x09) − the attacker's spirit / 30); w is the attacker, other the defender (callers checked 2026-11-02) |
| 5 | the TKO stagger (`move_aftermath_check`) | only when the **defender's** health_eff is below 25 (the second record is the one hit; the callers pass (attacker, defender), checked 2026-11-02): by the move's tko_class (+0x0A), 0x80146270 = 16 / 133 / 166 / 222 / 333 / 666 |
| 6 | the TKO recovery, per press (0x2E) | 1000 / (0x80146288[attrs +7] + 6 × (tries − 1)); the table is 17, 17, 12, 8, so the first press is 58 / 58 / 83 / 125 and each later try is harder |
| 7 | staying down (0x31) | by health and (100 − spirit) / 40 |
| 8 | move reversals (`reversal_check`) | 500 − 10 × max(0, other.spirit − w.spirit) |
| 9 | crowd weapon | by attrs +6 |
| 10 | lifting | 1000 in finisher mode, else by the size difference (attrs +0x15 / 3) and the other's health (table 0x8014628C) |
| 11 | resisting a pick-up | spirit 100 / 80 / 60 / 40 / below: 600 / 400 / 300 / 200 / 100 |
| 12 | `timed_press_check` (below) | by the escape counter (+0xB4; 0x80146304): ≥ 120: 100, ≥ 60: 125, ≥ 45: 166, ≥ 30: 222, ≥ 15: 333, else 666 |
| 13 / 14 / 15 | `timed_press_check` | 700 / 500 / 600 |
| 16-18 | knockdown falls (`strike_land`) | 100 × `fall_chance` (+0xE4) |
| 19 | knocked off a ladder (0xB2) | 10 × (0x8014631C[ladder_rung] + other.spirit / 5 − own spirit / 5); by rung 0, 10, 20, 30, 40, 30 |

## Attribute record (attrs, +0x2E4)

64 bytes per slot at 0x8009FE78 + 64 × slot (`match_load` points +0x2E4 at it). **`wrestler_data_unpack`
(0x800063BC) fills it from a bit-packed stream** (`bits_read`, most significant bit first), so the field
order and widths below are exact (read 2026-11-02). The packed source is the first 32 bytes of the slot's
276-byte wrestler record at 0x8009809C + 276 × k (then the names, "THE ROCK" / "ROCK", and more), filled
by the menus. **In the ROM each wrestler's packed attributes are a 32-byte file** (505, 507, 509 ... 677:
the 66 32-byte files each followed by a 184-byte file; The Rock 505, Steve Austin 507, Undertaker 509,
Triple H 521; the roster's attire descriptor +12 names the file, +10 the move-set file,
`animation_data.md`). Checked in RAM 2026-11-02 in a four-wrestler match: the four buffers equal those files,
and unpacking them gives the records at 0x8009FE78 exactly, except +0x15 (overwritten at match load by
`wrestler_set_size`: size value / 42, e.g. 7 → 4) and +0x38 / +0x3C (the has-move bits, below).
`nomercy_models.py attrs` decodes all 66.

| Offset | Bits | Field (users in the match code) |
|--------|------|---------------------------------|
| 0x00 | 3 | speed class 1-3 (`walk_speed`, so walk and run speed) |
| 0x01 | 4 | follow-up variant set (`move_anim_offset`, files 498-503) |
| 0x02 | 3 | cut / bleed class: how easily this wrestler bleeds when hit (`chance_roll` kind 4 reads the defender's; 0 never bleeds) |
| 0x03, 0x04 | 4, 4 | health regeneration per tick (`health_regen`) |
| 0x05 | 3 | spirit bonus index (`spirit_exchange`) |
| 0x06 | 3 | weapon trait (crowd weapons, the floor AI's area moves) |
| 0x07 | 3 | TKO recovery class (`chance_roll` kind 6) |
| 0x08 | 5 | forced crowd weapon type |
| 0x09 | 6 | crowd weapon (`weapon_pick_type`) |
| 0x0A-0x0E | 4 each | strength by body region (`apply_damage`) |
| 0x0F-0x13 | 4 each | defence by hit kind (`apply_damage`) |
| 0x14 | 3 | read by `dive_aim` and `move_metrics_measure` (probably a height class) |
| 0x15 | 5 | size: the packed value is replaced at match load by `wrestler_set_size` (the size value / 42); / 3 compares wrestlers (lifting, the lock-up wait, the AI run chance) |
| 0x16 | 3 | class (hold damage, table 0x801461C0) |
| 0x17 | 3 | not read in the match |
| 0x18 | 4 | tag-partner behaviour (AI mode 15) |
| 0x19, 0x1A | 3, 3 | AI: a downed opponent, healthy / hurt (`ai.md`) |
| 0x1C | 9 (u16) | first finisher move number (`wrestler_start_move`) |
| 0x1E, 0x1F | 4, 4 | AI: which move list by the opponent's health |
| 0x20 | 9 (u16) | second finisher move number |
| 0x22-0x27 | 4, 3, 3, 4, 4, 3 | AI traits: grapple keenness, defence, close range, aggression, outside fighting, specials (`ai.md`) |
| 0x28, 0x2A, 0x2C | 12 each (u16) | not read in the match (probably menu data) |
| 0x2E | 12 (u16) | **probably the regular tag partner**: in tag matches a slot whose partner has this roster id (and 0x33 matching) is marked a real team (flags_e9 0x01) |
| 0x30-0x32 | 3 each | not read in the match |
| 0x33 | 3 | the tag-team check above |
| 0x34 | 32 | trait bits: 0x1000 can reverse, 0x2000 whip reversal, 0x3F8 animation style (`anim_style_variant`), 0x700000 partner behaviour |
| 0x38 | 1 (u32) | **bits for moves the wrestler has**, set by `moveset_unpack` (below) |
| 0x3C | 1 (u16) | the same for a second list |
| 0x3E | 4 | not read in the match |

**+0x38 and +0x3C are not stored traits.** `moveset_unpack` ORs in one bit for each listed move slot the
wrestler's set fills (non-zero `g_move_anims` entry): +0x3C from 0x8014DE2C (moves 70, 81, 94, 106, 107,
128, 138, 147, 151, 257, 262, 275, 155, 156, 157, 158 → bits 0x0001 to 0x8000 in that order) and +0x38 from 0x8014DEB4
(104 → 0x800000, 105 → 0x1000000, 256 → 0x20, 261 → 0x40, 263-274 → 0x80 to 0x40000, 278-281 →
0x80000 to 0x400000), plus 0x01 / 0x04 / 0x08 from the flags of moves 235, 265 and 266. So the
"move kind" bits that gate inputs and the CPU's move lists (`ai.md`) just mean **the wrestler has that
move**: an apron dive, a turnbuckle aerial, a rope move and so on.

**`move_anim_offset`** (0x800EC7D8, read 2026-11-02; the name is older): used by `move_chain_check`,
`move_follow_up` and `grapple_reversal_start`, it returns a base move number **+ 1-4**: a 2-bit value for
the current move's animation code from a table at 0x80090BA0 (ROM files 498-503, one per set; `animation_data.md`), the
row picked by attrs +0x01. So which follow-up, chain or reversal comes out of a move depends on the
wrestler's set as well as the move; the converter needs this table and each wrestler's set.

## Escaping while staggered: the timed press (`timed_press_check`)

**`timed_press_check`** (0x80132FFC, read 2026-11-02, disassembly checked later that day; it was called
`mash_escape`) passes kinds **12-15** in a register. It is a **timed press, not a mash**. Op 1 runs every
frame from `wrestler_update` (not only while staggered): a new press of A / R / L records that button
(bits 0x04 / 0x08 / 0x10 in 0x80159458 + 2 × slot) and resets the count in 0x80159450 + 2 × slot;
every frame the same button stays held the count goes up (releasing clears the record); the state's
class bit is set alongside (0x50 step 1 → 0x20, 0x50 → 0x40, 0x53 → 0x80, 0x51 → 0x100; so the classes
are the grapple states, not the staggered ones). When something then lands, the caller asks for a button
(op 2 R, op 3 L, op 4 A: `strike_land`, `dive_counter`, `grapple_attempt_resolve`, 0x32 / 0x33 / 0x35):
the check passes only when that button is the recorded one and has been **held for fewer than 3
frames**, i.e. it was pressed in the 3 frames before the hit, and the wrestler is not flags_e9 0x01;
then it rolls `chance_roll` with kind 12 + the class in the grapple states (12 by the escape counter,
13 → 700, 14 → 500, 15 → 600) or the caller's kind elsewhere, and on success the defender escapes
(`strike_counter_check` / the grapple reversal). Op 0 clears the checked bit. Only **kind 2 is never
used**.

## Open

