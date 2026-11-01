# World Tour: which states play which animations (2901–3027)

Andrew tagged 84 animations in the previewer by watching them (2026-10-30); the tags are
saved in `anim_tags.json` next to this file (anim id → tag, his wording). This note checks
each tag against the code that starts the animation, using the state handlers (names in
`symbols/wcw/states.toml`) and the pickers documented in `symbols/wcw/models.toml`.

How the code was searched: constant ids passed to `wrestler_set_anim` or assigned before it
in the Hex-Rays output, `li` immediates in the raw MIPS, and s16 tables in the overlay
data. Ids that the code computes (`anim + 1`, `3012 + n`) were followed by hand.

Overall the tags agree with the code wherever the code is specific. The code adds three
things the tags could not show: **which stance style or injured body region picks each idle,
walk and block variant**, **that every grapple situation has a 4-animation set (attacker,
defender, attacker break, defender break)**, and **that defender animations are usually the
attacker's id + 1**.

## Idle, walk, block: one picker for all (`wrestler_base_anim`, `0x800A1EEC`)

Called with a kind (0 walk, 1 idle, 2 block, 3–8 hit while blocking) by the stand, walk,
block, ropes, tag and run-related states. It reads a table at `0x800E6F40`:

| Row | Index | Animations | Andrew's tags |
|-----|-------|------------|---------------|
| walk | stance style 0–3 | 2911, 2912, 2913, 2914 | short walk + 3 variants |
| idle | stance style 0–3 | 2901, 2902, 2903, 2904 | idle, idle variant ×2, "slightly injured" |
| injured walk | body region 0–3 | 2918, 2919, 2921, 2923 | head, torso, left arm, left leg |
| injured idle | body region 0–3 | 2905, 2906, 2908, 2910 | head/neck, torso, left arm, "right arm for idle variant" |
| block | stance style 0–3 | 2969, 2972, 2972, 2972 | "strong block some characters use like Lex Luger", blocking |
| hit while blocking (×6 rows) | stance style 0–3 | 2970, 2973, 2973, 2973 | struck doing strong block, struck when blocking |

- **Stance style** is 2 bits of the wrestler header (`WrestlerHeader.traits` bits 2–5, one
  set per wrestler). Style 0 has its own block (2969/2970), which is the Lex Luger block.
  2904 is style 3's idle, so "slightly injured" is probably just how that style stands.
- **Body region** is `g_injury_region[slot]` (`0x800F05A0`, guess). The injured versions
  play when that region's condition (`Wrestler +0x33C + region`) is below 26. Region 3
  pairs 2923 (tagged left leg) with 2910 (tagged right arm), so one of those two tags is
  probably off; region 3 is most likely a leg.
- **Not in the table:** 2907, 2909, 2920 and 2922 (other injured idles and walks). No code
  starts them by a constant either, so they may be unused or picked elsewhere.
- Overrides: when `flags_hi & 0x2000` the picker returns 3245/3246 (3253/3254 if `0x4000`
  is also set) instead. When the third (float) argument is above 355 it returns 2928
  (walk) or 2929 (idle); both are untagged. What that argument measures is not traced yet.

## Running (`state_03_run`, `state_19`, `state_04`)

| Anim | Tag | Code |
|------|-----|------|
| 2924 | run normal | `state_03_run` |
| 2925 | run transition | `state_03_run` |
| 2926 | run stop | `state_03_run` |
| 2931 | – | `state_03_run` (untagged) |
| 2927 | run jump over wrestler on ground | `state_19` (so state 0x19 is the hurdle over a downed opponent) |
| 2930 | bounce off rope | no reference found yet |
| 2995–2997, 3000 | 3000: whipped but caught the ropes | `state_04`. State 0x04 is therefore the rope rebound / rope-catch state |
| 2915 | pushing | `sub_800A2964` (walking into the opponent) |
| 2916, 2917 | push transition | no constant reference |

## Grapples: one set of four per situation

