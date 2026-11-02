# Wrestler models in No Mercy (2026-11-02)

How No Mercy turns a roster entry into the meshes and textures it draws. Everything here comes
from the main-segment code, checked by rendering; there are no RAM reads. The file formats are
World Tour's (`_re_docs/file_formats/wrestler_models.md`, `animations.md`), and so is the
skeleton. What is new is how the parts are chosen and how the textures are put together.

Tools (`tools/rom/nomercy_models.py ROM ...`; the pages hold ROM data, keep them under `_tools/`):

- `roster` lists each wrestler's four attires as appearance bytes;
- `assets` counts the meshes and textures in the archive;
- `preview OUT.html`: a bind-pose page of the first roster slots, for checking parts;
- `viewer OUT.html`: World Tour's previewer (`wcw_viewer.html`) with all 64 named wrestlers, 4
  attires each, and all 5,787 animations (default `_tools/viewer/nomercy_viewer.html`, 25 MB).
  `#w=<wrestler>&c=<attire>&a=<anim>&f=<frame>&cam=x,y,z,tx,ty,tz` sets the view.

The wrestlers come out recognisable, with outfits, pads, boots, tattoos, hair, facial hair,
masks, hats and item colours: The Rock, Austin (trunks, vest, knee pads, goatee, cap), Undertaker
(bandana, sunglasses, hair, coat), Kane and Mankind (masks), Tazz (hood), the Godfather (hat,
beard), X-Pac (DX colours), Trish (cowboy hat) and others. The animations play on them.

## Every wrestler is a created wrestler

World Tour and Revenge pick a costume record: one mesh, texture and palette per body part.
No Mercy stores each attire as a **63-byte appearance**, the data the create-a-wrestler editor
edits: 21 triplets of (item, colour 1, colour 2). At match start `appearance_build` expands it
into a **624-byte model record**: a mesh and a texture for each of 24 part slots, plus lists of
**texture layers**. `model_build_textures` then stamps the layers into the slots' textures:
tattoos, logos, face details, pads and outfit details are small textures drawn over the skin,
not baked into one texture per costume. `model_nodes_build` turns the record into render nodes.

## From roster entry to appearance

| Step | Code | Data |
|------|------|------|
| roster word → wrestler index 0-83 | `roster_wrestler_index` | `0x80047124`: 84 × 4 attire pointers (16 bytes per wrestler); 0x0Bxx roster words are the created-wrestler slots (84 and up) |
| attire descriptor | (pointer from the table) | 20 bytes: +0 roster word, +4 / +5 / +6 bytes, +8 u16, +10 / +12 file numbers (184- and 32-byte profile blocks), **+14 attire record number**, +16 name index |
| packed attire | archive **file 1** | 260 records × 38 bytes; `attire_record_unpack` scatters them into the 63-byte appearance (map in `nomercy_models.py`) and `appearance_pack` packs that into 23 bytes |
| names | archive **file 2** | u16 offsets, then strings: full name, short name, alias (+0 / +1 / +2) |
| boot copy | `attires_load_all` → `attire_load` | 84 × 4 × 78-byte attires (heap pointer `0x80047E90`); packed appearance at +49, names at +0 / +21 / +32 |
| profiles | `profile_fill` (from ovl_a's roster set-up) | 101 × 276-byte profiles at `0x80097FE0`; the packed appearance is the last 23 bytes (+252) |
| match start | `models_init` → `profile_appearance` → `appearance_unpack` (23 → 63 bytes) → `wrestler_model_build` → **`appearance_build`** | model record per slot at `0x800A8980 + 624 × slot` |
| non-wrestlers | `appearance_fixed` | fixed appearances for kinds 1-3 (referees and others) |
| loading | `model_load_parts` → `model_build_textures` | per-slot model buffer at `0x800A4050 + 1456 × slot` (texture slots of 52 bytes) |
| nodes | `model_nodes_build` → `node_setup_part` (`mesh_part_load`), `mesh_split_seam` | 220-byte render nodes at `0x80091CC0 + 6,160 × slot` |

## Part slots, skeleton and seams

The skeleton is World Tour's 19-joint data, byte for byte (`0x80047ED0`: bind pose, then the
joint order at +0x29 and the parents at +0x3C), so World Tour's animation format and decoder
apply. The model has 24 part slots:

- 1–19 follow the bones as in World Tour: 1 pelvis, 2 waist, 3 chest, 4 neck, 5 head,
  6 / 7 / 8 right shin, thigh and foot, 9–12 right hand, wrist, forearm and upper arm,
  13–19 the left leg and arm.
- 20 is the face, 21 an extra head piece (hood, hat, mask shell); both follow the head.
- **22–24 are the other halves of the torso**: the pelvis is split into −x (1) and +x (22)
  halves, the waist and chest into front (2, 3) and back (23, 24). They follow bones 1–3. That
  gives the torso twice the texture area.

