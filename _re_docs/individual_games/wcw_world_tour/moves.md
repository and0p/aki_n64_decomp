# WCW vs. nWo: World Tour — moves, move sets and their files

Found from the live recomp at two paused moves (see `match_state.md`). All addresses are N64
virtual addresses inside the match overlay `ovl_b`, unless the address is in the main segment.

## Summary

- A wrestler's **move set** is 222 move slots. Each slot has a 24-byte bit-packed parameter
  record and a short list of animation ids (attacker, receiver, and sometimes a third).
- The game copies four files per wrestler from the ROM's asset archive into fixed RAM tables
  when the match starts. The move-set files are numbered per roster entry.
- A wrestler doing a move stores **which move set** (`+0x370`) and **which slot** (`+0x372`).
  Animation ids are looked up from the move set, and the slot's parameters are unpacked into
  the wrestler at `+0x378`.
- Attacker and receiver both play animations from the **attacker's** move set: the attacker
  takes part 0 and the receiver part 1. The receiver's own `+0x370`/`+0x372` are left alone; in
  both pauses P2's still pointed at its own move set (1). That explains the even/odd pairs seen in both
  snapshots.

## Per-wrestler move tables in RAM (filled at match start)

| RAM | Stride per wrestler slot | File | Contents |
|-----|--------------------------|------|----------|
| `0x800F1D60` | 0x20 | base + 0 | header (0x20 bytes, not decoded) |
| `0x800F32A0` | 0x14D8 | base + 1 | 8 unused bytes, then 222 × 24-byte move records (file size 0x14D0) |
| `0x800F1F60` | 0x4D0 | base + 2 | 8 unused bytes, then the animation lists as s16 (file size 0x4C4) |
| `0x800F1DE0` | 0x60 | base + 3 | 0x60 bytes, not decoded |

`func_800B07C0` fills them. It first loads file 221 to `0x800F8600 + 8` (shared by
everyone, 0x3F0 bytes, not decoded). Then for each of the 4 slots it reads the roster entry at
`0x80061828 + slot*4` (`0xFFFFFFFF` = empty). The low byte minus 1 is the roster index and
bits 8–11 are a group. The base file number is `s16 ((s16*) word[0x800E74CC + group*4])[roster index]`.
In both snapshots the two slots held `0x101` (roster 0, group 1, files 430–433). It was a
mirror match, which matches the screenshots: both wrestlers look the same.

## Roster → move-set files (confirmed for Lex Luger, the rest from the tables)

`word[0x800E74CC + group*4]` points to an array of base file numbers, indexed by roster
index. The arrays overlap in memory, so a group's list ends where the next group's starts.
Matching them against the in-game roster order in `_re_docs/gamefaq/world_tour_guide.txt`
gives a clean fit: every group has the right number of wrestlers, and both versions of Sting
share one move set.

| Group | Wrestlers (base file) |
|-------|-----------------------|
| 0 nWo | Hollywood Hogan 374, nWo Sting 534, Buff Bagwell 258, Eric Bischoff 246, Scott Norton 462, Kevin Nash 402, Scott Hall 338, Syxx 542, Randy Savage 514 |
| 1 WCW | Lex Luger 430, Sting 534, The Giant 326, Scott Steiner 518, Rick Steiner 486, Ric Flair 294, Ultimo Dragon 282, Dean Malenko 438, Eddy Guerrero 250, Rey Misterio Jr. 458, Chris Benoit 266, Steven Regal 426, Diamond Dallas Page 270 |
| 2 DOA | Sumo Jo 570, Kim Chee 610, Blackheart 330, Puchteca 474, Hannibal 362, Powder Keg 318, Dim Sum 322, Saladin 262, Ali Baba 526, Wrath 358 |
| 3 IU | Black Ninja 510, Shaolin 382, The Unknown 274, The Claw 334, Black Belt 546, Paco Loco 590, Shaman 554, Master Fuji 306, Glacier 350 |
| 4 WWW | Joe Bruiser 238 |
| 5 WWW | Black Widow 586 |

The two paused matches were **Lex Luger vs Lex Luger** (roster entry `0x101` = group 1, index 0).
That fits the screenshots: two identical blond wrestlers.
57 of the 99 move-set groups are not used by any roster entry. They may be leftovers from
VPW64.

## Move slots → controller inputs (from the FAQ)

