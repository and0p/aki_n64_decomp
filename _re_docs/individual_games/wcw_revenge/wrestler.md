# WCW/nWo Revenge: the wrestler struct, flags and header

Part of the WCW/nWo Revenge notes (index: `README.md`). Written as differences from World Tour
(`../wcw_world_tour/`); anything not mentioned is not checked yet, not known to be the same.

## Wrestler struct (first fields mapped)

From `wrestler_update`, `wrestler_set_state` (`0x800A6C48`), `wrestler_set_move`
(`0x800A7C40`), `move_texture_stamp` (`0x800A7D1C`), `move_count_stats` (`0x800A7F6C`)
and the facing update `0x800D4AD0`, compared with World Tour. The layout is **reordered**, not
just shifted:

| Field | Revenge | World Tour |
|-------|---------|------------|
| state | +0x0A | +0x0A |
| state_step | +0x0C | +0x12 |
| (cleared with the step; state_timer?) | +0x0E | +0x14 |
| flags (two words) | +0x20, +0x24 | +0x18, +0x1C |
| ring_zone | +0x3C | +0x32 |
| pos | +0x4C | +0x3C |
| facing | +0x58 | +0x48 |
| partner_idx / other_idx | +0x351 / +0x352 | +0x341 / +0x342 |
| teammate_idx | +0x355 | +0x344 |
| teammate flags copy | +0x358 / +0x35C | +0x348 / +0x34C |
| per-state scratch (`_unk350`) | +0x360 | +0x350 |
| turn_step | +0x368 | +0x338 |
| moveset / move_slot / stat_category | +0x384 / +0x386 / +0x388 | +0x370 / +0x372 / +0x374 |
| move (MoveParams), its flags | +0x38C, +0x3AC | +0x378, +0x398 |

Move data: **279 move slots per wrestler** (6,704-byte blocks at `0x80075478`; World Tour 222),
generic moves at `0x8007BD78` (24-byte records as in World Tour's file 221), move statistics
87 categories × 8 bytes at `0x8007F0E4`. Revenge also keeps one more "current" wrestler pointer
than World Tour (five instead of four).

## Flags (+0x20 hi, +0x24 lo)

Read from the code and RAM (2026-11-01); World Tour's `WrestlerFlagsLo` bits moved:

| Bit (lo, +0x24) | Meaning | World Tour |
|-----------------|---------|------------|
| 0x4 | CPU-controlled: setting it on P1 hands the wrestler to the AI (emulator) | 0x1 |
| 0x10 | waiting to be replaced by the next battle-royal entrant (`matches.md`) | |
| 0x40 / 0x80 / 0x100 | location: ring / apron / floor | 0x4 / 0x8 / 0x10 |
| 0x200 | legal man (tag), everyone in a battle royal | 0x2 (LEGAL) |
| 0x4000 / 0x8000 | won / lost the last fall (`match_record_fall`) | 0x80 / 0x100 |
| 0x80000 | SPECIAL (`wrestler_health_eff` reads health_max) | 0x1000 |
| 0x10000000 | RERUN (run the new state again this frame) | 0x1000000 |
| 0x40000000 | grapple lead | 0x4000000 |

| Bit (hi, +0x20) | Meaning |
|-----------------|---------|
| 0x2000 | pinning, holding or carrying someone (states 0x38, 0x35, 0x66); C-up on such a wrestler breaks it up (0x25) |
| 0x10000 / 0x20000 | holding a weapon / it is one-handed (`weapons.md`) |

## Wrestler header trait bits (2026-11-01)

Every test of the header words `*(w + 0x364) + 0x38` and `+ 0x3C` found in the match code (constant
masks only; bits read through a variable, such as the stance style, are not in this list):

| Word | Bit | Read by | Meaning so far |
|------|-----|---------|----------------|
| +0x3C | 0x1 | `sub_800D00FC`, AI mode 14, `state_47_body` | strike combo from a strong grapple (`strikes.md`) |
| +0x3C | 0x200 | `state_05` | picks animation 4167 instead of 4166 in state 0x05 (a per-wrestler animation variant) |
| +0x3C | 0x400 | `state_0A_whip_attacker_body` | a whip variant |
| +0x3C | 0x800, 0x1800 | `ai_vs_floor` | AI choices outside the ring (probably dive styles) |
| +0x3C | 0x2000, 0x8000, 0x10000 | `state_55_ropes_b_body` | ring-entry styles (World Tour used 0x400–0x2000) |
| +0x3C | 0x4000, 0xE000 | `state_60_ropes_a_body` | ring-exit styles |
| +0x3C | 0x20000 | `sub_800DF924`, `sub_800AB43C` | a turnbuckle / springboard ability (run and climb inputs) |
| +0x3C | 0x40000 | `state_64_body` | its own animation (3832) and an extra step in state 0x64 instead of the stance animation |
| +0x3C | 0x80000, 0x100000, 0x2000000 | `input_apron_moves` | apron moves 0x65, 0x67, 0x69 |
| +0x3C | 0x200000 | `run_inputs` | running attack on the apron 0x68 |
| +0x3C | 0x1000000 | `run_inputs` | a running move in the ring, gated by `run_flags` (+0x28) bit 1 and not bit 8; leads to `sub_800B5CA0` (not identified) |
| +0x3C | 0x400000 | `input_climb_turnbuckle` | move on a downed opponent 0x66 |
| +0x3C | 0x800000 | `get_up_anim` | get-up style |
| +0x3C | 0x4000000, 0x8000000 | `state_2F_cover_attacker_body` | cover (pin) styles |
| +0x3C | 0x80000000 | `state_63_dive_outside_body` | dive-to-the-floor style |
| +0x38 | 0x4 | `grapple_attempt_resolve` | **test of strength**: when both wrestlers have it, both have health >= 151 and grapple at the same moment (opponent in 0x44 step 4, both in the ring) |
| +0x38 | 0x40, 0xC0 | `weapon_give` | the wrestler's own weapon type |
| +0x38 | 0x1000 | AI mode 9 (meet a runner) | AI preference |
| +0x38 | 0x4000 | AI mode 14 (grapple) | AI preference |

Other header bytes read by the match code, compared with World Tour's (whose bytes are spread
over `moves.md`, `frame_loop.md`, `movement.md`, `strikes.md` and `outside_ring.md`). From +0x11
on, Revenge has one more climb byte, so the bytes after it sit one higher:

| Revenge | Read by | Meaning | World Tour |
|---------|---------|---------|------------|
| +0x00 | not seen by constant offset | blood resistance in World Tour | +0x00 |
| +0x01 / +0x02 | `health_regen` | health regained every 60 / 90 frames | +0x01 / +0x02 |
| +0x03 | `move_apply_hit` | extra spirit on hits | +0x03 |
| +0x04 | `walk_speed` | walk class 1–3 | +0x04 |
| +0x05–0x0E | `move_apply_hit` (through a pointer) | attack per body part / defence per region, presumably as in World Tour | +0x05–0x0E |
| +0x10 | `chance_roll` | crowd-weapon chance | +0x10 |
| +0x11–0x17 | `state_09_turnbuckle_body` | climb kinds per turnbuckle move (7; World Tour 6) | +0x11–0x16 |
| +0x18 | `chance_roll`, AI modes | **weight class** (lift chance by difference / 3) | +0x17 |
| +0x1B | `dive_aim` | dive reach | +0x1A |
| +0x1C | `chance_roll` kind 6 | TKO-save table index | |
| +0x1E | `sub_800C778C` | strike animation style (from 4169) | |
| +0x20 | `reversal_slot` | which 400-byte reversal table (`0x80064AE0` + 400 x n); World Tour had a per-wrestler file instead | |
| +0x24 / +0x26 | `sub_800AC208` (AI) | two move slots + 1 that the CPU picks with 50 % each (one when its health is below 65): favourite moves | |
| +0x38 / +0x3C | many | trait words (table above) | +0x38? / +0x3C? |

Bytes +0x05–0x0F, +0x19–0x1A, +0x1D, +0x1F, +0x21–0x23, +0x28–0x37 are not read by constant
offset; World Tour's block-counter bytes (+0x18 / +0x19 there) have not been located yet.
