# World Tour: body-part damage

Everything the per-body-part condition does, gathered from the code (2026-10-31). The
damage formula itself is in `moves.md`; this page lists the effects.

## The values

- `Wrestler.region_condition[5]` (+0x33C): **0 head, 1 body, 2 arms, 3 legs, 4 whole body**,
  each **50 when fresh** and floored at 0 (`wrestler_add_region_condition`). Seen in the RAM
  snapshots: 50 at the start, e.g. head 31 and body 4 after a few minutes.
- Read through `wrestler_region_condition`, which **returns 50 while the wrestler is SPECIAL**:
  injuries are ignored for the length of SPECIAL.
- `g_injury_region[slot]` (`0x800F05A0`): the region the wrestler shows as hurt.
- **Nothing heals a body part during a match.** The only writer is `move_apply_hit` (and
  the match start). Health regenerates while standing (`health_regen`: header byte 1 every
  60 frames, byte 2 every 90 while bleeding), but the regions never do.

## How parts get hurt (`move_apply_hit`)

Every move record carries five damage bytes, one per region (`dmg_head`, `dmg_body`,
`dmg_arms`, `dmg_legs`, `dmg_region4`); on the hit the defender's regions drop by those
amounts, independently of the health damage. A strike to the face usually takes 1–2 from
the head; a big slam takes from several regions at once.

After the hit, if the move targets a region (0–3) and that region is now the defender's
**weakest**, it becomes `g_injury_region`.

## What a hurt part changes

| Effect | Region | Where | Detail |
|--------|--------|-------|--------|
| **Weaker attacks** | the attacking part (`atk_part`) | `move_apply_hit` | the base term of the damage is `(condition + 50) × power / 100`: ×1.0 at full condition, ×0.5 at 0, so ruined arms halve that part of the damage of arm moves (the stat and spirit terms of the formula do not depend on condition) |
| **Slower walking and running** | legs (3) | `walk_speed` | the walk class (header byte 4) drops one step for each threshold the legs are below (30 and 10; class 1 only 10): 6.0 → 5.5 → 5.0 → 4.5 units a frame, and running is twice that (`movement.md`) |
| **Injured idle and walk** | `g_injury_region` | `wrestler_base_anim` | below 26 (about half) the wrestler holds that part: injured idles 2905–2910 and walks 2918–2923 (`animation_states.md`) |
| **Can submit** | the hold's target | `state_36_hold_defender` | a submission hold can only make a wrestler give up when the targeted region is at 0 (plus spirit < 91, and not on the first hold, `match_end.md`) |

That is the whole list: no other function reads the regions. In particular a hurt part
does not change reversal chances, the time spent down, the get-up speed or the pin count
(those use health, spirit and the last hit's damage), and the AI does not aim at hurt parts.

## Between matches

At the end of a match (900 frames after the decision, unless `0x800F16F6` is set) each
wrestler's health, five region conditions, `g_injury_region`, bleeding (bit 0 of byte 6) and a
power-out flag (bit 1, `flags_hi` 0x8000) are written to `g_match_results` (`0x80061868`, 8
bytes per slot: health, head, arms, legs, body, whole, flags, injury region), and the next
match starts from them (`match_init_wrestlers`, `0x800B3400`). So **injuries and blood carry
over** in modes that keep the table (probably the league / tournament; an exhibition starts
from 255 health and 50 everywhere, as in every snapshot).

## For the port

- Five bytes per wrestler, decreased by the move tables, 50 when fresh, frozen at 50 while
  SPECIAL; four readers as above.
- Keep the between-match record if the career mode wants injuries to carry over.