Global move id = `W0 & 0x7FF` of the move record. For each FAQ input (for example "Strong
Front Grapples / B"), the slot below is the one where wrestlers whose FAQ entries share a move
name also share a global id, across all 42 wrestlers. Scores are 0.7–0.95 agreement, so treat
any single entry as **likely**, not certain.

| Input | Slot |
|-------|------|
| Strikes: Tap B close / far, Up+Tap B close / far | 118 / 120 / 119 / 121 (from `state_1D_strike`, see `strikes.md`) |
| Strikes: Hold B, Up+Hold B, Run+B, Run+L/R+B | 123, 124, 125, 126 |
| Weak front grapple: A, Up+A, Down+A, B, Up+B, Down+B | 0, 1, 2, **3**, 8, 13 (confirmed from code; the FAQ fit gave 7 for B) |
| Strong front grapple: A, Up+A, Down+A, B, Up+B, Down+B | 18, 23, 28, 33, 38, 43 (confirmed from code) |
| Weak rear grapple: A, B (no direction / with direction) | 61/62, 63/64 (confirmed from code) |
| Strong rear grapple: A, B (no direction / with direction) | 65/66, 67/68 (confirmed from code) |
| Irish whip: Tap A, Up+Tap A, Hold A, Up+Hold A | 74, 75, 76, 77 |
| Special (finisher): front, rear | 58, 69 |
| Opponent face up: A near head, A near legs, B | 158, 162, 154 |
| Opponent face down: A near head, A near legs, B | 84, 163, 155 |
| Turnbuckle: opponent standing, opponent down | 166, 167 |
| Outside: running to floor, running to post, apron to floor, slingshot to floor | 173 (0.96), 172 (0.82), 176 (0.88), 178 (0.88). Slot 59 was a wrong fit; slingshot to inside is probably 179 (weak fit) |
| Corner: Tap A+A, Hold A+A, Hold A+Up+A, Hold A+Down+A, Run+B | 83, 88, 93, 98, 128 |

The grapple rows are now read from the game's own table (`g_grapple_slots`, used by
`grapple_choose_move`). Every grapple move except weak A takes **5 slots: the move followed by
its 4 reversal variants**. When the defender reverses, `reversal_variant` picks
`slot + 1..4`, using 2 bits per global move id from the *defender's* move-set file 3 (the
0x60-byte file is 384 moves × 2 bits). Use `py tools/sym/sym.py show grapple_choose_move`
for the full input rules (Irish whip, rear carry, corner moves, specials).

Checked against the pauses:

- Snapshot 2 was **slot 1 = weak front grapple Up+A**, which for Luger is "Elbow to neck".
  That fits Andrew's note: it looked like a strike but came from a grapple.
- Snapshot 1 was **slot 208**. Slots 200–214 are filled for most wrestlers but no FAQ input
  explains them. Since the receiver was seated on the mat, they may be moves on a seated or
  dazed opponent. Still open.

## Animation lookup — `func_800A2400(w, part)` (confirmed)

```
first = s16 0x800E7BD0[w->move]           // where this move's list starts
anim  = s16 animlist[w->moveset][first + part] + 0xD82
animlist[n] = (s16*)(0x800F1F68 + n*0x4D0)
```

`0x800E7BD0` is a fixed table of 222 starting points. Most moves take 2 entries, some take 3
or more. Part 0 is the attacker's animation and part 1 the receiver's. The `+0xD82` offset maps
the list values into the global animation numbering used by `set_anim` (`func_800A0E48`).

Both pauses check out:

| Snapshot | Move set / slot | first | entries | Animations from RAM |
|----------|-----------------|-------|---------|---------------------|
| 1 (rear hold, 04:59) | 0 / 208 | 498 | 0x542, 0x543 → 0x12C4, 0x12C5 | P1 0x12C4, P2 0x12C5 |
| 2 (after grapple, 05:24) | 0 / 1 | 2 | 0x26, 0x27 → 0x0DA8, 0x0DA9 | P1 0x0DA8, P2 0x0DA9 |

## Choosing a move — `func_800A63F8(w, moveset, slot)` (confirmed)

Sets `w+0x370 = moveset` (s8) and `w+0x372 = slot` (s16). It then unpacks that slot's record
from `0x800F32A0 + moveset*0x14D8 + 8 + slot*24` into `w+0x378` with `func_800A6514`.
`func_800A649C(w, src*, slot)` is a variant that copies the move set from another wrestler's
`+0x370`.

## Move record: 24 packed bytes → 36 bytes at `w+0x378`

Six big-endian words, `W0`–`W5`. The unpacked layout follows (offsets are from `+0x378`).
Field names are not known yet; the bit positions are exact. 6-bit fields marked † are
sign-extended (bit 5 set → `| 0xC0`).

