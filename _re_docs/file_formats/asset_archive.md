# AKI asset archive (WCW vs. nWo: World Tour)

The ROM keeps nearly all game data in one numbered file archive. Files are referenced only by
number (1–5498). Some are stored raw and some are LZSS-compressed.
Tool: `tools/rom/aki_archive.py ROM list|extract|verify`.

Checked against the US ROM (SHA1 `5AD2D8359058C8BB71F08E3D3433B7A50D3BB645`):

- All 5498 files unpack. 1733 of them are compressed, and they total 9.73 MB unpacked.
- Every unpacked size matches the size table the game builds in RAM (`*0x80047904`).
- Files 221, 430, 431, 432 and 433 match the game's own loaded copies in RAM byte for byte.
  Those copies came from a live match in the recomp.

VPW64 and the later games have not been checked yet. The table addresses below are specific
to this ROM.

## Layout in ROM

| What | ROM | Notes |
|------|-----|-------|
| file data | `0x039490` – `0x7C1A78` | `DATA_BASE = 0x39490` |
| offset table | `0x7C1A78` | 5499 big-endian `u32`, one per file plus an end entry |

Entry `n-1` describes file `n`:

- `offset = entry & ~1`, so the file starts at `DATA_BASE + offset`
- `compressed = entry & 1`
- the file ends where the next entry starts (`DATA_BASE + (entry[n] & ~1)`)

The last entry points at `0x7C1A78`, so the data runs straight into the table.

At boot the game copies this table into RAM (at `0x803CE780` in the session examined, with
the pointer kept at `0x80047900`). It also keeps a table of unpacked sizes (pointer at
`0x80047904`). That second table is not stored in the ROM in this form: each size equals the
LZSS header of a compressed file, or the stored length of a raw file.

## Loader (main code segment)

- `func_80004D24(info*, n)` looks up file `n`. It hangs on `n == 0` or `n >= 5499`. It fills
  `info` with the ROM start and end (offset + `0x39490`), the unpacked size, and the packed
  length (or 0 for raw files).
- `func_80004DDC(info*, n, dest)` loads a file. A compressed file is DMA'd into a temporary
  heap buffer (`func_800007F4` allocates, `func_800008FC` frees) and then decompressed into
  `dest`. A raw file is DMA'd straight into `dest` (`func_80000650`).
- `func_80004E8C(src, dest)` is the decompressor.

## Compression: LZSS (Okumura-style)

```
u32  unpacked_size            // big-endian
then a stream of groups:
  u8 flags                    // read LSB first, one bit per item
  bit = 1 -> 1 literal byte
  bit = 0 -> 2-byte reference  b0, b1
       pos    = b0 | (b1 & 0xF0) << 4      // 12-bit position in a 4 KB ring buffer
       length = (b1 & 0x0F) + 3            // 3..18 bytes
```

- The ring buffer is 4096 bytes, zero-filled, and writing starts at `0xFEE` (4096 − 18). This
  is the classic Okumura LZSS setup.
- The game does not keep a separate ring buffer. It decodes straight into the output and
  turns `pos` into a distance back from the current output position:
  `rel = ((pos + 0x12) & 0xFFF) - (out_pos & 0xFFF)`, then `rel -= 0x1000` if `rel >= 0`.
  A source position before the start of the output reads as 0, standing in for the
  zero-filled ring.
- The game first zeroes up to 4096 bytes of the output, so references into the initial window
  also produce zeros.
- Decoding stops once `unpacked_size` bytes are written.

## Archive map

`aki_archive.py ROM survey` prints the run-by-run type list. The rules it uses are listed under
"Texture and palette files" below. Tags: **code** = the game's code loads it this way;
**shape** = inferred from size or contents only.