`grapple_anim_attacker` (`0x800C3C6C`), `grapple_anim_defender` (`0x800C421C`) and
`grapple_timeout` (`0x800C46EC`) pick by the `GrappleCtx` bits in `Wrestler +0x24`. The
defender's animation is always the attacker's + 1, and when the grapple times out without a
move, `state_3F_grapple_defender` gives the defender the attacker's break animation + 1.

| GrappleCtx bit | Attacker | Defender | Attacker break | Defender break | Andrew's tags |
|----------------|----------|----------|----------------|----------------|---------------|
| 0x80 HOLD (front) | 2936 | 2937 | 2938 | 2939 | grappling, grappled, break grapple, break grapple alternate |
| 0x08 REAR | 2940 | 2941 | 2942 | 2943 | grapple from behind, grappled from behind, …broken, breaking… |
| 0x10 CORNER | 2944 | 2945 | 2946 | 2947 | –, grappled against corner, "break, possibly referee", "referee break alternate" |
| 0x20 (DEF_ON_APRON) | 2948 | 2949 | 2950 | 2951 | grapple from apron, –, –, strike over apron |
| 0x40 (ATK_ON_APRON) | 2952 | 2953 | 2954 | 2955 | –, –, grappled on apron and struck from ring, strike related to grapple across rope |
| 0x10000 test of strength | 2957 | 2958 | | | grapple strength battle (+ alternate) |
| 0x40000 (unknown) | 2934 | 2935 | | | untagged |

Corrections and findings from this:

- 2946/2947 are not a referee break. They are the normal timeout break of a **corner**
  grapple. Corner grapples do look like a referee break, which explains the tag.
- 0x20 and 0x40, previously unnamed, are the **apron / across-the-ropes** grapples (one
  with the attacker on the apron, one with the attacker in the ring). Their breaks are
  strikes, which matches the tags. The `GrappleCtx` notes are updated.
- The test of strength runs in states 0x40 and 0x41, which use 2959/2960 for the two sides
  and 2961/2962 for overpowering and being overpowered (all four tags match).
- Grapple attempts: `sub_800C5C7C` starts 2932 (light), 2933 (strong, button held) and 2956
  (untagged). `state_3D_grapple_attempt` plays 2965 when the grapple is parried, and
  `state_53` plays 2963 (dodge back) and 2964 (parry). So **state 0x53 is the grapple
  counter / dodge** and 0x3D the attempt.
- 2967/2968 (placing on / being placed on the turnbuckle) come from
  `state_3E_grapple_attacker` and `state_3F_grapple_defender`, so moving to the corner is
  part of the grapple states, not a move.
- `state_39_block` uses the picker (block) and 2966 (untagged); `state_3A` uses the picker's
  hit-while-blocking rows. So **0x39 = blocking, 0x3A = hit while blocking**.

## Irish whip (`state_0A_whip_attacker`, `state_0B_whip_runner`, `sub_800D4D4C`)

| Anim | Tag | Code |
|------|-----|------|
| 2975 | setting up for Irish whip | 0x0A attacker |
| 2976 | being Irish whipped | 0x0B runner |
| 2977, 2980, 2981 | following through (+ alt) | 0x0A attacker |
| 2978, 2979, 2982 | being whipped | 0x0B runner |
| 2983 | whip failure | 0x0A attacker |
| 2984 | resisting a whip | 0x0B runner |
| 2988 | whipped up onto the ring from outside | 0x0B runner |
| 2989 | whipping someone up onto the ring | `sub_800D4D4C` (with 2985, 2987) |
| 2990 | whipped into a solid object | 0x0B runner (with 2986, 2992, 2994) |
| 2991, 2993 | – | 0x0A attacker |

All tags match the attacker/runner split. The whip-from-outside set (2985–2994) is mostly
untagged; the tagged ones show it covers whipping between the floor, ring and barrier.

## Where the partner stands (two-wrestler animations)

**Animations carry no partner offset.** Every animation is authored around its own origin:
frame 0 has the pelvis at about (0, 85, 0) for both halves of a pair (2936/2937, 2940/2941,
3496/3497, 4804/4805 all checked). The game keeps each wrestler's `pos` as the origin of the
animation it is playing and its `facing` as the animation's heading, and it places the two
wrestlers **before** the move, during the grapple lock-up. Move animations are then started
on both wrestlers without moving them again (`grapple_choose_move` → states 0x42/0x43 never
call a placement helper; only the pelvis continuity below applies).

