# WCW/nWo Revenge: wrestler models, attires and colours

Part of the WCW/nWo Revenge notes (index: `README.md`). Written as differences from World Tour
(`../wcw_world_tour/`); anything not mentioned is not checked yet, not known to be the same.

## Wrestler models compared with World Tour

From the archive (both games scanned with World Tour's mesh rule) and Revenge's `model_load`
(`0x8000C91C`) and node builder (`0x8000BDD0`) next to World Tour's `model_load` /
`model_build_nodes`.

**Same formats and the same model structure:**

- The **mesh part format is unchanged** (8-byte header, 8-byte vertices, index triples; 666
  archive files match it exactly, World Tour 395), and so is the texture format (CI4 / CI8 with
  the 8-byte header, RGBA5551 palettes).
- A wrestler is still **23 part slots on the 19-joint skeleton**, built per part and drawn
  with the **same 7 seams** (pelvis, waist, neck, both thighs, both forearms). The part record
  grew from 40 to 44 bytes; the node records are the same 220 bytes (5,060 per wrestler).
- Body parts come per body type and bone, head parts from a per-wrestler head list (neck, head,
  optional extra on the head such as hair), as in World Tour. Revenge has **no foot extras**
  (World Tour's nodes 21/22).

**Not more detailed per part.** The meshes are about the same size or slightly smaller:

| | World Tour | Revenge |
|--|-----------|---------|
| mesh part files | 395 | 666 |
| average vertices / triangles per part (all) | 28.2 / 33.0 | 23.8 / 27.1 |
| body parts (one run) | 209 files, 34.0 / 39.5 | 278 files, 31.0 / 34.8 |
| largest part | 64 vertices, 86 triangles | 56 vertices, 75 triangles |

What grows is **variety**: 70 % more mesh parts (more body types and heads) and more textures
(e.g. 64×32 CI4: 177 vs 32; 64×64: 116 vs 63; the 128×128 CI4 textures at 677–713 are probably
arena pieces, not wrestlers). Attires such as a long coat are built from their own body-part
meshes (body type 36 for the first wrestler).

New in the loader: each wrestler gets two heap buffers (700 × 16 bytes and 1,200 × 8 bytes,
`sub_80007A74`), which look like room for built vertices and display-list commands; small fixed
offsets are applied to a few joints after building (shoulders ±5, ±7). Neither is followed yet.

### Tables and the previewer

`tools/rom/revenge_models.py ROM viewer _tools/viewer/revenge_viewer.html` builds the same
previewer as World Tour's (86 roster slots, first 8 attires each, all 4,017 animations; no
labels or arenas). Renders check out: the bodies assemble correctly and animation 3685 (World
Tour's 2919, byte-identical) plays the same pose.

| Table | Address | Contents |
|-------|---------|----------|
| roster descriptors | `0x80031998` | ptr per group (10 groups) → u16 per wrestler; low 7 bits = **body build** |
| head lists | `0x80032B44` | ptr per group → ptr per wrestler → 0-ended head variants; each 12 u16: neck / head / extra mesh, their palette and texture, face palette / texture, [11] texture-record index |
| attires per build | `0x800360F0` | ptr per build → ptr per attire → texture records (picked by head [11]); the first record's [0] & 0x7F = body type |
| attire count per build | `0x8003673C` | u16 per build (24, 63, 33, 20, 7, 1 …) |
| body parts | `0x800317F0` | ptr per body type → 17 u16 mesh files |
| bone → part | `0x80031089` | as World Tour's `g_bone_part` |
| skeleton | `0x800307C0` (bind), `0x800307E9` (order), `0x800307FC` (parents) | identical data to World Tour's |

**Attires are shared per body build**, not per wrestler as in World Tour: build 1 has 63
attires that every wrestler of that build can wear, which matches Revenge letting you pick
outfits. The per-wrestler default is not found yet (the previewer shows the first 8).

**New texture format 0x0020**: CI4 pixels with a **32-colour palette** (two 16-colour banks; the
second looks like a darker copy of the first). 357 textures use it, many of them body parts.
The previewer uses bank 0; when the game uses bank 1 is not known (lighting or a dirty /
bloodied version?). World Tour has only 0x0010 (CI4, 16 colours) and 0x0100 (CI8).

### Attires and colours (2026-11-01, from code; Andrew's viewer notes)

Two things the first previewer got wrong:

**Which costumes a wrestler wears.** The costume list of a body build (`0x800360F0`, up to 63
for build 1) is shared by everyone with that build. Each wrestler's four attires are defaults
in `0x80036710` (pointer per roster group, then per wrestler, then 4 x 4 bytes), unpacked by
`sub_8000B630`: byte 0 = head variant + 1, byte 1 = **costume number + 1** in the build's list,
bytes 3 / 2 = **colour 1 / colour 2** + 1 (0 = keep the texture's own colours). Examples: Sting
(0x001) wears costumes 0, 0, 1, 2; Giant (0x002) 0, 19, 0, 0; DDP (0x004) 6, 7, 6, 8; Hogan
(0x201) 19, 19, 21, 20. At boot `sub_8002BAB8` copies these into a 42-byte-per-attire RAM table
(`0x8003A594`, 80 profiles x 4 attires, with the names); the costume editor edits that copy and
`match_setup` reads attire bytes +36 (head), +37 (costume), +39 / +40 (colours) from it
(`sub_8002B7E0`) for `model_load`. Attires 1 and 3 of Kanyon (0x404) use the name "Mortis" and of
AKI Man (0x601) "THQ man", which is why THQ Man reads as roster 0x601.

**Why skin (and other parts) are red.** The 32-entry palettes (format 0x0020) are 16 colours
plus 16 **recolour keys**; this is the "second bank". `sub_8000B884` rewrites each colour whose key
is set:

- key bits only in 0x07C1 (green field): colour 1 — shade `(key & 0x7C1) >> 6 >> 1` of row
  `colour 1` in the ramp file **2641** (512 bytes = 16 colours x 16 shades, RGBA5551), counted from
  the top (`16 x colour + 15 - shade`); colour 15 or more keeps the original.
- key bits only in 0x003F (blue field): colour 2, the same way.
- **skin**: for bones with a skin file (`0x8003086C[bone]`, files 2642–2657, 96 bytes = 3 tones x
  16), every palette *colour* that is itself a pure red value (only 0xF801 bits) is a skin
  placeholder and becomes shade `(c & 0xF801) >> 11 >> 1` of tone `head[11]` (head record
  +0x16, which also picks the costume's texture record). Bare-skin parts have 16-colour palettes
  that are entirely such placeholders.

The previewer (`tools/rom/revenge_models.py`) now does all of this (2026-11-01): each wrestler
shows its 4 default attires with their head, skin tone and colours, and is named from the
select-screen lists (`0x8003A4F8`: 11 groups of records with the roster word at +0 and the
name string pointer at +8).

So the red skin in the viewer is the untouched placeholder colours. The editor's two colour
pickers are colour 1 / colour 2.
