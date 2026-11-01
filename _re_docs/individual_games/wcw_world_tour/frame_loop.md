# World Tour: frame loop, input and random numbers

The basics a frame-exact port needs (2026-10-30, read from the code).

## Frame loop (`match_run`)

Each pass of the loop is one game frame:

1. `pads_read` (`0x8000544C`) reads the controllers into `g_pads` (12 bytes per port: held,
   newly pressed, stick x / y).
2. `match_tick` runs the match logic once (unless the pause menu holds it): the phase machine,
   the clock, the end-of-match checks, and `wrestlers_update_all`.
3. The pause menu (`sub_8009AE40`, Start) and the fades.

**The logic runs at 30 frames per second**: the match clock counts 30 ticks per second
(`match_state.md`), there is one `match_tick` per loop pass, and the recomp's notes describe
it as a 30 fps game (the TV still scans at 60 Hz, each frame shown twice). Higher display rates
in the port come from interpolating poses, as `AGENTS.md` says.

Within a frame, wrestlers are updated by `wrestlers_update_all` starting from
`g_update_first`, which rotates 0–3 every frame so no slot always acts first; grapple
leaders are updated together with their linked partner (`match_state.md`).

## Input

`wrestler_tick` builds the input for the wrestler being updated:

- **Human:** `g_input_held` / `g_input_pressed` from its pad (`g_slot_pad`, 1-based), then
  `input_remap`; `g_input_far` = the analog stick pushed past **41** on either axis. The stick
  is not used for walking at all (the d-pad gives 8 fixed directions, `movement.md`); pushing
  it is the taunt / special input.
- **CPU (`flags_lo & 1`):** `ai_think` fills `g_ai[slot]` (+2 held, +4 pressed, +8 far), which
  is then used exactly like pad input. So the AI "presses buttons".

### Button configuration (`input_remap`)

The game reads **actions**, not physical buttons. Start and the d-pad (`0x1FC0`) pass through
unchanged; seven actions are taken from the buttons chosen in the options
(`0x80061FF8 + pad * 8`, one byte per action: 1 A, 2 B, 3 L, 4 R, 5 Z, 7 C-up, 8 C-down,
9 C-left, 10 C-right):

| Action | Bit the game code tests | Default meaning |
|--------|-------------------------|-----------------|
| 0 | 0x8000 (A) | grapple |
| 1 | 0x4000 (B) | strike |
| 2 | 0x0004 (C-down) | run / whip / pin / climb |
| 3 | 0x0010 (R) | block / pick up / release |
| 4 | 0x0020 (L) | evade / drag |
| 5 | 0x0008 (C-up) | flip over / tag / climb in |
| 6 | 0x0001 (C-right) | switch target |

So every button test in the game code is really an action test, and Z can be bound to any
action.

## Random numbers

There are two generators and a shuffle pool.

**`rng_main`** (`0x80011BF0`, state `0x80032C40`, unsigned 32-bit):

```
s = ((4*s + 2) * (4*s + 3)) >> 2      // wraps at 32 bits
return s
```

**The idle thread calls `rng_main` in a busy loop** whenever the CPU has nothing else to do
(`sub_800004AC`, the boot thread that becomes the idle loop; the recomp had to patch this loop
to yield). So its state advances by an amount that depends on how much spare CPU time each
frame had (about 21,600 idle-loop draws per match frame in mupen64plus; the game's own draws are
16 per frame in a CPU-vs-CPU match, measured with the idle call patched out, 2026-10-31). That makes the game's randomness depend on hardware timing; it cannot be replayed
exactly, and a port does not need to (and cannot) reproduce particular rolls.

**`rng_match`** (`0x800A0468`, a linear congruential generator):

```
s = s * 0x5D588B65 + inc              // state 0x800F05C0, inc 0x800F05C4 (odd)
r = (s & 0xFFFFF) / 1000; repeat while r >= 1000
return r                              // 0..999
```

**`rng_seed_match(n)`** (`0x800A02D4`, called with n = 10 when the match starts): `s =
rng_main()`, then `rng_main` n & 15 more times, `inc = rng_main() | 1`, then fills the
**32-entry pool** `g_rand_pool` (`0x800F05C8`) with `rng_match`.

**`rand_pool_next`** (`0x800A039C`) is a Bays–Durham style shuffle: it picks slot
`rng_main() & 31`, returns that pool entry (0–999) and refills the slot with `rng_match`.

**`chance_check(chance)`** (`0x800A0410`) succeeds when `rand_pool_next() <= chance`, so a
chance of c means **(c + 1) / 1000**. `chance_roll` (the reversal, recovery, counter and AI
chances) computes c from tables and calls this; 41 functions use it, half of them the AI.

For the port: one seeded generator returning 0–999 with the same `<=` comparison reproduces
every probability in the game.

