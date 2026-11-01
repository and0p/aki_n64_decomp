# World Tour: ring and arena geometry

First pass (2026-10-30), from the match setup code (`arena_build` = `0x80091100`,
`arena_crowd_build` = `0x800917B0`, `0x80091B80`, `ropes_build` = `0x80010B18`) and the
mesh files. The previewer draws it (`arena` option, built by `arena_pieces` in
`tools/rom/wcw_models.py`): the ring, posts, barriers, floor and crowd come out right for all
four arenas, and the ropes at rest; the 5 crowd copies are not placed yet.

## How it is put together

- The arena is **static meshes in world coordinates**, in the same mesh-part format as the
  wrestlers (`file_formats/wrestler_models.md`), mostly 4-vertex quads with `scale + 1` = 10
  (so file coordinates are tenths of world units). Each piece has a CI4/CI8 texture and a
  palette; for arena objects the texture file is always the palette file **+ 1**.
- `scenery_object` (`0x80008774`) loads a list of mesh files with their palettes and builds one
  display list that draws them all with one matrix. Several objects are then placed with node
  matrices (per ring side, rotated by 90°).
- There are **4 arenas** (`g_arena`, `0x800EB814`), one per federation, confirmed by the
  rendered apron banners: **0 WCW, 1 nWo, 2 Dead or Alive, 3 Independent Union** (wood
  floor). The ring and walls are the same
  meshes everywhere; the arena picks the texture sets and its own crowd / entrance meshes.
- When more than two wrestlers are in the match (`0x800EB812`), the crowd is built from a
  much shorter list (3 pieces instead of 11–17): a lower-detail arena for tag and 4-way
  matches.

## The ring (`arena_build`)

| Mesh | Extent (world units) | What it is |
|------|----------------------|------------|
| 995 | x, z ±280, y 0–90 | ring sides (the skirt below the apron) |
| 996 | x, z ±280, y 90 | the apron surface |
| 997 | x, z ±240, y 90 | the canvas inside the ropes |
| 998, 999 | x −80…0 and 0…80, z ±80, y 90 | the centre logo, two halves |
| 1011 or 1012 | around (212–242, 212–242), y 10–90 (+125) | the **turnbuckle pads**: three wedges, one per rope, coloured by the side palette (1012, a plain box, is the alternative when the per-arena flag is clear) |
| 1013 | (237–260, 237–260), y 125–225 (+0) | a diagonal plate behind the pads, where the ropes meet |
| 1014 | (257–278, 257–278), y 0–150 (+90) | the **ring post**, from the mat to 240 |
| 985 | x ±500 at z = −500, y 0–90 | the **barrier** wall, one side |
| 1000 | x ±500, z 280…500, y 0 | the **floor** between ring and barrier, one side |
| 991 | x, z ±240, y 0 | an extra floor piece, only in the 2-wrestler build |

The textures come from per-arena tables: ring set at `0x800E62C4 + 132` (5 palette numbers per
arena), side pieces at `0x800E629C` / `0x800E62A4`, side palettes at `0x800E62AC` (different
per side, so each side's apron can have its own colours), barrier and floor at `0x800E6360`.

The corner and side pieces (1011–1014, 985, 1000) are drawn once per side, rotated about the
centre by 90°, which gives the 4 corners, 4 barrier walls and 4 floor strips. The three corner
pieces are also **lifted** by the bytes at `0x800E6284` (125, 0, 90, the numbers in brackets
above): the pad file is built around y 10–90 and lifted to 135–215, level with the ropes at
140/175/210, and the post is lifted from 0–150 to 90–240.

## Ropes (`ropes_build`, `0x80010B18`)

The ropes are **not static meshes**; the game builds them at match start and reshapes them
every frame. The previewer draws them at rest.

### Shape (`rope_vertices_init` `0x8000F790`, `rope_shape` `0x8000F8A4`)

Per ring side, in that side's frame (the side node is placed 230 units from the centre, the
same 230 the in-ring collision uses, and rotated 90° per side):

- **7 stations** along the side at x = −224, −150, −76, −2, 72, 146, 220 (every 74).
- **3 ropes** at heights **140, 175, 210** (`0x80032B9C`; the mat is at 90, so 50, 85 and 120
  above it).
- Each rope's cross-section is **4 vertices forming two crossed ribbons** 6 units wide:
  (y ± 3, z) and (y, z ± 3) (`0x80032B88` / `0x80032B8C`). So 7 × 3 × 4 = 84 vertices per side.
- Vertex shades 100, 0, 50, 50 (`0x80032B94`) give the ribbons a light-to-dark gradient. Texture
  t is 16 or 31 (`0x80032B90`) and s runs along the rope with a random 0–7 jitter per vertex; the
  rope texture itself has not been identified.