### Lock-up placement (`grapple_anim_attacker`, `grapple_anim_defender`)

1. `vec_xz_dist_angle(attacker pelvis, defender pelvis)` gives the angle from attacker to
   defender (`g_grapple_angle[slot]`, `0x800F19F0`).
2. A distance and a target heading are chosen by `GrappleCtx`:

   | Situation | Distance (pelvis to pelvis) | Attacker heading | Defender heading | Measured from |
   |-----------|-----------------------------|------------------|------------------|---------------|
   | front (HOLD) | **73.5** | toward defender | toward attacker | defender pelvis |
   | rear (REAR) | **53.5** | toward defender | same as attacker (attacker behind) | defender pelvis |
   | corner | **72.1** (`0x800E8920`) | opposite the defender's facing (face to face) | toward attacker | defender pelvis |
   | apron (DEF_ON_APRON) | **76.0** | fixed per ring side, from `0x800E7030[defender.ring_zone]` | toward attacker | defender **pos** |
   | ropes (ATK_ON_APRON) | not moved; keeps its facing | | toward attacker | |
   | test of strength | **80.0** | toward defender | toward attacker | defender pelvis |

3. Both wrestlers' `pos` is snapped onto their own pelvis (`wrestler_translate` by
   pelvis − pos), so the next animation starts where the body visibly is.