Seams (`model_nodes_build`, fixed calls to `mesh_split_seam`): a part's first `header[3] >> 4`
vertices follow the first slot listed, the next `header[3] & 15` the second.

| Part slot | First group | Second group |
|-----------|-------------|--------------|
| 1 pelvis −x | 14 (left thigh) | – |
| 22 pelvis +x | 14 | 7 (right thigh) |
| 2 / 23 waist front / back | 3 (chest) | 1 (pelvis) |
| 3 chest | 5 (head) | – |
| 5 head | 3 (chest) | – |
| 6 right shin | 7 (thigh) | 8 (foot) |
| 13 left shin | 14 | 15 |
| 11 right forearm | 12 (upper arm) | 9 (wrist) |
| 16 left forearm | 19 | 17 |

Unlike World Tour, the knee seam sits on the shin instead of the thigh, the neck has none, and
the head and chest are joined directly. A one-sided seam (count 0 in one group) is normal here.
`model_nodes_build` then nudges some joints (shoulders ±5, arms ±7, as in Revenge); the
previewer does not yet.

## Base body and head

- Base meshes: `0x80048A78[build]`, 20 meshes for the slots in `0x8004FD28` (1, 22, 2, 23, 3,
  24, 6–19). The build is `0x80048A48[body type]` (s16): builds 0–5 male, 6 and up female.
- Base textures: `0x80048AD4[0x80048AA4[body type]]`, 20 palette files (texture = palette + 1),
  **CI8** (format 0x0100) with 256-colour palettes: the skin.
- Neck: mesh 5375, texture 8042. Head: texture 8044 (female 8046). Both use skin palette 8043
  (female 8045).
- `[byte 12][byte 18]` in `0x8004913C` (female `0x80049148`, styles from 22) gives the head mesh
  (slot 5) and the face mesh (slot 20). Byte 15 picks the face texture (`0x8004D254`, CI8).
- Byte 1 is the **skin tone** (0–7). It picks the palette variant of skin-coloured layers
  (tattoos, face details): palette file + 1 (tones 0–3), + 2 (4–5) or + 5 (6–7).

## Items

Triplets at 3k are (item, colour 1, colour 2). Items with meshes replace the base mesh in their
slots; items with layer lists add layers. All the tables are in main data. Slot lists are
0-ended bytes; a mesh or layer list has one u16 per entry of its slot list (0 = leave it).

| Byte | Item | Meshes | Layers (slots, palette band) |
|------|------|--------|------------------------------|
| 0 | body type | base body | base textures |
| 1 | skin tone (0-7) | | layer palette variant |
| 3 | lower / full-body outfit (trunks, pants, suits; **item 0 is plain trunks**) | `0x8004CF64[0x8004CF98[item]][build]`, slots `0x8004FD40` | `0x8004D018[item]`, `0x8004FD50`, band 64 |
| 6 | upper body (shirts, vests, jackets; 0 = none) | `0x8004EB60[0x8004EB98[item]][build]`, slots `0x8004FD64` | `0x8004EC18[item]`, `0x8004FD70`, band 96 |
| 9 | head wear (bandanas, caps, masks) | `0x8004F824[0x8004F85C[item]][build]`, slots `0x8004FD7C` (incl. 21) | `0x8004F89C[item]`, `0x8004FD7C`, band 224; its slot 21 entry is slot 21's texture |
| 12 / 18 | head shape / hair style (+ colours) | head and face meshes | byte 18 < 32: hair layers `0x8004918C[item]` on `0x8004FDD8` (head, face, chest, back), band 128, palette `0x8004912C[byte 19]` |
| 15 | face | face texture | |
| 21 | facial hair (beard, goatee) | | `0x800490AC[item]` on the face, band 128, in the hair colour (byte 19) |
| 24 | face detail (1-31) | | `0x8004D528[item]` on the face, band 96, skin-tone palette, shifted down by `0x8004D354[face] − 0x8004D568[item]` rows |
| 27 | (1-31) | | `0x8004EF84[item]`, `0x8004FDE0` (head, face, chest), band 160 |
| 30 | arm wear (1-7) | `0x8004D4E8[item][0x8004D4DC[build]]`, slots `0x8004FDA4` | `0x8004D508[item]`, `0x8004FDAC` (both arms), band 192 |
| 33 / 36 | upper-arm items (1-7) | | `0x8004D234[item]`, `0x8004FDB4` / `0x8004FDB8`, band 160 |
| 39 | forearm item (1-7) | | `0x8004FCDC[item]`, drawn on both forearms (`0x8004FDC0`, `0x8004FDBC`), band 128 |
| 42 / 45 | knee pads (1-15) | | `0x8004EE4C[item]`, `0x8004FDC4` / `0x8004FDC8`, band 160 |
| 48 | boots (< 64) | `0x8004F278[0x8004F288[item]][0x8004F26C[build]]`, slots `0x8004FDCC` (feet) | `0x8004F2C8[item]`, `0x8004FDD0` (shins, feet), band 128 |
| 51 | tattoos / body paint | | `0x8004FC48[item]` on `0x8004FD94`, band 32 (texture = file, palette = file + skin-tone step) |
| 54 | alternative head (1-9) | when head wear sets flag 0x40 (`0x8004FFD0[byte 9]`) | see below |
| 57, 60 | | not read by the builder | byte 60 differs for every wrestler (Rock 0, Austin 1, Taker 2, Kane 3, …) |