- The display list (`rope_dl`, `0x8000FBF0`) draws 4 triangles per rope per segment (both
  ribbons), and each of the 6 segment nodes (centres from −187 every 74) loads its two stations'
  24 vertices.
- `0x80032B98` = {0, 3, 2, 1} maps ring sides to rope records.

### Deflection

Each side has 3 rope records (40 bytes, `0x80058600 + side * 120 + rope * 40`):

| Offset | Meaning |
|--------|---------|
| +0x00 | s16 kind (the `ring_rope_push` case; 0 = at rest) |
| +0x02 | s16 frames left on the current swing |
| +0x04 | f32 position along the side where it is pushed |
| +0x08 / +0x0C | f32 current offset, vertical / outward |
| +0x14 / +0x18 | f32 target offset of the current swing |
| +0x20 / +0x24 | f32 per-frame step toward the target |

`rope_shape` bends each rope as a **tent**: the offset is full at the push position and falls
linearly to 0 at both ends (±224). `ropes_tick` (`0x80010C6C`) moves the offsets toward their
target each frame; when a swing ends the target flips sign and shrinks to **60 %** (70 % for the
outward axis of kind 7) and the next swing takes 6 frames (13 for kind 7), until the target is
below 0.1 and the rope comes to rest. So a push is a damped back-and-forth wobble.

## Crowd and entrance (`arena_crowd_build`)

Each arena has its own list (`0x800E62C4 + 176`, counts at `+172`: 17, 15, 15, 11). The
pieces are all on the **−Z side**, behind the barrier, and step up and back:

| Example meshes (arena 0) | Extent | Reading |
|--------------------------|--------|---------|
| 832, 833 | x ±500, z −670, y 0–140 | first crowd row, right behind the barrier |
| 838, 839 | x ±520, z −680…−940, y 60–210 | sloped stand |
| 844, 845 | x ±690, z −1010…−1270, y 100–240 | upper stand |
| 986 / 989, 987 / 990 | x ±500…±1390, z −590…−2040, y 0–90 | the angled walls along the **entrance aisle** |
| 889, 890 | x ±1080, z −1910, y 0–410 | the stage front |
| 848, 849 | x ±1080, z −2050, y 330–680 | the backdrop / banner above the stage |
| 886 | x ±160, z −1870, y 280–600 | a screen over the entrance |
| 879 | (1290–1710, 1290–1710), y 0–540 | a corner tower (arena 0 only) |

The list is built twice with two palette sets (`0x800E6384` and `0x800E6394`), which looks
like two frames of crowd animation, and placed in 5 instances. Whether the other sides reuse
the crowd rows (rotated) is not checked.

`0x80091B80` builds four more objects from `0x800E6400` (meshes 1031–1046 with palettes
1032–1050); not identified yet.

## Collision

Collision does not use these meshes. It is the box table `g_zone_bounds` from `movement.md`:

| Visual | Collision |
|--------|-----------|
| canvas ±240, ropes at 230 | in-ring box ±230 |
| ring block ±280 | floor zones start at 280 |
| apron surface | apron lines at 265 |
| barrier at 500 | floor zones end at 497 |

The entrance aisle (z −590 to −2040) is outside the floor boxes, so wrestlers cannot walk up
it in a match.

## Open questions

- The rope texture (still open; it is a graphics question).
- ~~The push kinds 1–11 of `ring_rope_push`~~ Read 2026-11-01. Each side has three ropes at
  `g_rope_state` + side × 120 + 0 / 40 / 80; each push is (outward, vertical) offsets over a
  number of frames:

  | Kind | Ropes moved | Offsets | Frames |
  |------|-------------|---------|--------|
  | 1 | +80 | (0, −10) | 6 |
  | 2 | +80 and +40 | (0, +10) / (0, −10), spread apart | 12 |
  | 3 | +0 | (0, +10) | 12 |
  | 4 | +80 / +40 | (0, ±15) | 10 |
  | 5 | +40 / +0 | (0, ±15) | 10 |
  | 6 | all three | out 60 / 40 / 20 | 12 (a wrestler bounced off them) |
  | 7 | all three | out 65 / 40 / 20, top −3 down | 7 (harder rebound) |
  | 8 | all three | (0, +10) | 6 |
  | 9 | all three on this side at the −220 end and on the previous side at the +220 end | (10, −5) | 4 (into the corner) |
  | 10 | every rope on all four sides | (0, +6) | 4 (the whole ring shakes) |
  | 11 | this side, all three | reset to rest | 0 |
- The 5 crowd instances' transforms, the two palette sets, and `0x80091B80`'s objects.
- File 994 (40 bytes, first entry of every crowd list) and the per-arena flag at `0x800E6288`.
