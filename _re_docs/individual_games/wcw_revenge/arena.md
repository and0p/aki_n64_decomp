# WCW/nWo Revenge: arenas

Part of the WCW/nWo Revenge notes (index: `README.md`). Written as differences from World Tour
(`../wcw_world_tour/`); anything not mentioned is not checked yet, not known to be the same.

## Arenas

From `arena_build` (`0x8000E920`, called with the arena number, 0–5) and the crowd builder
`0x8000EF90`, both in main, with World Tour's `arena.md` for comparison. Shown in the Revenge
previewer (arena menu); the names come from the renders.

| # | Arena | Mat logo, entrance |
|---|-------|--------------------|
| 0 | Nitro | Nitro apron, stage with the Nitro towers |
| 1 | Souled Out | Souled Out logo, banner stage with stairs |
| 2 | SuperBrawl | red carpet ramp under a lit arch |
| 3 | Bash at the Beach | Bash logo, beach stage with a sky backdrop |
| 4 | Halloween Havoc | Havoc logo, skull and haunted-house stage |
| 5 | Starrcade | Starrcade logo, star arch |

Compared with World Tour (4 arenas by federation, WCW / nWo / DOA / IU):

- **6 arenas, all WCW events.** Same building method: static meshes in world space loaded by a
  `scenery_object`-style loader (`0x80007020`: palette p, texture p + 1), side pieces repeated
  per ring side.
- **Ring:** 6 meshes (`0x80036C68` [0–5]): apron + canvas in one mesh (758), the centre logo as
  one piece **10 units above the mat** (847, World Tour two halves level with it), and the four
  skirts as separate meshes (760–763) instead of one. Palettes per arena from the pointer at
  [6 + 2·arena]; arenas without a logo have palette 0 for it.
- **Corners:** post, plate and pads (749, 752, 755) are modelled in place (y 90–240), so the
  per-piece lifts of World Tour are gone; the pads (755) have no texture (probably vertex
  colours or per-arena colours; not drawn by the previewer yet).
- **Floor and barrier:** floor strip 757 per side; the barrier is split into two halves (785,
  787) with a gap in the middle, filled by 771 on three sides and **left open on the entrance
  side** for the aisle (788, a ramp from the ring to the stage). In World Tour the aisle was
  walled off.
- **Crowd and entrance:** 24–29 pieces per arena (`0x80036C68` [38 + 2·arena] meshes, [50…] and
  [62…] two palette sets, [34] counts), on the **+Z side** (World Tour −Z): an arena-specific
  stage and banners (e.g. 864–872 for Nitro) plus shared crowd stands (793–839). A second list
  at `0x80036750` (19 + 3 meshes, two palette sets) looks like the stands for the other sides.
- `0x80011BB4` loads more objects from `0x80036D18` (11 lists of 1–7 meshes and a 6-piece
  sequence), not identified (entrance effects, ring steps?).
- The ropes are still generated in code, not meshes (the previewer draws World Tour's).
