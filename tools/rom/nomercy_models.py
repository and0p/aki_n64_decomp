"""Wrestler appearance data of WWF No Mercy (USA, Rev 1): first pass for the previewer.

Notes: _re_docs/individual_games/wwf_no_mercy/wrestler_models.md.

    python tools/rom/nomercy_models.py ROM roster      # wrestlers, attires, appearance bytes
    python tools/rom/nomercy_models.py ROM assets      # mesh / texture counts in the archive
    python tools/rom/nomercy_models.py ROM preview OUT.html  # bind-pose check page (ROM data: keep out of git)
    python tools/rom/nomercy_models.py ROM viewer OUT.html   # full previewer with animations (ditto)

No Mercy builds every wrestler like a created wrestler: each attire is a 63-byte appearance
(part numbers and colours), expanded at match start by sub_80018A3C into meshes and texture
layers. The defaults come from archive file 1 (38-byte records) through the attire pointer
table at 0x80047124; names come from file 2.
"""
import argparse
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from aki_archive import Archive, classify  # noqa: E402
from wcw_models import is_mesh, decode_mesh  # noqa: E402

MAIN_ROM, MAIN_VRAM, MAIN_END = 0x1000, 0x80000400, 0x80056670
ATTIRE_PTRS = 0x80047124    # 84 wrestlers x 4 attires: pointer to a 20-byte attire descriptor
WRESTLERS = 84
ATTIRE_FILE = 1             # 38-byte packed attire records (sub_8000B60C unpacks them)
NAME_FILE = 2               # u16 offsets, then 0-ended strings (sub_8000B780)
RECORD = 38
# sub_8000B60C: appearance byte -> byte of the 38-byte record. Unlisted appearance bytes are 0.
APPEARANCE_FROM_RECORD = {
    0: 0, 1: 1, 3: 2, 4: 3, 5: 4, 6: 5, 7: 6, 8: 7, 9: 8, 10: 9, 11: 10, 12: 28, 15: 29, 18: 30,
    19: 31, 21: 32, 24: 33, 27: 34, 30: 12, 31: 13, 33: 17, 34: 18, 36: 19, 37: 20, 39: 15,
    40: 16, 42: 21, 43: 22, 45: 23, 46: 24, 48: 25, 49: 26, 50: 27, 51: 14, 54: 35, 55: 36,
    57: 11, 60: 37,
}


class Main:
    def __init__(self, rom):
        self.rom = rom

    def off(self, a):
        assert MAIN_VRAM <= a < MAIN_END, hex(a)
        return a - MAIN_VRAM + MAIN_ROM

    def u8(self, a):
        return self.rom[self.off(a)]

    def u16(self, a):
        return struct.unpack_from(">H", self.rom, self.off(a))[0]

    def u32(self, a):
        return struct.unpack_from(">I", self.rom, self.off(a))[0]


def names(arc):
    data = arc.read(NAME_FILE)

    def get(k):
        off = struct.unpack_from(">H", data, 2 * k)[0]
        return data[off:data.index(b"\0", off)].decode("latin1")
    return get


def appearance(record):
    out = [0] * 63
    for a, r in APPEARANCE_FROM_RECORD.items():
        out[a] = record[r]
    return out


def roster(main, arc):
    """Every wrestler: roster word, names and the four attires (sub_8000B834)."""
    records, name = arc.read(ATTIRE_FILE), names(arc)
    out = []
    for w in range(WRESTLERS):
        attires = []
        for k in range(4):
            p = main.u32(ATTIRE_PTRS + 16 * w + 4 * k)
            n = main.u16(p + 14)
            attires.append({"record": n, "appearance": appearance(records[RECORD * n:RECORD * (n + 1)])})
        p = main.u32(ATTIRE_PTRS + 16 * w)
        nm = main.u16(p + 16)
        out.append({"slot": w, "roster": main.u16(p), "name": name(nm), "short": name(nm + 1),
                    "alias": name(nm + 2), "attires": attires})
    return out


# --- model parts (appearance_build, 0x80018A3C) ------------------------------------------

