# WCW/nWo Revenge: move sets, roster and inputs

Revenge's move data, laid out like World Tour's `moves.md`. Found 2026-11-01 from the ROM, the
emulator (`akiemu --game revenge`) and the move lists of the SayainPrince GameFAQs guide
("Move List and Guide", v1.0; the guide text itself is not stored here). Controls and unlocks:
`controls.md`.

## Files and RAM (`0x800B7BC4`, called from the match init `0x800B7A30`)

| What | File | RAM | Size |
|------|------|-----|------|
| Global moves (generic slots, `wrestler_set_move_from`) | 294 | `0x8007BD78` | 1440 = 60 x 24 |
| Reversal tables, one per match slot | 295-298 | `0x80064AE0` + 0x190 x slot | 400 |
| Per-wrestler header | base + 0 | `0x800705A0` + 0x40 x slot | 64 |
| Per-wrestler move records | base + 1 | `0x80075478` + 6704 x slot | **279 x 24**, with no 8-byte prefix (World Tour: 8 + 222 x 24) |
| Per-wrestler animation lists | base + 2 | `0x8007C5B8` + 1536 x slot | up to 1536 (s16 ids) |

The 24-byte record is unpacked exactly as in World Tour (`move_record_unpack`, `0x800A7D64`):
global move id `W0 & 0x7FF`, power, spirit, joints, damage and so on.

## Roster

The match roster is four u32 at `0x8007E8B0` (copied to u16 at `0x800FACEA`), one per match
slot: `group << 8 | (index + 1)`, as in World Tour. The base file is
`s16 ((s16*) word[0x80038A64 + group*4])[index]` (main segment). The internal groups are not the
select-screen groups. The words were read in the emulator by picking each wrestler on the select
screen (it writes `0x8007E8B0` on the pick):

| Wrestler | Roster word | Base file |
|----------|-------------|-----------|
| **nWo White** | | |
| Hollywood Hogan | 0x201 | 383 |
| Giant | 0x002 | 359 |
| Brian Adams | 0x20C | 326 |
| Scott Hall | 0x204 | 371 |
| Scott Norton | 0x209 | 452 |
| Buff Bagwell | 0x205 | 302 |
| Eric Bischoff | 0x20D | 314 |
| Scott Steiner | 0x206 | 494 |
| **nWo Red** | | |
| Kevin Nash | 0x203 | 446 |
| Sting | 0x001 | 503 |
| Lex Luger | 0x003 | 425 |
| Macho Man Randy Savage | 0x202 | 491 |
| Konnan | 0x208 | 416 |
| **Raven's Flock** | | |
| Raven | 0x501 | 470 |
| Lodi | 0x507 | 422 |
| Riggs | 0x504 | 479 |
| Sick Boy | 0x506 | 497 |
| Reese | 0x508 | 473 |
| **WCW 1** | | |
| Diamond Dallas Page | 0x004 | 335 |
| Goldberg | 0x00A | 365 |
| Bret Hart | 0x008 | 323 |
| Chris Benoit | 0x009 | 311 |
| Rick Steiner | 0x005 | 476 |
| Fit Finley | 0x101 | 353 |
| Booker T | 0x00B | 320 |
| **WCW 2** | | |
| Saturn | 0x502 | 488 |
| Disco Inferno | 0x00C | 341 |
| Jim Neidhart | 0x10E | 449 |
| British Bulldog | 0x10B | 329 |
| Glacier | 0x401 | 362 |
| Van Hammer | 0x505 | 374 |
| **WCW 3** | | |
| Yugi Nagata | 0x10D | 443 |
| Larry Zbysko | 0x107 | 524 |
| La Parka | 0x308 | 419 |
| Stevie Ray | 0x108 | 500 |
| **WCW 4** | | |
| Chris Jericho | 0x303 | 389 |
| Eddy Guerrero | 0x302 | 347 |
| Psychosis | 0x309 | 467 |
| Rey Mysterio Jr. | 0x304 | 440 |
| Dean Malenko | 0x306 | 428 |
| Juventud Guerrera | 0x305 | 398 |
| Ultimo Dragon | 0x301 | 344 |
| Chavo Guerrero Jr. | 0x307 | 332 |
| Alex Wright | 0x10F | 299 |
| **EWF** | | |
| AKI Man | 0x601 | 434 |
| Shogun | 0x602 | 401 |
| Executioner | 0x603 | 413 |
| Dr. Frank | 0x604 | 395 |
| Jekel | 0x605 | 515 |
| Maya Inca Boy | 0x606 | 506 |
| **DAW** | | |
| Hawk Hana | 0x701 | 509 |
| Kim Chee | 0x702 | 410 |
| Dake Ken | 0x703 | 458 |
| Brickowski | 0x704 | 356 |
| Ming Chee | 0x705 | 368 |
| HANZO | 0x706 | 377 |