**Alternative heads** (`appearance_alt_head`): hats, hoods and full masks. The index is byte 54 −
1 (when the head wear sets flag 0x40) or, for some head wear, its layer list's s16 at +34 minus
65 (72 and up). `[0x8004A07C[0x8004A040[index]]][head column][style row]` gives the meshes of
slots 21, 5 and 20 (head columns `0x8004A04C[byte 12]` and rows `0x8004A058[byte 18]`, female
`0x8004A045` / `0x8004A05A`), and `0x8004A0A0[index]` a layer list on slots 21, 5, 20 (band 32,
drawn last, colours bytes 55 / 56).

## Texture layers and colours

Each part slot has one CI8 texture (the skin) and a 256-colour palette. A layer is a CI4 texture
of the same width (height up to the slot's) with a 32-colour palette (format 0x0020: palette
file N, texture N + 1). `texture_stamp_layer` (`sub_8000CAF8`):

- every pixel whose layer colour is not 0x0000 (transparent) overwrites the slot's index with
  **nibble + band** (optionally shifted down some rows);
- the layer's 32 colours are copied into the slot palette at the band;
- `band_recolour` then recolours the band with the item's two colours. The upper 16 entries of
  the band are keys: an entry whose key lies only in 0x7C1 becomes shade
  `15 − ((key & 0x7C1) >> 6) / 2` of colour 1's ramp, one in 0x3F the same with `>> 1` and colour 2.
  The ramps are archive file **4341** (32 colours × 16 shades, RGBA5551). Colour 0 leaves the
  band alone; 32 and up give black. This is Revenge's scheme, applied per band.

The bands: the low entries hold the skin, then 32 tattoos and alternative heads, 64 lower
outfit, 96 upper outfit and face detail, 128 forearm item, boots, hair and facial hair, 160 pads,
arm items and byte 27, 192 arm wear, 224 head wear.

Order (`model_build_textures`): the face detail, tattoos, then 8 steps in the order of recipe
`0x8004FE78[6 × 0x8004FE58[upper kind] + 0x8004FE68[lower kind]]` (12 recipes of 8 steps at
`0x8004FDF8`; steps 0–7 = bytes 3, 6, 39, 48, 27, 33 / 36, 42 / 45, 30), then facial hair and
hair and head wear (head wear first when `0x8004FFB0[byte 9]` is set), then the alternative head.
So which outfit covers which depends on the outfit kinds.

## The model record (624 bytes)

- +8 × slot (slots 1–24): u16 mesh, u16 texture, u16 palette, colour 1 / colour 2 bytes.
  Slots 25–27 (+200…): extra textures set by `appearance_colours`; what draws them is open.
- Layer lists, 4 bytes an entry (u16 file, colour 1, colour 2): +224 byte 3, +292 byte 6, +336
  byte 9 (its slot 21 entry at +400), +404 byte 30, +428 / +436 bytes 33 / 36, +444 / +452 bytes
  42 / 45, +460 byte 48, +476 byte 39, +480 byte 51, +536 byte 18, +556 byte 27, +576 the
  alternative head.
- +588 / +590 facial hair texture and palette; +592 / +594 the face detail; +596 flags (0x10
  male, 0x20, 0x40 alternative head, 0x80); +597 skin tone; +598 hair colour; +599 the layer
  recipe (| 0x80: head wear before hair); +600 a face override in mode 3; +602 palette words;
  +620 the face detail's row shift; +621 byte 27's row shift.

## The archive (meshes and textures)

`nomercy_models.py ROM assets` (World Tour's mesh rule and texture header):

| | World Tour | Revenge | No Mercy |
|--|--|--|--|
| mesh part files | 395 | 666 | **3,591** |
| average vertices / triangles | 28.2 / 33.0 | 23.8 / 27.1 | 23.8 / 26.3 |
| largest part | 64 / 86 | 56 / 75 | 87 / 85 |
| textures (CI4 16 / CI4 32-colour / CI8) | 0x0010 and 0x0100 only | 0x0020 new | 1,094 / **2,397** / 402 |
| animations | 2,598 | 4,017 | 5,787 (files 11737–17523) |

The parts are no more detailed than in Revenge; there are about 5× as many of them, because
every item exists per build.

## Open

- Appearance bytes 57 and 60 (not model data as far as the builder goes).
- The extra textures in record slots 25–27 and the joint nudges of `model_nodes_build`.
- Animation labels: Revenge's and World Tour's labelling from move sets and state code is not
  ported to No Mercy yet.
