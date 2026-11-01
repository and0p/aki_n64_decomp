# Wrestler models and textures (WCW vs. nWo: World Tour)

A wrestler is 21–22 small mesh parts, one per bone of the shared skeleton plus a few extras.
Each part has one CI4 texture with a 16-colour palette. The parts are picked by three
tables: a **body type** (which 19 body meshes), a **head variant** (head meshes and their
textures) and a **costume record** (texture and palette per part). Skeleton and posing are in
`animations.md`. Function and table names are in `symbols/wcw/models.toml`.

Tool: `tools/rom/wcw_models.py ROM roster` lists every wrestler's costumes and mesh files;
`viewer` builds the previewer described in `animations.md`.

Confirmed by rendering all 43 roster wrestlers (172 costumes) with the rules below; the
result matches the in-game screenshots of Lex Luger.

## Mesh part file (`mesh_load` = `func_80005D20`)

395 archive files match this layout exactly (size = 8 + 8·N + 3·T, padded to 4): runs in
722–890, 985–1043, 1052–1260 (body parts), and single files through 1261–1717 (heads, hair
and face parts between their textures and palettes).

```
u8   scale - 1          positions are multiplied by this + 1
u8   flag << 7 | N      N vertices (max 127); flag is set on every body part (meaning unknown)
u8   T                  triangles
u8   seam_a << 4 | seam_b
s8   ox, oy, oz         position offset, * 16
u8   s_hi << 4 | t_hi   texture coordinate offset, * 16 texels
N × { s8 x, y, z; u8 u, v; s8 nx, ny, nz }
T × { u8 i0, i1, i2 }   vertex indices
```

`mesh_build_vtx` turns each vertex into an N64 `Vtx`:

- position = `(x + ox·16) · (scale + 1)`, likewise y and z, in **model space of the bind
  pose** (for example the head part spans y 63–89 around the head joint at y 72);
- `s = 32·(u + s_hi·16) + 8` and the same for t (10.5 fixed point, so texel `u + s_hi·16 +
  0.25`);
- the three signed bytes are copied as a normal (the game lights the models);
- alpha 0xFF.

`mesh_build_tris` packs the index triples into `G_TRI2` (0xB1) commands, two triangles each,
with a trailing `G_TRI1`. Triangles index the part's own vertices.

### Seams (`mesh_split_seam` = `func_80006630`)

Seven parts bend across a joint. Their first `seam_a` vertices follow one neighbouring bone,
the next `seam_b` follow another, and the rest follow the part's own bone. The game does
this by loading those vertex ranges with the other node's matrix before drawing the
triangles (the "pass 1 loads vertices, pass 2 draws" scheme described in the No Mercy
recomp's `docs/gex-design.md`). The pairs are fixed in `model_build_nodes`:

| Part (bone) | First `seam_a` vertices | Next `seam_b` vertices |
|-------------|--------------------------|------------------------|
| 1 pelvis | 14 (−x hip) | 7 (+x hip) |
| 2 waist | 3 chest | 1 pelvis |
| 4 neck | 5 head | 3 chest |
| 7 +x thigh | 1 pelvis | 6 knee |
| 14 −x thigh | 1 pelvis | 13 knee |
| 11 +x forearm | 12 shoulder | 9 wrist |
| 16 −x forearm | 19 shoulder | 17 wrist |

Both counts must be non-zero for the split to happen.

## Textures (`model_load_textures`)

The 8-byte-header texture format from `asset_archive.md`:
`[w−1, h−1, fmt (u16), cms, cmt, log2 w, log2 h]`, then pixels. `fmt` 0x0010 is CI4 and 0x0100
CI8. `cms`/`cmt` look like the RDP clamp/mirror bits (1 = mirror, 2 = clamp); the previewer
treats them that way and the results look right, but this is not confirmed from the code.
Palettes are raw RGBA5551 (0x20 bytes = 16 colours). Body textures come from 2388–2900 and
palettes from 1719–2387; head textures and palettes sit next to their meshes in 1261–1717.

## Choosing parts: roster → costume → files

All pointers are main-segment addresses, so the tables can be read straight from the ROM.
A roster entry is `group << 8 | (index + 1)` (the same encoding as `match_load_movesets`).

1. `g_costume_tables[group][index]` (`0x80032B40`) is a 0-terminated list of costume
   pointers. Each costume is a u16 array:
   - `[0]`: low 7 bits = body type, bits 8–11 = head variant;
   - `[1 + 2k]`, `[2 + 2k]`: palette and texture for body part `k` (k = 0–16);
   - `[35]`, `[36]`: palette and texture for the two extra foot parts.
2. `g_body_parts[body type]` (`0x8002E500`) is 19 u16 mesh files: parts 0–16 for the bones,
   17 and 18 for the extras on bones 8 and 15 (they share texture `[36]`).
3. `g_head_tables[group][index][head variant]` (`0x8002F150`) is 11 u16:
   `[0]` neck mesh, `[1]` head mesh, `[2]` optional extra mesh on the head (hair, mask);
   `[3]/[4]` neck palette/texture, `[5]/[6]` head palette/texture, `[7]/[8]` extra
   palette/texture, `[9]/[10]` an extra palette/texture loaded with `tex_pal_load` when
   present (probably a swappable face; not traced).
4. `g_bone_part` (`0x8002DE31`) maps bone 1–19 to body part `k`: bones 1–3 → 0–2, bones 4
   and 5 → head table, bones 6–19 → 3–16.

Render nodes 1–19 draw the part of the same-numbered bone; node 20 is the head extra (bone
5), nodes 21 and 22 are the foot extras (bones 8 and 15).

Lex Luger, costume 1 (roster `0x101`): body type 0 (meshes 1206–1224), head variant 0
(meshes 1358, 1361; textures 1359, 1362), body textures such as 2456 with palette 1853.

## Open questions

- The mesh flag bit (`header[1] & 0x80`, kept in the part record and copied to the node).
- The face texture (`head[9]/[10]`). Blood does not swap textures: `blood_apply` paints red
  texels into the loaded head, neck and chest textures through 1-bit masks at `0x8002DA30`.
- Ring, ropes, turnbuckles and arena meshes: the small 0x2E–0x122 files in 619–1051 have
  not been checked against this format, and the 395 matches include files that no roster
  costume uses (722–890, 985–1043), which may be referees, managers or props.