| Unpacked off | Type | Source bits |
|--------------|------|-------------|
| 0x00 | u16 | `W0 & 0x7FF`, probably the global move id (0x139 for the snapshot 2 move) |
| 0x02 | u16 | `(W5 >> 5) & 0xFFF`. Climbs with slot number, so it may be a name or string index |
| 0x04 | u8 | `(W0 >> 11) & 0x3F` |
| 0x05 | s8 † | `(W0 >> 17) & 0x3F` |
| 0x06 | s8 † | `(W0 >> 23) & 0x3F` |
| 0x07 | u8 | `W1 & 7` |
| 0x08 | u8 | `(W1 >> 3) & 7` |
| 0x09 | u8 | `(W1 >> 6) & 7` |
| 0x0A | u8 | `(W1 >> 9) & 7` |
| 0x0B | u8 | `W2 & 0x1F` |
| 0x0C | u8 | `(W2 >> 5) & 0x1F` |
| 0x0D | u8 | `(W2 >> 10) & 0x1F` |
| 0x0E | u8 | `W5 & 0x1F` |
| 0x0F | u8 | `(W3 >> 22) & 7` |
| 0x10 | u8 | `(W3 >> 28) & 0xF` |
| 0x11 | u8 | `W4 & 0xF` |
| 0x12 | u8 | `(W4 >> 4) & 0xF` |
| 0x13 | u8 | `(W1 >> 28) & 0xF` |
| 0x14 | u8 | `(W1 >> 12) & 0xF` |
| 0x15 | u8 | `(W3 >> 25) & 7` |
| 0x16 | u8 | `(W3 >> 16) & 0x3F` |
| 0x17 | u8 | `W3 & 0xFF` |
| 0x18 | u8 | `(W2 >> 20) & 0x3F` |
| 0x19 | u8 | `(W2 >> 15) & 0x1F` |
| 0x1A | u8 | `(W3 >> 8) & 0xFF` |
| 0x1B | u8 | `(W2 >> 26) & 0x3F` |
| 0x1C | s8 † | `(W1 >> 16) & 0x3F` |
| 0x1D | s8 † | `(W1 >> 22) & 0x3F` |
| 0x20 | u32 | `(W4 >> 8) & 0xFFFFFF` |

Field names are now in `MoveParams` (`symbols/wcw/types.h`), worked out from the code that
reads each field and checked against the value ranges across all 43 roster move sets
(7569 non-empty slots). See the next section.

In the attacker's state handler (state 0x42, `state_42_attack_move`), `w+0x04` counts down
the frames left. When it reaches 1, `move_apply_hit(self, self, opponent)` runs. At 0 the
move ends through `wrestler_end_move`.

Slots that are all zero are empty (that wrestler has no move there).

## What the move fields do

### The hit (`move_apply_hit`, confirmed from code)

```
base     = (attacker condition[atk_part] + 50) * power / 100
strength = (attacker header[5 + atk_part] - defender header[10 + target_region]) * power / 10
spirit   = power ? (attacker spirit - defender spirit) / 20 : 0
damage   = sum of the three, each clamped at 0
           x 1.2 when attacker flags_lo & 0x1000, x another constant (not read yet)
             when defender header.traits & 2
           x table 0x800E6D3C[per-slot setting at 0x800F1858]: entries 1-7 are
             0.5, 0.625, 0.75, 0.875, 1.0, 1.16, 1.33 (a handicap or difficulty, guess)
```

Then:

- the defender loses `damage` through `sub_800A05E0`, and `damage / 4` from `+0x00E`
  (floored at 128);
- each body region `r` (0–4) of the defender loses `dmg_head`, `dmg_body`, `dmg_arms`,
  `dmg_legs`, `dmg_region4` (`Wrestler +0x33C[r]`, the condition that triggers the injured
  idle/walk, see `animation_states.md`);
- if `target_region` is 0–3 and that region is now the defender's weakest,
  `g_injury_region` is set to it;
- spirit: attacker `+= spirit_atk` (+3 more in some states, + header byte 3 with
  `flags_lo & 0x40000000`), defender `+= spirit_def`;
- `meter_atk` / `meter_def` move a 30–200 meter per wrestler at `0x800F1CB0` (both are 0 in
  every World Tour move, so this is probably used by the later games).

Body part numbering, shared by `atk_part`, `target_region` and the damage fields: 0 head,
1 body, 2 arms, 3 legs, 4 whole body / none. Luger's slot 1 "elbow to neck" checks out:
`atk_part` 2 (arms), `target_region` 0 (head), `dmg_head` 2, power 6.

The wrestler header (move-set file 0) therefore holds attack stats per body part at bytes
5–9 and defence stats per region at bytes 10–14.

### Other fields