BUILDS = 0x80048A48         # s16 per body type: build (0-5, 6+ probably female)
BASE_MESHES = 0x80048A78    # ptr per build -> 20 u16 meshes for BASE_SLOTS (sub_800186D4)
BASE_TEX_SEL = 0x80048AA4   # s16 per body type -> index into BASE_TEXTURES
BASE_TEXTURES = 0x80048AD4  # ptr per index -> 20 u16 palette files (texture = palette + 1)
BASE_SLOTS = 0x8004FD28     # 20 s8 part slots
HEAD_MESHES_M = 0x8004913C  # ptr per head (byte 12) -> ptr per style (byte 18) -> u16 head mesh, face mesh
HEAD_MESHES_F = 0x80049148
FACE_TEXTURES = 0x8004D254  # u16 palette file per face (byte 15); texture = palette + 1
# Items with meshes (appearance_build): (byte, kind table or None, kind type, [kind][column] mesh
# lists, column table or None (column = build), mesh slot list)
MESH_ITEMS = [
    (3, 0x8004CF98, "u8", 0x8004CF64, None, 0x8004FD40),   # lower / full-body outfit
    (6, 0x8004EB98, "u8", 0x8004EB60, None, 0x8004FD64),   # upper body
    (30, None, None, 0x8004D4E8, 0x8004D4DC, 0x8004FDA4),  # (1-7) arms
    (9, 0x8004F85C, "s16", 0x8004F824, None, 0x8004FD7C),  # head wear (masks, hats)
    (48, 0x8004F288, "u8", 0x8004F278, 0x8004F26C, 0x8004FDCC),  # boots (< 64; kind 0 = none)
]
# Texture layers in model_build_textures' default order (recipe 0): (byte, largest item, layer
# list per item, slot list the builder writes, slot lists drawn, palette band). A layer is a CI4
# texture whose non-zero colours overwrite the slot's CI8 texture with index + band
# (sub_8000CAF8); its 32 colours go into the slot's palette at the band.
LAYER_ITEMS = [
    (3, 255, 0x8004D018, 0x8004FD50, [0x8004FD50], 64),
    (6, 255, 0x8004EC18, 0x8004FD70, [0x8004FD70], 96),
    (39, 7, 0x8004FCDC, 0x8004FDBC, [0x8004FDC0, 0x8004FDBC], 128),
    (48, 255, 0x8004F2C8, 0x8004FDD0, [0x8004FDD0], 128),
    (27, 31, 0x8004EF84, 0x8004FDE0, [0x8004FDE0], 160),
    (33, 7, 0x8004D234, 0x8004FDB4, [0x8004FDB4], 160),
    (36, 7, 0x8004D234, 0x8004FDB8, [0x8004FDB8], 160),
    (42, 15, 0x8004EE4C, 0x8004FDC4, [0x8004FDC4], 160),
    (45, 15, 0x8004EE4C, 0x8004FDC8, [0x8004FDC8], 160),
    (30, 7, 0x8004D508, 0x8004FDAC, [0x8004FDAC], 192),
    (9, 31, 0x8004F89C, 0x8004FD7C, [0x8004FD7C], 224),
]
TATTOOS = 0x8004FC48        # byte 51: layers on slots 0x8004FD94, band 32 (texture = file, palette = file + 1)
HAIR_LAYERS = 0x8004918C    # byte 18 (< 32): layers on slots 0x8004FDD8, band 128 (texture = file)
HAIR_PALETTES = 0x8004912C  # u16 palette file per hair colour (byte 19)
FACE_DETAILS = 0x8004D528   # u16 texture per face detail (byte 24); palette = texture + skin-tone step
FACE_ROWS = 0x8004D354      # u8 per face (byte 15) and per detail (0x8004D568): the detail's row shift
DETAIL_ROWS = 0x8004D568
BEARDS = 0x800490AC         # u16 texture per facial hair (byte 21), in the hair colour
LAYER_RECIPES = 0x8004FDF8  # 12 x 8 steps: the layer order
LAYER_ORDER_BY_KIND = 0x8004FE78  # recipe = [6 * 0x8004FE58[upper kind] + 0x8004FE68[lower kind]]


