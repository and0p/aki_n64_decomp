# WCW/nWo Revenge: action states

Part of the WCW/nWo Revenge notes (index: `README.md`). Written as differences from World Tour
(`../wcw_world_tour/`); anything not mentioned is not checked yet, not known to be the same.

Revenge has **146 action states** (`g_state_handlers`, `0x800E2FF0`) against World Tour's 120.
It kept World Tour's states in order and inserted new ones. The alignment below gives the
Revenge number of every World Tour state; the new ones were then traced from the code that
sets them and partly checked in the emulator.

## Alignment with World Tour

Matching each World Tour state handler with Revenge's by shared animations (through the 661
identical files) and constants, with the order kept, gives a consistent alignment: Revenge
kept World Tour's states in order and **inserted new ones**. Revenge number = World Tour number
plus:

| World Tour states | Revenge offset |
|-------------------|----------------|
| 0x01–0x07 | +0 |
| 0x08–0x23 (0x12, 0x19 unmatched) | +1 |
| 0x24–0x2C | +4 |
| 0x2D–0x2E | +5 |
| 0x31–0x51 (0x2F +7, 0x30 unmatched) | +7 |
| 0x52 | +9 |
| 0x53–0x58 | +12 |
| 0x59, 0x5D–0x6C, 0x6F–0x73 | +14 (0x6D +15) |
| 0x74–0x76 | +18 |

Unmatched or new Revenge states: 0x00, 0x08, 0x13, 0x1A, 0x25–0x27, 0x31, 0x34, 0x35, 0x37,
0x59, 0x5A, 0x5C–0x5E, 0x65, 0x66, 0x68–0x6A, 0x7B, 0x82–0x85, 0x89–0x91. Where several matches
were weak the exact boundary of an insertion may be off by one; 25 handlers with strong matches
are named in `symbols/revenge/states.toml` (e.g. turnbuckle 0x0A, whips 0x0B/0x0C, corner slump
0x14, hit reaction 0x28, recover 0x2B, dazed 0x2D, pick-up 0x32, carries 0x74/0x75, crowd
weapon 0x7D).

## New states traced from their setters (2026-11-01)

Every call to the five state setters was searched, with the values of variables passed to them
(`who.py`-style scan of the pseudocode, then reading each site). Flags in Revenge sit at +0x20
(hi) and +0x24 (lo); location bits are lo 0x40 ring, 0x80 apron, 0x100 floor (World Tour's
4 / 8 / 0x10 shifted by 4). Several new moves are gated by **wrestler header traits** (the
header word at `*(w + 0x364) + 0x3C`), like World Tour's ring-entry traits.