| **Hidden** (unlocked with the GameShark code `8107F07C FFFF`) | | |
| Curt Hennig | 0x207 | 380 |
| Billy Kidman | 0x503 | 404 |
| Roddy Piper | 0x007 | 464 |
| Meng | 0x105 | 431 |
| Kanyon | 0x404 | 437 |
| Barbarian | 0x106 | 308 |
| Wrath | 0x403 | 521 |

The hidden ones were picked from `_tools/libretro/states/rv_select_unlocked.state` (the select
screen with everyone unlocked: `0x8007F07C` = 0xFFFF, a 16-bit unlock mask; Hennig appears at
the end of nWo White, Barbarian and Wrath in WCW 3). THQ Man (C-Down on AKI Man) still read
0x601 like AKI Man, so the swap is probably a costume or flag rather than a roster entry; not
checked further. Alternate entries and managers in the name table (`0x8003A1EC`, select order)
were not picked.

## Move slots and inputs

Fitted as in World Tour: for each guide input, the slot where wrestlers whose guide entries
share a move name also share a global move id (`same`), and those with different names differ
(`diff`). The front grapples sit in the same slots as World Tour's (checked at those slots):

| Input | Slots | same / diff |
|-------|-------|-------------|
| Weak front grapple: A, Up+A, Down+A, B, Up+B, Down+B | 0, 1, 2, 3, 8, 13 | 0.48 / 0.97 (names vary), 0.85 / 0.95, 0.80 / 0.84, 1.00 / 0.93, 0.98 / 0.96, 0.70 / 0.97 |
| Strong front grapple: A, Up+A, Down+A, B, Up+B, Down+B | 18, 23, 28, 33, 38, 43 | 0.59 / 0.97, 0.92 / 0.98, 0.68 / 0.98, 0.72 / 1.00, 0.48 / 1.00, 0.22 / 0.99 |

As in World Tour, each grapple move is followed by its 4 reversal variants (`slot + 1..4`), which
often share the move id, so a fit can land on `slot + 1`. The other inputs, best slot (marked
`weak` when either score is under 0.75; treat those as unconfirmed):

| Input | Slot | same / diff | |
|-------|------|-------------|---|
| Opponent Running (After Strong Irish Whip) / A (Hold) | 9 | 0.63 / 0.66 | weak |
| Back Weak Grapple / B | 62 | 0.73 / 0.96 | weak |
| Back Strong Grapple / A | 63 | 0.91 / 0.97 |  |
| Back Strong Grapple / B | 64 | 0.61 / 0.97 | weak |
| Standing Attacks / B (Hold) | 68 | 0.56 / 0.56 | weak |
| Opponent Running (After Weak Irish Whip) / A (Hold) | 76 | 0.92 / 0.72 | weak |
| Opponent Running (After Strong Irish Whip) / A | 80 | 0.84 / 0.85 |  |
| Opponent Running (After Weak Irish Whip) / B | 88 | 0.69 / 0.59 | weak |
| Opponent In Corner Weak Grapple / B | 95 | 0.83 / 0.93 |  |
| Back Weak Grapple / A | 100 | 0.61 / 0.64 | weak |
| Opponent In Corner Strong Grapple / A | 100 | 0.95 / 0.90 |  |
| Opponent Running (After Weak Irish Whip) / A | 100 | 0.70 / 0.62 | weak |
| Opponent In Corner Strong Grapple / B | 105 | 0.95 / 0.93 |  |
| Standing Attacks / B (Close) | 106 | 0.61 / 0.60 | weak |
| Opponent Face Up / B (Standing) | 149 | 0.70 / 0.70 | weak |
| Opponent Face Down / B (Standing) | 151 | 0.59 / 0.73 | weak |
| Opponent In Corner Weak Grapple / A | 151 | 0.58 / 0.79 | weak |
| Standing Attacks / B + Forward | 151 | 0.64 / 0.59 | weak |
| Running / B | 162 | 0.90 / 0.88 |  |
| Opponent Running (After Strong Irish Whip) / B | 172 | 0.54 / 0.94 | weak |
| Standing Attacks / B (Away) | 174 | 0.82 / 0.75 |  |
| Opponent Face Up / A (Head) | 196 | 0.85 / 0.97 |  |
| Opponent Face Down / A (Head) | 198 | 0.92 / 0.89 |  |
| Opponent Face Up / A (Feet) | 200 | 0.77 / 0.95 |  |
| Opponent Face Down / A (Feet) | 202 | 0.84 / 0.79 |  |
| Running / B + Forward | 223 | 0.59 / 0.67 | weak |
| Opponent Face Down / B (Running) | 239 | 0.81 / 0.85 |  |
| Opponent Face Up / B (Running) | 239 | 0.81 / 0.87 |  |

The low scores mostly come from the guide naming the same move differently between wrestlers.
The code's own slot tables (World Tour's `g_grapple_slots`) would settle them.