| Files | Count | Contents | Tag |
|-------|-------|----------|-----|
| 1–2 | 2 | 0xD20 and 0x1C23 bytes, mostly `0xFF` (lookup or bitmask tables?). Loaded from the main segment at `0x8002916C` (file 1) and `0x8002919C` (file 2) | code (purpose unknown) |
| 3–88 | 86 | pairs: a 0x1900-byte image (80×80 at 8 bpp?) plus a small `TEX` image, likely one pair per roster portrait | shape |
| 89–162 | 74 | `TEX` images, mostly 64×64 4 bpp (0x830 bytes) plus 128×128 (0x2030) | shape |
| 163–220 | 58 | mixed: 256-colour palettes, `TEX` images, and 173–209 are unknown 0x240–0x600 blobs (fonts or UI masks?) | shape |
| 221 | 1 | global move table (0x3F0); loaded to `0x800F8608` at every match start. Its 24-byte entries look like move records | code |
| 222–617 | 396 | **move sets**: 99 groups of 4 files (header 0x20, 222 move records 0x14D0, animation lists 0x4C4, 0x60 block) | code |
| 618 | 1 | **animation table**: 32 bytes for each of the 2598 animations (`*0x80047E1C`), see `animations.md` | code |
| 619–1051 | ~430 | textures (8-byte header) with 16- and 256-colour palettes, and ~100 small mesh parts: the **ring and arena scenery** (crowd stands, entrance, barrier, floor, ring, posts), per arena; see `individual_games/wcw_world_tour/arena.md` | code |
| 1052–1260 | 209 | **wrestler body mesh parts**, see `wrestler_models.md` (395 files in all match that format, also in 722–1043 and 1261–1717) | code |
| 1261–1718 | ~460 | per-wrestler **heads**: mesh parts with their textures and palettes (`g_head_tables`, see `wrestler_models.md`) | code |
| 1719–2387 | 669 | 16-colour palettes (0x20 each) for costume textures | code |
| 2388–2900 | 513 | 4 bpp textures (mostly 64×64 and 32×64), the last few paired with palettes. `func_80007AC0(obj, tex, pal)` loads pairs such as 2871/2872 and 2873/2874 | code for those pairs, shape otherwise |
| 2901–5498 | 2598 | **animations**, all stored raw. The animation id *is* the file number: `func_800097EC` rejects ids < 2901 and indexes file 618 with `id − 2901`. Format in `animations.md` | code |

Overall: 2598 animations, ~1000 palettes, ~1200 textures, 396 move-set files, and ~490 files
that are unknown or mesh-like.

### Texture and palette files (shape, consistent across ~1200 files)

- **8-byte header textures:** `[w−1, h−1, fmt_hi, fmt_lo, ?, ?, log2(w), log2(h)]` followed by
  `w*h*bpp/8` bytes. Seen: `00 10` with 4 bpp, and `01 00` with 8 bpp. These fields look like
  they were prepared for the RDP tile setup.
- **`TEX` textures:** `"TEX\0"`, u16 width, u16 height, then `04 02 00 10` (format?) and 4
  zero bytes. A 16-colour RGBA5551 palette (0x20 bytes) and 4 bpp pixels follow. Seen at
  32×32, 64×64 and 128×128.
- **Palettes:** raw RGBA5551. A 0x20-byte file is a 16-colour palette and a 0x200-byte file
  is a 256-colour palette.

### Move-set files (code)

| File | Size | Use |
|------|------|-----|
| 222 + 4k | 0x20 | per-wrestler header → `0x800F1D60 + slot*0x20` |
| 223 + 4k | 0x14D0 | 222 move records → `0x800F32A8 + slot*0x14D8` (see `individual_games/wcw_world_tour/moves.md`) |
| 224 + 4k | 0x4C4 | animation lists → `0x800F1F68 + slot*0x4D0` |
| 225 + 4k | 0x60 | per-wrestler block → `0x800F1DE0 + slot*0x60` |

k = 0…98. The roster tables at `0x800E74CC` say which group belongs to which wrestler; only
42 of the 99 groups are used by the playable roster (see `moves.md`). File 996 is also 0x60
bytes and may be a default block.