| State | Entered from | Condition | Reading |
|-------|--------------|-----------|---------|
| 0x1A | `state_19_trip_runner` | the opponent of a successful trip | tripped runner (receiver of 0x19) |
| 0x25 | `input_cup_opponent` | C-up, in the ring or on the floor, opponent with hi 0x2000 (not 0x4000000) within reach and in front (angle < 512) | **breaking up a pin or hold**: the target's hi 0x2000 is set by the pin, hold and carry attacker states (0x38, 0x35, 0x66), so this is C-up on a wrestler who is pinning or holding someone (move slots 185 / 186). Tagging is 0x7F–0x81 (`tag_try`) |
| 0x27 (seen: Nash in the 8-man battle royal, AI strike mode 3) | `state_1D_strike_body` | the strike connects while `sub_800C7870` holds (both in the ring, opponent byte +0x37 bit 0, distance under a constant) | a strike variant; the opponent goes to 0x28 (hit reaction) |
| 0x31 | `sub_800E0590` (from `state_01_stand`) | pair 0x30 / 0x31 next to a downed opponent | **flip the opponent over** (the new C-Left), 0x31 the one flipped |
| 0x34 / 0x35 | `input_strike` | R pressed in the ring next to an opponent with byte +0x37 bit 0 (closer than `0x800E76A8`) | **R counter** of an attack (the guide's "R: counter moves"); 0x34 counters, 0x35 is countered. R next to a lying opponent instead starts the pick-up 0x32 / 0x33, otherwise R is the block 0x40 |
| 0x37 | `ground_grapple_try` | pair 0x36 / 0x37 on a downed opponent in reach | receiver of the ground grapple 0x36 |
| 0x58 / 0x59, 0x5A / 0x5B, 0x5D / 0x5E | `sub_800C41F0` (from the strike landing `0x800C3A78`) | the caught-strike record (+0x3A bits, World Tour's `state51_flags`): slot bits 0x7E00 → 0x5A / 0x5B; bit 1 (far strike) → 0x5D / 0x5E; otherwise 0x58 / 0x59 | **strike counters, now three kinds**, each with its own receiver state (World Tour had 0x51 / 0x52 only) |
| 0x5C | `sub_800DB72C`, `sub_800C41F0` | lo 0x8000000 and `sub_800D7B54(w, 0, 0, 2)`, with sound 300 | not identified (animation 3773 = World Tour 4537) |
| 0x65, 0x67, 0x69 | `input_apron_moves` | on the apron: d-pad toward a side with trait 0x80000 → 0x65; opponent on the floor with trait 0x100000 → 0x67; trait 0x2000000 and allowed to act → 0x69 | **trait-gated apron moves** |
| 0x66 | `input_climb_turnbuckle` (`0x800DF5C4`, from `state_01_stand`) | opponent lying in the ring, trait 0x400000 | a trait-gated climb / springboard onto a downed opponent (it sits in the turnbuckle-climb input) |
| 0x68 | `run_inputs` | running on the apron (lo 0x80), A, trait 0x200000 | **trait-gated apron running attack** |
| 0x82, 0x85, 0x90 | `state_01_stand` (0x82, 0x90), `state_28_down` (0x85 instead of getting up) | lo 0x8000 in the ring | **lo 0x8000 = lost a fall** (set by `match_record_fall`): a standing loser goes to 0x82 / 0x90, a downed one stays down in 0x85 (getting up through 0x8E). Not the analog "stay down" trick as first guessed |
| 0x83 / 0x84 | `input_l` | L next to a dazed opponent | turning a dazed opponent around (first pass, below) |
| 0x89 | `sub_800DF214` (from stand / taunt) | C-down with a d-pad direction while standing, facing a wrestler (slot stored at +0x354) within 512 | probably the **leapfrog / drop to the mat** over a runner (animation 3706, next to the run animations); the guide notes these got easier in Revenge |
| 0x8A | `run_collide` | the run meets something (`sub_800B678C` = 1) | a running collision (animation 3707) |
| 0x8B / 0x8C | `sub_800CD3F8` (grapple start, from `state_3D_grapple_attempt_body` via `sub_800CCE50`) | on the apron (lo 0x80, not 1 / 8) when none of grapple context 0x40384 applies | **grapple between the apron and the ring**, a new pair (the ring version is 0x45 / 0x46) |
| 0x8D | `run_step` | running toward one end, Z + up, x in a set range (`0x8007FC10` 0 or 2) | **run off to grab a weapon**: 30 frames of the run animation away from the ring; an A/B/L/R press wins `chance_roll` kind 3, and then `weapon_give` (`0x800E1920`) hands a weapon and the wrestler runs back (state 0x03) |
| 0x8F | `sub_800C0004` / `sub_800C01A8` from `match_tick` | for wrestlers standing (1) or down (0x2C) with lo 0x10, with `chance_check` | **a new wrestler coming in**: the run-in (match type 0x100 with interference) and the next battle-royal entrant (match type 0x400) both enter through 0x8F; in the battle royal the eliminated wrestler's slot switches to the entrant (+0x48 -> roster +0x02) during it (`matches.md`) |
| 0x13 | `run_step` | a 4-point probe ahead hits something | a running collision with the opponent (generic move 10) |
| 0x08 | `run_inputs` | running + B while holding a weapon (hi 0x10000) | **running weapon attack** (generic move 41 / 42 and animation 4141 / 4137 by hi 0x20000); without a weapon it is the plain running attack 0x07 |
| 0x8E | not set by code found so far | | uses the same get-up animations as 0x85 (3869–3874, turning ±1024): the getting-up end of the "stay down" pair, reached from 0x85's own code |
| 0x91 | not set by the match code | | zeroes the facing and plays one of five poses chosen by the d-pad (6223, or 4932 / 4933 / 4191 / 4192), then stands: most likely the **pre-match pose** set by the entrance code outside the match overlay |
| 0x00 | | | the zero state a slot starts in |

## New states (first pass, 2026-11-01)

The state setters are `wrestler_set_state` (0x800A6BE0), `_now` (0x800A6C48, RERUN is
0x10000000 at +0x24), `_free` (0x800A6CDC), `_free_now` (0x800A6D80) and `pair_set_state`
(0x800A6E44). From who sets them and the animations they play (759 Revenge animation files
are byte-identical to World Tour ones; table in the scratch `anim_r2w.json`, worth making a
tool):

- **0x83 / 0x84, turning a dazed opponent around**: L next to a dazed opponent (0x2D),
  animation = World Tour 3091 / 3087. World Tour did this with C-up inside the dazed state.
- **0x8D**: started from the run (`run_step`) with a d-pad direction toward one end of the
  ring at a set x range, depending on `0x8007FC10`; plays the plain run (World Tour 2924) for
  30 frames, an A/B/L/R press with `chance_roll` kind 3 sets a success bit, then runs on. Not
  identified.
- **0x13**: from the run when a 4-point probe ahead hits something (otherwise the rope / crash
  0x11); generic move 10 and a hit on the opponent: a running collision of some kind.
- The others (0x00, 0x08, 0x1A, 0x25, 0x27, 0x31, 0x34, 0x35, 0x37, 0x59–0x5E, 0x65,
  0x68–0x6A, 0x82, 0x85, 0x89–0x91) play animations with no World Tour twin; naming them needs
  RAM (watching which state a known action enters).

Tried in the emulator (`akiemu --game revenge`, 2026-11-01): 6,000 frames of random P1 input
against the CPU reached only states World Tour also has (0x01–0x07, 0x0B, 0x0C, 0x14, 0x1E,
0x1F, 0x28, 0x2B, 0x2C, 0x2E, 0x2F, 0x3A, 0x3B, 0x40–0x46, 0x49, 0x4A, 0x5F). Forcing each new
state onto a standing wrestler shows its first animation (e.g. 0x08 4141, 0x13 4199, 0x1A 3697,
0x25 7289, 0x27 4376 with the opponent into 0x28, 0x59 4391, 0x5C–0x5E 3773–3775, 0x89 3706,
0x8A 3707) but most wait for a partner or context and stay in step 1. Run + Z + up (the 0x8D
branch of `run_step`) did not trigger it from the middle of the ring. Identifying these is
best done by playing in the emulator window and marking (F4) when a Revenge-only move happens.

## CPU vs CPU run (2026-11-01)

`_tools/libretro/states/rv_cpu_cpu.state`: the 1P vs CPU save with P1's flags lo |= **0x4, the
CPU bit in Revenge** (World Tour 0x1); both wrestlers then run on AI. 16,000 frames (about 9
minutes) of Hogan vs Hogan logged every second frame:

- States seen: 0x01, 03, 04, 06, 07, 0B, 0C, 0F, 10, 14, 1B, 1E, 1F, 20, 21, 28, 2A–2F, 32, 33,
  3A–46, 49, 4A, 4F, 50, 53, 54, 5F, 61, 62. The common transitions fit the alignment (stand →
  grapple attempt 0x44 → grapple 0x45 / 0x46 → move 0x49 / 0x4A → down 0x2C → get up 0x43;
  strike 0x1E → hit 0x28 → recover 0x2B; whips 0x0B / 0x0C; pick-up 0x32 / 0x33; ground holds
  0x3A–0x3D; block 0x40 / 0x41; chain move 0x53 / 0x54; evade 0x5F; ropes 0x61 / 0x62). Moves in
  0x49 / 0x4A came from slots 0–13, 61–63, 125, 147–150, 159, 165, 172, 181, as in moves.md.
- None of the new states appeared: they need tag partners, weapons, the apron or trait holders.
- **0x2F** follows the stand and the run and leads to 0x2E or 0x62, which does not fit "flip over";
  it is more likely a stagger or knock-back (to check).
- AI modes seen: 0–3, 6–14, 16–19, 21, 25–28, 30, 31, 33, 35, 37–39, 43; 27 (circling) is most of
  the time. None of the new modes 46–61 ran in singles.

## The state setters

`wrestler_set_state` (`0x800A6BE0`), `wrestler_set_state_now` (`0x800A6C48`, RERUN is
0x10000000 at +0x24), `wrestler_set_state_free` (`0x800A6CDC`, also unlinks),
`wrestler_set_state_free_now` (`0x800A6D80`) and `pair_set_state` (`0x800A6E44`). Names of the
handlers and their bodies: `symbols/revenge/states.toml` (matched one by one) and
`states_aligned.toml` (from the alignment).