def slot_list(main, a):
    out = []
    while True:
        v = main.u8(a)
        if v == 0:
            return out
        out.append(v - 256 if v > 127 else v)
        a += 1


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def s8(v):
    v &= 0xFF
    return v - 0x100 if v & 0x80 else v


def model_parts(main, look):
    """Part slot -> {mesh, tex, pal, layers} for an appearance (appearance_build and
    model_build_textures); layers are (texture file, palette file, band) in drawing order.

    Slots 1-20 follow World Tour's bones; 22-24 are the second halves of the torso parts 1-3
    (pelvis -x / +x, waist and chest front / back). Not applied yet: the item colours
    (sub_8001858C / sub_80019550), recipe orders other than 0 and the alternative heads."""
    body = look[0]
    build = s16(main.u16(BUILDS + 2 * body))
    sel = s16(main.u16(BASE_TEX_SEL + 2 * body))
    meshes, pals = main.u32(BASE_MESHES + 4 * build), main.u32(BASE_TEXTURES + 4 * sel)
    parts = {}

    def part(s):
        return parts.setdefault(s, {"mesh": 0, "tex": 0, "pal": 0, "layers": []})
    for i, s in enumerate(slot_list(main, BASE_SLOTS)[:20]):
        p = part(s)
        p["mesh"], p["pal"] = main.u16(meshes + 2 * i), main.u16(pals + 2 * i)
        p["tex"] = p["pal"] + 1
    for byte, kinds, ktype, mesh_tab, columns, slots in MESH_ITEMS:
        item = look[byte]
        if (not item and byte != 3) or (byte == 30 and item > 7):  # outfit 0 is a real item
            continue
        kind = item
        if kinds is not None:
            kind = main.u8(kinds + item) if ktype == "u8" else s16(main.u16(kinds + 2 * item))
            if byte == 48 and (item >= 64 or not kind):
                continue
        col = build if columns is None else main.u8(columns + build)
        row = main.u32(mesh_tab + 4 * kind)
        ml = main.u32(row + 4 * col) if row else 0
        if not ml:
            continue
        for i, s in enumerate(slot_list(main, slots)):
            m = main.u16(ml + 2 * i)
            if m:
                part(s)["mesh"] = m
    # Head (appearance_build 0x80018C7C-0x80018E38): neck mesh 5375 and the skin textures are
    # fixed per sex; head and face meshes come from [byte 12][byte 18], the face texture from byte 15.
    female = build >= 6
    skin_pal = 8045 if female else 8043
    parts[4] = {"mesh": 5375, "tex": 8042, "pal": skin_pal, "layers": []}
    if female:  # the female lists start at style 22 (lw -88)
        hm = main.u32(main.u32(HEAD_MESHES_F + 4 * look[12]) + 4 * (look[18] - 22))
    else:
        hm = main.u32(main.u32(HEAD_MESHES_M + 4 * look[12]) + 4 * look[18])
    parts[5] = {"mesh": main.u16(hm), "tex": 8046 if female else 8044, "pal": skin_pal, "layers": []}
    if main.u16(hm + 2) and look[15] < 127:
        face = main.u16(FACE_TEXTURES + 2 * look[15])
        parts[20] = {"mesh": main.u16(hm + 2), "tex": face + 1, "pal": face, "layers": []}

    # Skin tone (byte 1, 0-7) picks the palette variant of skin-coloured layers: file + 1 / 2 / 5.
    tone = look[1] if look[1] < 8 else 0
    tone_pal = 1 if tone < 4 else (2 if tone < 6 else 5)
    hair_pal = main.u16(HAIR_PALETTES + 2 * look[19])

    def layers(byte, lst, write_slots, draw_slots, band, kind="std"):
        if not lst:
            return
        colours = (look[byte + 1], look[byte + 2])  # the item's triplet (appearance_layers)
        files = [main.u16(lst + 2 * i) for i in range(len(slot_list(main, write_slots)))]
        for ds in draw_slots:
            for f, s in zip(files, slot_list(main, ds)):
                if not f or s not in parts:
                    continue
                if s == 21 and not parts[21]["tex"]:  # slot 21 has no base: the layer is its texture
                    parts[21].update(tex=f + 1, pal=f, colours=colours)
                elif kind == "std":
                    parts[s]["layers"].append((f + 1, f, band, colours, 0))
                elif kind == "tattoo":
                    parts[s]["layers"].append((f, f + tone_pal, band, colours, 0))
                else:  # hair
                    parts[s]["layers"].append((f, hair_pal, band, colours, 0))

    def item_layers(byte):
        for b, top, table, write_slots, draw_slots, band in LAYER_ITEMS:
            if b == byte and (0 < look[b] <= top or (b == 3 and look[b] == 0)):
                layers(b, main.u32(table + 4 * look[b]), write_slots, draw_slots, band)

    # model_build_textures' order. Face details first, stamped on the face (slot 20):
    # byte 24 (1-31) with the skin-tone palette, band 96, shifted down by
    # 0x8004D354[face] - 0x8004D568[detail] rows.
    if 20 in parts and 0 < look[24] < 32:
        f = main.u16(FACE_DETAILS + 2 * look[24])
        shift = 0
        if look[15] < 118:
            shift = s8(main.u8(FACE_ROWS + look[15]) - main.u8(DETAIL_ROWS + look[24]))
        parts[20]["layers"].append((f, f + tone_pal, 96, (0, 0), shift))
    if look[51]:
        layers(51, main.u32(TATTOOS + 4 * look[51]), 0x8004FD94, [0x8004FD94], 32, "tattoo")
    upper = main.u8(0x8004EB98 + look[6])
    lower = main.u8(0x8004CF98 + look[3]) if look[3] < 128 else 0
    recipe = main.u8(LAYER_ORDER_BY_KIND + 6 * main.u8(0x8004FE58 + upper) + main.u8(0x8004FE68 + lower))
    steps = [[3], [6], [39], [48], [27], [33, 36], [42, 45], [30]]
    for step in (main.u8(LAYER_RECIPES + 8 * recipe + i) for i in range(8)):
        for byte in steps[step]:
            item_layers(byte)
    # Facial hair (byte 21) on the face in the hair colour, band 128, then hair and head wear;
    # head wear goes first when 0x8004FFB0[byte 9] is set.
    beard = []
    if 20 in parts and look[21] < 128 and main.u16(BEARDS + 2 * look[21]):
        beard = [(main.u16(BEARDS + 2 * look[21]), hair_pal, 128, (0, 0), 0)]

    def hair():
        parts[20]["layers"].extend(beard) if beard else None
        if look[18] < 32:
            layers(18, main.u32(HAIR_LAYERS + 4 * look[18]), 0x8004FDD8, [0x8004FDD8], 128, "hair")
    if look[9] < 32 and main.u8(0x8004FFB0 + look[9]):
        item_layers(9)
        hair()
    else:
        hair()
        item_layers(9)
    # Alternative heads (sub_800188FC): hoods and full masks replace the head (slot 5) and face
    # (slot 20) meshes and add slot 21, with their own layers on slots 21, 5, 20 (band 32, last).
    # Picked by byte 54 (1-9) when head wear byte 9 sets flag 0x40 (0x8004FFD0), or by the head
    # wear's layer list (s16 at +34, 72 and up).
    alt, alt_colours = None, (look[10], look[11])
    if look[9] < 32 and main.u8(0x8004FFD0 + look[9]) and 1 <= look[54] <= 9:
        alt, alt_colours = look[54] - 1, (look[55], look[56])
    elif 0 < look[9] < 32 and main.u32(0x8004F89C + 4 * look[9]):
        v = s16(main.u16(main.u32(0x8004F89C + 4 * look[9]) + 34))
        if v >= 72:
            alt = v - 65
    if alt is not None:
        if female:
            col, row = main.u8(0x8004A045 + look[12]), main.u8(0x8004A05A + look[18])
        else:
            col, row = main.u8(0x8004A04C + look[12]), main.u8(0x8004A058 + look[18])
        trip = main.u32(main.u32(main.u32(0x8004A07C + 4 * main.u8(0x8004A040 + alt)) + 4 * col) + 4 * row)
        lst = main.u32(0x8004A0A0 + 4 * alt) if alt < 10 else 0
        layer_files = [main.u16(lst + 2 * i) for i in range(3)] if lst else [0, 0, 0]
        # Slot 21's texture (model_load_parts): the head wear's own layer for slot 21 (record +400,
        # entry 16 of 0x8004FD7C) when there is one, else the alternative head's (record +576).
        hw = main.u32(0x8004F89C + 4 * look[9]) if 0 < look[9] < 32 else 0
        hw21 = main.u16(hw + 2 * 16) if hw else 0
        for slot, mesh, f in zip((21, 5, 20), (main.u16(trip), main.u16(trip + 2), main.u16(trip + 4)), layer_files):
            if slot == 21:
                tex, colours = (hw21, (look[10], look[11])) if hw21 else (f, alt_colours)
                if mesh and tex:
                    parts[21] = {"mesh": mesh, "tex": tex + 1, "pal": tex, "layers": [], "colours": colours}
                continue
            if mesh and slot in parts:
                parts[slot]["mesh"] = mesh
            if f and slot in parts:
                parts[slot]["layers"].append((f + 1, f, 32, alt_colours, 0))
    return parts