| Field | Values seen | Where it is read |
|-------|-------------|------------------|
| `power` | 0–40+ | damage multiplier above |
| `spirit_atk` / `spirit_def` | 0..15 / −15..+5 | `move_apply_hit`, also `state_18`, `state_69` |
| `hit_joint`, `hit_joint2`, `hit_joint3` | joint numbers, strikes only (e.g. slot 118: 16, 17, 18 = an arm) | `ai_learn_strike_reach` uses `hit_joint` |
| `aim_joint` | 1–4 (pelvis to neck), slots 154–182 | dive / ground-attack aiming (`sub_800BC888`, states 0x5A–0x5D) |
| `dive_pct` | 50/75/100/125, slots 166–182 | scales header byte 0x1A in the dive aiming |
| `dive_value` | 52–62 stored, signed 6-bit = −12 to −2, slots 154–182 | spirit lost by a diver who misses and lands lying (`state_5D` step 6, `sub_800BD484`) |
| `hit_reaction` | 1–5, strikes 118–130, 157 | receiver reaction switch in `state_24`, `sub_800CA9B0` |
| `reaction_variant` | 0–2, slots 122–126 | `state_24` plays animation 3309 + this |
| `next_slot` | move slot | chained move: `state_1E` and `sub_800A7300` start it after this one |
| `followup_190` | 1–31 | pins, holds and failed moves continue with slot 190 + this |
| `f07`, `f08` | 0–5, rare | `sub_8009EBF0` (timing from spirit tables), `sub_800CBC94` |
| `end_value` | rises with slot | copied to the receiver's `+0x03A` at the end of the move |
| `f18` | always 0 | no reader |
| `f19` | mostly 1 (strikes) or 3 (grapples) | no reader found |

### `flags` bits

| Bit | Moves with it | Read by | Meaning |
|-----|---------------|---------|---------|
| 0x1 | 1440 | `grapple_check_reversal`, `sub_800D9F68`, `sub_800DA118` | can be reversed (confirmed) |
| 0x2 | 49 | `state_35_hold_attacker` | hold behaviour (guess) |
| 0x4 | 712 | `state_32_pin_defender`, `state_36_hold_defender` | pin / hold escape rule (guess) |
| 0x8 | 7526 | not found | set on nearly everything |
| 0x10, 0x20 | 43, 42 | `state_5D`, `state_28` | dive outcome variants (guess) |
| 0x80 | 132 | `state_24` | strike reaction variant |
| 0x100, 0x200, 0x8000, 0x400000, 0x800000 | 854, 1943, 16, 78, 862 | `state_35/36` holds | submission hold rules (guess) |
| 0x1000, 0x2000 | 15, 6 | `grapple_choose_move`, `state_46/47`, `sub_800C9110` | move needs another place: in the ring the pair walks there first (0x2000 = to the ropes of that side, state 0x46), elsewhere slot + 5 is used |
| 0x4000 | 4464 | not found | |
| 0x20000, 0x80000 | 2, 4 | `state_46/47` | 0x20000: walk the receiver along at run speed; 0x80000: no arrival check at the ropes |
| 0x40000 | 2 | `sub_800C9110` | |
| 0x100000 | 2 | `sub_800D9CAC` | |
| 0x200000 | 96 | `state_3E/3F` with strong corner grapple | corner-only behaviour |

## Asset archive and file loader (main segment)

- `func_80004DDC(info*, file, dest)` loads file `file` (1–5498) to `dest`. It decompresses
  when needed: it allocates a buffer, DMAs into it, decompresses with `func_80004E8C`, then
  frees it.
- `func_80004D24` reads two tables, using pointers held at `0x80047900` (offsets) and
  `0x80047904` (decompressed sizes):
  - offset table entry `n-1` = ROM offset − `0x39490`, with **bit 0 = compressed**. Entry `n`
    gives the end. At runtime the table sits at `0x803CE780`; the same bytes are at ROM
    `0x7C1A78`.
  - size table entry `n-1` = decompressed size.
- Roster 0's files: 430 at ROM `0xC9174` (0x20 bytes, stored raw), 431 at `0xC9194`
  (compressed, 0x14D0 bytes unpacked), 432 at `0xC99A2` (compressed, 0x4C4), 433 at
  `0xC9CBC` (compressed, 0x60).
- The compression is LZSS, decoded in `../../file_formats/asset_archive.md`.
  `tools/rom/aki_archive.py` extracts any file; its output for files 431–433 matches RAM.

## Next steps

1. ~~Decode `func_80004E8C` and write a ROM file extractor.~~ Done; see `asset_archive.md`.
2. Dump all roster move sets and name the record fields by comparing moves of known types.
3. Work out what `func_8009DF20` does with the record (damage, spirit, receiver state).
4. Decode files 430 and 433 and the shared file 221.

## Strike counters (2026-10-30)

Slots 136–139 are the wrestler's own counters to a caught tap strike (136/137 close, 138/139
far; the odd slot when `chance_roll` table 11 wins), and 140–144 are counters to strong and
running strikes 123–126 and 128, taken from the striker's move set. See `strikes.md`.