## The chance tables (`chance_roll`, `0x8009EBF0`)

`chance_roll(w, other, kind)` computes a chance out of 1000 (capped at 1000) and rolls it with
`chance_check`. Step tables are read as "the first row whose threshold the value reaches"
(tables at `0x800E6D60`–`0x800E6ED2`); the formulas use 1000 as numerator.

| Kind | Used for | Chance (‰) |
|------|----------|------------|
| 0 | grapple reversal (`sub_800D1CE8`) | by `w.spirit`: 100 → 500, 81+ → 333, 61+ → 250, 31+ → 125, 11+ → 62, else 31 |
| 1 | the defender's reversal try in a grapple (`grapple_defender_input`, `sub_800DA118`) | same table as 0 |
| 2 | (no caller) | by health_eff: ≥196 → 3.9 … <32 → 62.5; 0 while SPECIAL |
| 3 | catching yourself running into the post / ropes (`state_10_crash`, `state_04`, `whip_run_inputs`) | by spirit: 100 → 1000, 81+ → 500, 61+ → 333, 31+ → 250, 11+ → 200, else 125 |
| 4 | **bleeding** (`move_aftermath_check`) | 1000 / (15 − def.header[0] − move.f07 − att.spirit / 30) |
| 5 | **TKO** (`move_aftermath_check`) | by `move.f08` 0–5: 0, 133, 166, 222, 333, 666; doubled while the attacker is SPECIAL |
| 6 | **saving yourself from a TKO** (`state_25_tko_check`, per mash) | 1000 / (3 × TKO staggers so far (`0x800F05B8[slot]`) + {14, 11, 8, 5}[header[27]] − 3) |
| 7 | random give-up in a hold (rule 0x08) | by health_eff: ≥196 → 31, ≥128 → 62, ≥64 → 125, else 250 |
| 8 | the roll when going down (`state_28_down`) | (100 − spirit) / 40 / (32 when health_eff ≥ 128, else 16) × 1000; 0 while SPECIAL |
| 9 | (no caller) | by spirit: 100 → 500, 91+ → 62.5, 81+ → 15.6, 31+ → 7.8, else 3.9 |
| 10 | **crowd weapon** handed over (`state_6F_crowd_weapon`) | 1000 / (3 − header[16]): 333, 500 or 1000 |
| 11 | the better strike counter (`block_counter`) | by spirit: 100 → 1000, 76+ → 500, 51+ → 300, 26+ → 200, else 100 |
| 12 | lifting into a carry (`state_66_carry_attacker`) | 1000 while SPECIAL; else by the weight difference (header[23] / 3, other − self, clamped) and the other's health_eff, table `0x800E6E58`: e.g. same weight: ≥192 → 400 … <32 → 900 |
| 13 | **countering a pick-up** (`state_2E_pickup_defender`, also needs health_eff < 129) | by spirit: 100 → 300, 80+ → 200, 60+ → 100, 40+ → 50, else 0 |
| 14 | **powering out of a hold** (trait 0x1000000) | 1000 / (health_eff / 10 + spirit / 10 + 1) |
| 15 | blocking / grabbing from the apron (`strike_land`, `grapple_connect`, the dive counter) | by `recover_count`: ≥120 → 100, 60+ → 125, 45+ → 166, 30+ → 222, 15+ → 333, else 666 |

The header bytes used here (`WrestlerHeader` +0x00, +0x10, +0x17, +0x1B) are per-wrestler
stats; from how they are used, probably blood resistance, crowd popularity, weight class and
toughness against a TKO (guesses). Checked against the roster (2026-11-01):

| Byte | Values | Reading |
|------|--------|---------|
| +0x00 | 1–2 (2: Hogan, Bagwell, Nash, Savage, Luger, Rick Steiner, Flair, Malenko, Regal, DDP) | bleeding **tendency** (higher bleeds more: 1000 / (15 − it − ...)) |
| +0x10 | 0–2 (2: Hogan and both Stings) | how readily the crowd hands a weapon |
| +0x17 | 0–11 (Giant 11, Hogan / Nash 8, Norton / Rick Steiner 6, most 5, cruiserweights 0–2) | **weight class**, confirmed |
| +0x1B | 0–3 (3: the Steiners; 0: Hall, DDP) | TKO-save index into {14, 11, 8, 5}: a higher value saves more easily |

Also +0x01 / +0x02 health regained (4 / 2, the Giant 6 / 3), +0x04 walk class (Giant and Nash 1,
cruiserweights 3) and +0x1A dive reach (10, high flyers 12).

## Open questions

- `pads_read` details (controller pak / rumble handling is out of scope).
- The exact default button bindings stored at `0x80061FF8` (filled by the options menu, not in
  the match code).