COLOUR_RAMPS = 4341          # 32 colours x 16 shades, RGBA5551 (sub_80019550)
KEY_MASKS = (0x07C1, 0x003F)  # colour 1 / colour 2 key fields (0x8005002C), shifts 6 / 1 (0x80050030)
KEY_SHIFTS = (6, 1)


def recolour(pal, band, colours, ramps):
    """sub_80019550: entries band..band+15 whose key (entry + 16) lies only in one key field (and
    is 2 or more) become shade 15 - (key field >> shift) / 2 of that colour's ramp; colours 32 and
    up give 0x0001; colour 0 leaves the band alone."""
    for k in range(2):
        c = colours[k]
        if c <= 0 or c > 127:
            continue
        for i in range(band, band + 16):
            key = struct.unpack_from(">H", pal, 2 * (i + 16))[0]
            if key & ~KEY_MASKS[k] & 0xFFFF or key < 2:
                continue
            if c < 32:
                shade = 15 - (((key & KEY_MASKS[k]) >> KEY_SHIFTS[k]) >> 1)
                v = struct.unpack_from(">H", ramps, 2 * (16 * c + shade))[0]
            else:
                v = 1
            struct.pack_into(">H", pal, 2 * i, v)


def composite(arc, p):
    """One slot's texture: the CI8 base with its layers stamped in (sub_8000CAF8).
    Returns (w, h, rgba, wrap)."""
    from wcw_models import decode_texture
    base, pal = arc.read(p["tex"]), bytearray(arc.read(p["pal"]))
    w, h = base[0] + 1, base[1] + 1
    ramps = arc.read(COLOUR_RAMPS)
    if struct.unpack_from(">H", base, 2)[0] != 0x0100:  # a CI4 part texture (slot 21), recoloured
        if p.get("colours") and len(pal) >= 64:
            recolour(pal, 0, p["colours"], ramps)
        return decode_texture(base, bytes(pal))
    idx = bytearray(base[8:8 + w * h])
    pal += bytes(max(0, 512 - len(pal)))
    for tex, lp, band, item_colours, shift in p["layers"]:
        t, lpal = arc.read(tex), arc.read(lp)
        lw, lh = t[0] + 1, t[1] + 1
        if lw != w or lh > h:
            continue
        colours = [struct.unpack_from(">H", lpal, 2 * i)[0] for i in range(min(32, len(lpal) // 2))]
        px = t[8:]
        for i in range(lw * lh):
            y = i // lw + shift
            if not 0 <= y < h:
                continue
            n = (px[i >> 1] >> 4) if i % 2 == 0 else (px[i >> 1] & 0xF)
            if n < len(colours) and colours[n]:
                idx[y * w + i % lw] = n + band
        pal[2 * band:2 * band + 2 * len(colours)] = lpal[:2 * len(colours)]
        recolour(pal, band, item_colours, ramps)
    rgba = bytearray()
    for n in idx:
        c = struct.unpack_from(">H", pal, 2 * n)[0]
        rgba += bytes(((c >> 11 & 31) * 255 // 31, (c >> 6 & 31) * 255 // 31,
                       (c >> 1 & 31) * 255 // 31, 255 if c & 1 else 0))
    return w, h, bytes(rgba), [base[4], base[5]]


def preview(main, arc, out_path, slots=range(1, 12)):
    """Static bind-pose page (three.js) of the first wrestlers' attires, to check the decode."""
    import base64
    import json
    from wcw_models import png
    wrestlers, tex_cache, textures, meshes = [], {}, [], {}
    for w in roster(main, arc):
        if w["slot"] not in slots:
            continue
        looks = []
        for a in w["attires"]:
            nodes = []
            for slot, p in sorted(model_parts(main, a["appearance"]).items()):
                mesh = p["mesh"]
                if not mesh:
                    continue
                if mesh not in meshes:
                    meshes[mesh] = decode_mesh(arc.read(mesh))
                key = (p["tex"], p["pal"], tuple(p["layers"]), p.get("colours"))
                if p["pal"] and key not in tex_cache:
                    tw, th, rgba, wrap = composite(arc, p)
                    tex_cache[key] = len(textures)
                    textures.append({"w": tw, "h": th, "wrap": wrap,
                                     "png": base64.b64encode(png(tw, th, rgba)).decode()})
                nodes.append({"slot": slot, "mesh": mesh, "tex": tex_cache.get(key, -1)})
            looks.append(nodes)
        wrestlers.append({"name": w["name"], "looks": looks})
    data = {"wrestlers": wrestlers, "textures": textures,
            "meshes": {str(k): {"v": [v["pos"] + v["st"] for v in m["verts"]], "t": m["tris"]}
                       for k, m in meshes.items()}}
    page = PREVIEW_HTML.replace("__DATA__", json.dumps(data, separators=(",", ":")))
    open(out_path, "w", encoding="utf-8").write(page)
    print("wrote", out_path, len(page) // 1024, "KB")


PREVIEW_HTML = """<!doctype html><html><head><meta charset="utf-8"><title>No Mercy parts</title>
<style>body{margin:0;background:#334;color:#eee;font:14px sans-serif}#ui{position:absolute;padding:8px}</style>
<script type="importmap">{"imports":{"three":"https://cdn.jsdelivr.net/npm/three@0.160.0/build/three.module.js",
"three/addons/":"https://cdn.jsdelivr.net/npm/three@0.160.0/examples/jsm/"}}</script></head><body>
<div id="ui"><select id="w"></select> <select id="a"><option>0<option>1<option>2<option>3</select></div>
<script type="module">
import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
const D = __DATA__;
const r = new THREE.WebGLRenderer({antialias:true, preserveDrawingBuffer:true}); r.setSize(innerWidth, innerHeight);
document.body.appendChild(r.domElement);
const scene = new THREE.Scene(), cam = new THREE.PerspectiveCamera(35, innerWidth/innerHeight, 1, 2000);
const h = new URLSearchParams(location.hash.slice(1));
const c = (h.get("cam")||"0,0,320").split(",").map(Number); cam.position.set(c[0], c[1], c[2]);
const ctl = new OrbitControls(cam, r.domElement); ctl.target.set(0, 0, 0); ctl.update();
const tex = D.textures.map(t => { const im = new Image(); im.src = "data:image/png;base64," + t.png;
  const x = new THREE.Texture(im); im.onload = () => x.needsUpdate = true;
  x.wrapS = t.wrap[0] & 1 ? THREE.MirroredRepeatWrapping : THREE.RepeatWrapping;
  x.wrapT = t.wrap[1] & 1 ? THREE.MirroredRepeatWrapping : THREE.RepeatWrapping;
  x.magFilter = THREE.NearestFilter; x.colorSpace = THREE.SRGBColorSpace; x.flipY = false; x.w = t.w; x.h = t.h; return x; });
let group = null;
function show() {
  if (group) scene.remove(group); group = new THREE.Group();
  const look = D.wrestlers[+wsel.value].looks[+asel.value];
  for (const n of look) {
    const m = D.meshes[n.mesh], t = tex[n.tex], g = new THREE.BufferGeometry();
    const pos = [], uv = [];
    for (const tri of m.t) for (const i of tri) { const v = m.v[i]; pos.push(v[0], v[1], v[2]);
      uv.push(t ? v[3] / t.w : 0, t ? v[4] / t.h : 0); }
    g.setAttribute("position", new THREE.Float32BufferAttribute(pos, 3));
    g.setAttribute("uv", new THREE.Float32BufferAttribute(uv, 2));
    group.add(new THREE.Mesh(g, new THREE.MeshBasicMaterial(t ? {map:t, side:THREE.DoubleSide, alphaTest:0.5}
      : {color:0xff00ff, side:THREE.DoubleSide})));
  }
  scene.add(group);
}
const wsel = document.getElementById("w"), asel = document.getElementById("a");
D.wrestlers.forEach((w, i) => wsel.add(new Option(w.name, i)));
wsel.value = h.get("w") || 0; asel.value = h.get("a") || 0;
wsel.onchange = asel.onchange = show; show();
(function loop() { requestAnimationFrame(loop); r.render(scene, cam); })();
</script></body></html>
"""


# --- full previewer (World Tour's template) ------------------------------------------------

SKELETON = 0x80047ED0       # bind (x, y) per joint 0..19, then joint order (+0x29), parents (+0x3C):
JOINTS = 19                 # the same 19-joint data as World Tour's 0x8002D3A0
SLOT_BONE = {20: 5, 21: 5, 22: 1, 23: 2, 24: 3}  # slots that ride another slot's bone


def skeleton(main):
    xy = [s8(main.u8(SKELETON + i)) for i in range(2 * (JOINTS + 1))]
    return {"bind": [[xy[2 * j], xy[2 * j + 1], 0] for j in range(JOINTS + 1)],
            "order": [main.u8(SKELETON + 0x29 + i) for i in range(JOINTS)],
            "parent": [main.u8(SKELETON + 0x3C + i) for i in range(JOINTS + 1)]}


# Seams (sub_80015D8C -> sub_8000D284, fixed calls): part slot -> the slots whose bones the first
# header[3] >> 4 and the next header[3] & 15 vertices follow (0 = none). Unlike World Tour the knee
# seam is on the shin and the neck has none; the head and chest share one instead.
SEAMS = {1: (14, 0), 22: (14, 7), 2: (3, 1), 23: (3, 1), 3: (5, 0), 5: (3, 0),
         6: (7, 8), 13: (14, 15), 11: (12, 9), 16: (19, 17)}


def seam_bones(slot, mesh):
    """Bones of a part's two seam groups (the part's own bone where the game sets none)."""
    own = SLOT_BONE.get(slot, slot)
    first, second = SEAMS.get(slot, (0, 0))
    return [SLOT_BONE.get(first, first) or own, SLOT_BONE.get(second, second) or own]


def build_viewer(main, arc, out_path):
    """The World Tour / Revenge previewer page with every No Mercy wrestler, attire and animation."""
    import base64
    import json
    from revenge_models import is_anim
    from wcw_models import png
    sk = skeleton(main)
    roster_out, meshes, textures = [], {}, {}
    for w in roster(main, arc):
        if not w["name"]:
            continue
        costumes = []
        for k, a in enumerate(w["attires"]):
            nodes = []
            for slot, p in sorted(model_parts(main, a["appearance"]).items()):
                if not p["mesh"] or not p["pal"]:
                    continue
                if p["mesh"] not in meshes:
                    meshes[p["mesh"]] = decode_mesh(arc.read(p["mesh"]))
                key = "%d_%d_%08x" % (p["tex"], p["pal"], hash((tuple(p["layers"]), p.get("colours"))) & 0xFFFFFFFF)
                if key not in textures:
                    tw, th, rgba, wrap = composite(arc, p)
                    textures[key] = {"w": tw, "h": th, "wrap": wrap,
                                     "png": base64.b64encode(png(tw, th, rgba)).decode()}
                bone = SLOT_BONE.get(slot, slot)
                nodes.append({"bone": bone, "mesh": p["mesh"], "texkey": key,
                              "seamBones": seam_bones(slot, meshes[p["mesh"]])})
            costumes.append({"attire": k, "nodes": nodes})
        roster_out.append({"name": "%s (0x%03X)" % (w["name"], w["roster"]), "group": "0",
                           "costumes": costumes})
    blob, anims = bytearray(), []
    for n in range(1, arc.count + 1):
        d = arc.read(n)
        if is_anim(d):
            anims.append([n, len(blob), len(d), "00" * 32])
            blob += d
    data = {"skeleton": sk, "seams": [], "roster": roster_out, "arenas": [], "arenaNames": [],
            "meshes": {str(k): v for k, v in meshes.items()}, "textures": textures,
            "anims": anims, "labels": {}}
    here = os.path.dirname(os.path.abspath(__file__))
    page = open(os.path.join(here, "wcw_viewer.html"), encoding="utf-8").read()
    # No Mercy parts name their seam bones, and one seam group may be empty (torso halves).
    page = page.replace("const [sa, sb] = m.seam, sm = seams[node.bone];",
                        "const [sa, sb] = m.seam, sm = node.seamBones || seams[node.bone];")
    page = page.replace("if (sm && sa > 0 && sb > 0)", "if (sm && (sa > 0 || sb > 0))")
    page = page.replace('"wcw_anim_tags"', '"nomercy_anim_tags"').replace('"wcw_anim_last"', '"nomercy_anim_last"')
    # #cam=x,y,z[,tx,ty,tz] sets the camera for screenshots.
    page = page.replace("controls.target.set(0, 80, 0);",
                        "controls.target.set(0, 80, 0);\n"
                        "{ const c = new URLSearchParams(location.hash.slice(1)).get('cam');"
                        " if (c) { const v = c.split(',').map(Number); camera.position.set(v[0], v[1], v[2]);"
                        " if (v.length > 5) controls.target.set(v[3], v[4], v[5]); controls.update(); } }")
    page = page.replace("/*DATA*/null", json.dumps(data, separators=(",", ":")))
    page = page.replace("/*ANIMBLOB*/\"\"", '"' + base64.b64encode(bytes(blob)).decode() + '"')
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(page)
    print("wrote %s: %d wrestlers, %d meshes, %d textures, %d animations"
          % (out_path, len(roster_out), len(meshes), len(textures), len(anims)))


def assets(arc):
    kinds, fmts, verts, tris = collections.Counter(), collections.Counter(), [], []
    for n in range(1, arc.count + 1):
        d = arc.read(n)
        k = classify(n, d, "nomercy")
        if k == "unknown" and is_mesh(d):
            k = "mesh"
            m = decode_mesh(d)
            verts.append(len(m["verts"]))
            tris.append(len(m["tris"]))
        if k.startswith("tex_"):
            fmts["0x%04X" % struct.unpack_from(">H", d, 2)[0]] += 1
        kinds[k] += 1
    print("files by kind:", dict(kinds.most_common()))
    print("texture formats:", dict(fmts))
    print("mesh parts: %d, average %.1f vertices / %.1f triangles, largest %d / %d"
          % (len(verts), sum(verts) / len(verts), sum(tris) / len(tris), max(verts), max(tris)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("cmd", choices=["roster", "assets", "preview", "viewer"])
    ap.add_argument("out", nargs="?", help="preview: output .html (keep it out of git, e.g. under _tools/)")
    args = ap.parse_args()
    rom = open(args.rom, "rb").read()
    arc = Archive(rom)
    if arc.game != "nomercy":
        sys.exit("expected the No Mercy Rev 1 ROM")
    if args.cmd == "assets":
        assets(arc)
        return
    if args.cmd == "viewer":
        build_viewer(Main(rom), arc, args.out or "_tools/viewer/nomercy_viewer.html")
        return
    if args.cmd == "preview":
        preview(Main(rom), arc, args.out or "_tools/viewer/nomercy_parts.html")
        return
    for w in roster(Main(rom), arc):
        if not w["name"]:
            continue
        print("%2d 0x%03X %-20s" % (w["slot"], w["roster"], w["name"]))
        for k, a in enumerate(w["attires"]):
            print("     attire %d (record %3d): %s" % (k, a["record"], " ".join("%02x" % b for b in a["appearance"])))


if __name__ == "__main__":
    main()