4. The attacker's target point is `point_at_offset(defender pelvis, (0, distance), heading +
   0x800)`: `distance` units from the defender, back along the line toward the attacker.
   `wrestler_slide_to` spreads the move over **6 frames** (10 for the test of strength), and
   the heading turns by the same number of steps (`Wrestler +0x338` = angle step per frame).

So when a front grapple move starts, the two origins are 73.5 apart and face each other.
The previewer's partner presets use these numbers.

### Root motion between animations (`model_animate`)

When `wrestler_set_anim` is called with flag bit 0 set, `model_animate` shifts `pos` so
that the new animation's pelvis lands where the old one's was (x and z only). The flag is
kept per slot at `0x800F0640`. Both halves of a move start this way: `state_42_attack_move`
and `state_43_receive_move` call `wrestler_set_anim(w, anim, 1)`. The lock-up animations end
with the pelvis over the origin (2936 at (0, 88, 0), 2937 at (0, 82, 0)), so at the start of
a front grapple move the two **pelvises** are 73.5 apart and each animation is shifted by
its own frame-0 pelvis offset (a few units). The previewer does the same when the partner is
shown. Other flag bits: 2 turns on the 5-frame blend from the previous pose
(`model_blend_prev`), and 4 makes that blend skip re-centring the joints on the pelvis.
The head look-at (joint 5 aimed at the opponent through a per-slot target at `0x800F0658`)
is applied whatever the flag.

### Ground moves

- **Strikes on a downed opponent** (`state_23_ground_attack`, slots 154 face-up B and 155
  face-down B) do **no placement at all**. The attacker hits from where it stands. At the hit
  frame `ai_learn_strike_reach` records the distance from the attacker's `pos` to the joint
  that hits, averaged per strike type in a per-wrestler table at `0x800F1870`. That looks
  like the CPU learning how close it has to be for each strike (guess).
- **Grappling a downed opponent** (`state_33`) walks the attacker to a fixed spot:
  `point_at_offset(opponent.pos, (0, d), opponent.facing)` with **d = −130 near the head**
  or **+150 near the legs**, both negated when the opponent lies face down
  (`flags_lo & 0x8000`). Head or legs is chosen from the angle between the opponent's facing
  and the attacker (`+0x2E` bit 0), and it also picks the move slot (158/159 head, 162/163
  legs). The walk runs at speed 4 (`sub_800A4924`) with the attacker turning toward the
  opponent; the move starts on arrival.
- These ground offsets are from the opponent's **origin**, not its pelvis. A lying wrestler's
  origin is usually far from the body. For example, receive animation 3497 ends with the pelvis
  110 units from its origin. So in the previewer a ground move can only be lined up by also
  playing the receiver's lying pose; the presets don't cover it yet.

Not checked live: the sign conventions above follow the code (angle 0 = +Z, positive toward
+X, `facing` as in `match_state.md`). A paused match during a lock-up would confirm the
73.5 distance in one reading of the two pelvis joints.

## Corners and ropes

| Anim | Tag | Code |
|------|-----|------|
| 3001, 3002 | 3002: just got whipped into corner | `state_13`, `state_15` |
| 3003, 3005 | 3003: settled into corner | `state_13` |
| 3004, 3006 | falling out of / removed from corner | `state_24` |
| 3008 | – | `state_14` |
| 3009–3011 | – | `state_10` |
| 3013–3015 | onto ropes / onto corner / acrobatic onto corner | `state_09_turnbuckle` plays **3012 + `g_climb_kind[slot]`**, set from the wrestler header bytes 17–22 or constants, so the climb animation depends on the wrestler and situation |
| 3016, 3017, 3018 | taunt on corner, leave corner, jump back off | `state_09_turnbuckle` |
| 3019, 3020 | waiting on corner (+ alt) | `state_5D`, `state_09`, `state_71`, `state_73` |
| 3021 | stepping through ropes | `state_54_ropes_a` (with 3024; these states then add an offset to the current id) |
| 3027 | – | `state_55_ropes_b` |

So states 0x13/0x15 are being sent into the corner and 0x24 is coming out of it. 0x09 is the
turnbuckle climb and perch.

## Tagged animations with no code reference

2907, 2909, 2916, 2917, 2920, 2922 and 2930 are never started by a constant, a table or an
offset found so far. Either they come from a computed id not yet traced, or they are unused.
A live check (setting `Wrestler.anim_id` while paused) would settle it.

## Next steps

- Name the states this pins down in `states.toml` (0x04 rope rebound, 0x19 hurdle, 0x39/0x3A
  block, 0x53 grapple counter, 0x13/0x15/0x24 corner) after checking each handler.
- Trace the float argument of `wrestler_base_anim` (the 350/355 thresholds) and the
  `flags_hi` 0x2000/0x4000 overrides (3245/3246/3253/3254).
- Decode the per-wrestler header bytes 17–22 (climb kinds) and traits bits 2–5 (stance
  style) across the roster.

## The remaining small states (2026-10-31)

Read from the code; names in `symbols/wcw/states.toml`.

| State | What it is |
|-------|------------|
| 0x00, 0x74 | match intro: entrance poses (wrestler slots 146 / 147), wait for the bell; the illegal tag partner then goes to the apron (0x70) |
| 0x02 | turning round at the end of the apron (3362 / 3363) |
| 0x0E / 0x0F | second entries of the whip-context states 0x0C / 0x0D |
| 0x12 | running into a ring post on the floor (3314, generic move 10) |
| 0x17 | **SPECIAL activation pose** (3421, 3422 on the apron), played once when SPECIAL comes on (`flags_lo` 0x2000); cancellable by any action |
| 0x18 / 0x19 | L against a runner: slot 145; a runner passing close is tripped (0x19) |
| 0x34 | ground grapple, defender side of 0x33 |
| 0x37 / 0x38 | pin straight out of a move (`grapples.md`, move flags 0x400) |
| 0x50 | knocked down by a dive (3184 / 3185) |
| 0x53 | L evade, sidestep (2963 / 2964) |
| 0x62 | walking back to the ring from the floor (after the decision, or rule 0x04) |
| 0x63 | rolling out of the ring |
| 0x6D / 0x6E | pulling an opponent off the turnbuckle (slots 103 / 104) |
| 0x70 | illegal tag partner going back to the apron |
| 0x75 | an eliminated wrestler leaving the ring |
| 0x76 | eliminated in an elimination tag match: collapses (3314, 3319, 3325) |
| 0x77 | waits for a button, then stands |

After the decision, `state_01_stand` sends the winner to celebrate: in the ring the taunt
(0x16), from the apron in through the ropes, from the floor back to the ring (0x62).
