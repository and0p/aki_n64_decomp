"""Wrestler models, textures and animations of WCW vs. nWo: World Tour (US).

Format notes: _re_docs/file_formats/wrestler_models.md and
_re_docs/file_formats/animations.md

    python tools/rom/wcw_models.py ROM roster
    python tools/rom/wcw_models.py ROM anim ANIM_ID [--frames]
    python tools/rom/wcw_models.py ROM viewer OUT.html

`viewer` writes one self-contained HTML page (three.js from a CDN) with every roster
wrestler, costume and animation. It holds ROM-derived data, so keep it out of git
(for example under _tools/).
"""
import argparse
import base64
import json
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from aki_archive import Archive, ANIM_META, ANIM_FIRST, FILE_COUNT  # noqa: E402

# Main code segment: N64 0x80000400 is ROM 0x1000.
MAIN_VRAM, MAIN_ROM = 0x80000400, 0x1000

JOINTS = 19                    # skeleton joints 1..19; 0 is the model root
BIND_XY = 0x8002D3A0           # s8 (x, y) per joint 0..19: bind pose, z = 0
JOINT_ORDER = 0x8002D3C9       # 19 joint numbers, parents before children
JOINT_PARENT = 0x8002D3DC      # u8 per joint 0..19; 0 = no parent (position animated)
BONE_PART = 0x8002DE31         # s8 per bone 1..19: index into the body part list, -1 = head
BODY_PARTS = 0x8002E500        # ptr per body type -> 19 u16 mesh file numbers
COSTUMES = 0x80032B40          # ptr per roster group -> ptr per wrestler -> costume ptrs (0-ended)
HEADS = 0x8002F150             # ptr per roster group -> ptr per wrestler -> head variant ptrs

KEYFRAME_SIZE = 98             # bytes per keyframe in an animation file

# Roster order per group, from _re_docs/individual_games/wcw_world_tour/moves.md.
ROSTER = [
    ("nWo", ["Hollywood Hogan", "nWo Sting", "Buff Bagwell", "Eric Bischoff", "Scott Norton",
             "Kevin Nash", "Scott Hall", "Syxx", "Randy Savage"]),
    ("WCW", ["Lex Luger", "Sting", "The Giant", "Scott Steiner", "Rick Steiner", "Ric Flair",
             "Ultimo Dragon", "Dean Malenko", "Eddy Guerrero", "Rey Misterio Jr.", "Chris Benoit",
             "Steven Regal", "Diamond Dallas Page"]),
    ("DOA", ["Sumo Jo", "Kim Chee", "Blackheart", "Puchteca", "Hannibal", "Powder Keg", "Dim Sum",
             "Saladin", "Ali Baba", "Wrath"]),
    ("IU", ["Black Ninja", "Shaolin", "The Unknown", "The Claw", "Black Belt", "Paco Loco",
            "Shaman", "Master Fuji", "Glacier"]),
    ("WWW", ["Joe Bruiser"]),
    ("WWW", ["Black Widow"]),
]


class Main:
    """Reads tables out of the main code segment image in the ROM."""

    def __init__(self, rom):
        self.rom = rom

    def off(self, addr):
        return addr - MAIN_VRAM + MAIN_ROM

    def u8(self, addr, n):
        o = self.off(addr)
        return list(self.rom[o:o + n])

    def s8(self, addr, n):
        return list(struct.unpack_from(f">{n}b", self.rom, self.off(addr)))

    def u16(self, addr, n):
        return list(struct.unpack_from(f">{n}H", self.rom, self.off(addr)))

    def u32(self, addr):
        return struct.unpack_from(">I", self.rom, self.off(addr))[0]


def skeleton(main):
    """Bind pose, parents and evaluation order (func_8000992C, func_8000A5C4)."""
    xy = main.s8(BIND_XY, 2 * (JOINTS + 1))
    return {
        "bind": [[xy[2 * j], xy[2 * j + 1], 0] for j in range(JOINTS + 1)],
        "parent": main.u8(JOINT_PARENT, JOINTS + 1),
        "order": main.u8(JOINT_ORDER, JOINTS),
    }


# --- meshes -------------------------------------------------------------------------

def decode_mesh(data):
    """One model part (func_80005D20 / func_80005C14 / func_80005AE0)."""
    scale = data[0] + 1
    nverts = data[1] & 0x7F
    ntris = data[2]
    seam_a, seam_b = data[3] >> 4, data[3] & 0xF
    ox, oy, oz = (struct.unpack("b", data[4 + i:5 + i])[0] * 16 for i in range(3))
    s_off, t_off = data[7] & 0xF0, (data[7] << 4) & 0xF0
    verts = []
    for i in range(nverts):
        x, y, z, u, v, nx, ny, nz = struct.unpack_from(">bbbBBbbb", data, 8 + 8 * i)
        verts.append({
            "pos": [(x + ox) * scale, (y + oy) * scale, (z + oz) * scale],
            # the game writes s = 32 * (u + s_off) + 8 in 10.5 fixed point
            "st": [u + s_off + 0.25, v + t_off + 0.25],
            "n": [nx, ny, nz],
        })
    p = 8 + 8 * nverts
    tris = [list(data[p + 3 * i:p + 3 * i + 3]) for i in range(ntris)]
    return {"flag": data[1] >> 7, "seam": [seam_a, seam_b], "verts": verts, "tris": tris}


def is_mesh(data):
    if len(data) < 8:
        return False
    n, t = data[1] & 0x7F, data[2]
    size = 8 + 8 * n + 3 * t
    return n > 0 and t > 0 and size <= len(data) <= size + 3


# --- textures -----------------------------------------------------------------------

def decode_texture(data, palette):
    """8-byte header texture: [w-1, h-1, fmt(2), cms, cmt, log2 w, log2 h], then CI4 or
    CI8 pixels indexing an RGBA5551 palette. Returns (w, h, rgba bytes, wrap)."""
    w, h = data[0] + 1, data[1] + 1
    fmt = struct.unpack_from(">H", data, 2)[0]
    pal = []
    for i in range(len(palette) // 2):
        c = struct.unpack_from(">H", palette, 2 * i)[0]
        pal.append(((c >> 11 & 31) * 255 // 31, (c >> 6 & 31) * 255 // 31,
                    (c >> 1 & 31) * 255 // 31, 255 if c & 1 else 0))
    px = data[8:]
    out = bytearray()
    for i in range(w * h):
        if fmt in (0x0010, 0x0020):  # 0x0020 (Revenge): CI4 with a 32-colour palette, bank 0 used
            idx = (px[i >> 1] >> 4) if i % 2 == 0 else (px[i >> 1] & 0xF)
        else:
            idx = px[i]
        out += bytes(pal[idx] if idx < len(pal) else (255, 0, 255, 255))
    return w, h, bytes(out), [data[4], data[5]]


def png(w, h, rgba):
    def chunk(tag, body):
        c = tag + body
        return struct.pack(">I", len(body)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


# --- costumes -----------------------------------------------------------------------

def costumes(main, group, index):
    """Nodes of every costume of one wrestler (func_8000BB8C, func_8000BF70).

    Each node: bone it follows, mesh file, texture file, palette file."""
    bone_part = main.s8(BONE_PART, JOINTS)
    wrestlers = main.u32(COSTUMES + 4 * group)
    lst = main.u32(wrestlers + 4 * index)
    heads = main.u32(main.u32(HEADS + 4 * group) + 4 * index)
    result = []
    k = 0
    while True:
        rec_ptr = main.u32(lst + 4 * k)
        if rec_ptr == 0:
            break
        rec = main.u16(rec_ptr, 37)
        body_type = rec[0] & 0x7F
        head_var = (rec[0] >> 8) & 0xF
        head = main.u16(main.u32(heads + 4 * head_var), 11)
        body = main.u16(main.u32(BODY_PARTS + 4 * body_type), 19)
        nodes = []
        for bone in range(1, JOINTS + 1):
            part = bone_part[bone - 1]
            if part >= 0:
                nodes.append({"bone": bone, "mesh": body[part],
                              "tex": rec[2 + 2 * part], "pal": rec[1 + 2 * part]})
            elif bone == 4:
                nodes.append({"bone": 4, "mesh": head[0], "tex": head[4], "pal": head[3]})
            else:
                nodes.append({"bone": 5, "mesh": head[1], "tex": head[6], "pal": head[5]})
        if head[2]:
            nodes.append({"bone": 5, "mesh": head[2], "tex": head[8], "pal": head[7]})
        nodes.append({"bone": 8, "mesh": body[17], "tex": rec[36], "pal": rec[35]})
        nodes.append({"bone": 15, "mesh": body[18], "tex": rec[36], "pal": rec[35]})
        result.append({"body_type": body_type, "head": head_var, "face": [head[10], head[9]],
                       "nodes": nodes})
        k += 1
    return result


# Seam vertices: (bone of the part, bone for the first seam_a vertices, bone for the next
# seam_b vertices). The rest follow the part's own bone. From the func_80006630 calls in
# func_8000BB8C.
SEAMS = [(2, 3, 1), (4, 5, 3), (7, 1, 6), (14, 1, 13), (11, 12, 9), (16, 19, 17), (1, 14, 7)]


# --- animations ---------------------------------------------------------------------

def decode_anim(data):
    """Animation file (func_8000A42C streams it, func_8000AF78 decodes one joint).

    u32 header: bits 0-15 frame count, bits 16-25 yaw change (x4 = 1/4096 turns),
    bits 26-30 pose class. Then one 98-byte keyframe per 2 frames. Each keyframe has 19
    joints: a u32 with three 10-bit angles (x 360/1024 degrees); if bit 31 is set a
    position follows, absolute (bit 30 clear: 4 bytes, 11/11/10-bit sign-magnitude) or
    relative to the last absolute one (bit 30 set: 3 sign-magnitude bytes x 0.5)."""
    hdr = struct.unpack_from(">I", data, 0)[0]
    frames = hdr & 0xFFFF
    keys = []
    last = [0.0, 0.0, 0.0]
    for k in range((len(data) - 4) // KEYFRAME_SIZE):
        p = 4 + k * KEYFRAME_SIZE
        joints = []
        for _ in range(JOINTS):
            w = struct.unpack_from(">I", data, p)[0]
            p += 4
            rot = [(w & 0x3FF) * 360 / 1024, (w >> 10 & 0x3FF) * 360 / 1024,
                   (w >> 20 & 0x3FF) * 360 / 1024]
            pos = None
            if w & 0x80000000:
                if w & 0x40000000:
                    d = [(-(b & 0x7F) if b & 0x80 else b & 0x7F) * 0.5 for b in data[p:p + 3]]
                    pos = [last[i] + d[i] for i in range(3)]
                    p += 3
                else:
                    v = struct.unpack_from(">I", data, p)[0]
                    p += 4

                    def sm(x, bits):
                        m = x & ((1 << (bits - 1)) - 1)
                        return -m if x & (1 << (bits - 1)) else m
                    pos = [float(sm(v, 11)), float(sm(v >> 11, 11)), float(sm(v >> 22, 10))]
                    last = pos
            joints.append({"rot": rot, "pos": pos})
        keys.append(joints)
    return {"frames": frames, "yaw": (hdr >> 16 & 0x3FF) * 4, "pose": hdr >> 26 & 0x1F,
            "keys": keys}


# --- animation labels ---------------------------------------------------------------

OVL_B_VRAM, OVL_B_ROM = 0x80090000, 0xA69570
MOVE_ANIM_FIRST = 0x800E7BD0   # s16[222] g_move_anim_first
MOVE_SLOTS = 222
ANIM_LIST_BIAS = 0xD82         # list value + 0xD82 = animation id (wrestler_move_anim)
BASE_ANIMS = 0x800E6F40        # s16[44] g_base_anims (wrestler_base_anim)

# Move slot -> input, from _re_docs/individual_games/wcw_world_tour/moves.md.
SLOT_NAMES = {
    136: "counter: caught tap strike", 137: "counter: caught tap strike (better)", 138: "counter: caught far strike", 139: "counter: caught far strike (better)", 140: "counter to hold B strike", 141: "counter to Up+hold B strike", 142: "counter to run+B", 143: "counter to run+L/R+B", 144: "counter to corner run+B", 130: "strike across ropes (into ring)", 131: "strike from apron to floor", 132: "strike at apron", 133: "strike at apron (other side)", 128: "running strike into corner", 157: "get-up attack",
    118: "strike tap B close", 120: "strike tap B far", 119: "strike d-pad+tap B close",
    121: "strike d-pad+tap B far", 123: "strike hold B", 124: "strike Up+hold B", 125: "run+B",
    126: "run+L/R+B", 0: "weak front grapple A", 1: "weak front grapple Up+A",
    2: "weak front grapple Down+A", 3: "weak front grapple B", 8: "weak front grapple Up+B",
    13: "weak front grapple Down+B", 18: "strong front grapple A", 23: "strong front grapple Up+A",
    28: "strong front grapple Down+A", 33: "strong front grapple B", 38: "strong front grapple Up+B",
    43: "strong front grapple Down+B", 61: "weak rear grapple A", 62: "weak rear grapple dir+A",
    63: "weak rear grapple B", 64: "weak rear grapple dir+B", 65: "strong rear grapple A",
    66: "strong rear grapple dir+A", 67: "strong rear grapple B", 68: "strong rear grapple dir+B",
    74: "Irish whip tap A", 75: "Irish whip Up+tap A", 76: "Irish whip hold A",
    77: "Irish whip Up+hold A", 58: "special (front)", 69: "special (rear)",
    158: "face up: A near head", 162: "face up: A near legs", 154: "face up: B",
    84: "face down: A near head", 163: "face down: A near legs", 155: "face down: B",
    166: "turnbuckle, opponent standing", 167: "turnbuckle, opponent down",
    173: "outside: run to floor", 172: "outside: run to post", 176: "outside: apron to floor",
    178: "outside: slingshot to floor", 179: "outside: slingshot to inside (guess)",
    127: "run + L/R (roll?)", 177: "apron springboard to floor (guess)",
    83: "corner tap A+A", 88: "corner hold A+A", 93: "corner hold A+Up+A",
    98: "corner hold A+Down+A", 128: "corner run+B",
}
# Grapple moves are followed by 4 reversal variants (moves.md).
for _base in [3, 8, 13, 18, 23, 28, 33, 38, 43]:
    for _k in range(1, 5):
        SLOT_NAMES.setdefault(_base + _k, f"{SLOT_NAMES[_base]} reversal {_k}")
PART_NAMES = ["attacker", "receiver"]


def anim_labels(rom, arc, sym_db=None):
    """Where each animation is used: wrestler move-set slots (from the ROM), and states
    that start it with a constant id (from the sym index, when it has been built)."""
    labels = {}

    def add(anim, text):
        labels.setdefault(anim, [])
        if text not in labels[anim]:
            labels[anim].append(text)

    users = {}
    first = struct.unpack_from(f">{MOVE_SLOTS}h", rom, MOVE_ANIM_FIRST - OVL_B_VRAM + OVL_B_ROM)
    for g, (group, names) in enumerate(ROSTER):
        for i, name in enumerate(names):
            base = moveset_file(rom, g, i)
            lst = arc.read(base + 2)
            vals = struct.unpack_from(f">{len(lst) // 2}h", lst, 0)
            for slot in range(MOVE_SLOTS):
                end = first[slot + 1] if slot + 1 < MOVE_SLOTS else len(vals)
                for part, v in enumerate(vals[first[slot]:end]):
                    if v <= 0:
                        continue
                    who = PART_NAMES[part] if part < 2 else f"part {part}"
                    key = f"slot {slot} {SLOT_NAMES.get(slot, '')} ({who})".replace("  ", " ")
                    users.setdefault(v + ANIM_LIST_BIAS, {}).setdefault(key, []).append(name)
    for anim, keys in users.items():
        for key, who in keys.items():
            add(anim, f"{key}: {', '.join(who)}")
    # wrestler_base_anim's table (animation_states.md): rows of 4
    rows = ["walk, style", "idle, style", "injured walk, region", "injured idle, region",
            "block, style"] + ["hit while blocking, style"] * 6
    table = struct.unpack_from(">44h", rom, BASE_ANIMS - OVL_B_VRAM + OVL_B_ROM)
    for r, row in enumerate(rows):
        for k in range(4):
            add(table[4 * r + k], f"code: wrestler_base_anim {row} {k}")
    if sym_db and os.path.exists(sym_db):
        import re
        import sqlite3
        db = sqlite3.connect(sym_db)
        names = dict(db.execute("select addr, name from func"))
        for addr, text in db.execute("select addr, text from code"):
            for m in re.finditer(r"wrestler_set_anim\(([^;]*?)\);|anim_id = (\d{4});", text, re.S):
                for tok in re.findall(r"\b(0x[0-9A-Fa-f]+|\d{4})\b", m.group(1) or m.group(2)):
                    n = int(tok, 0)
                    if ANIM_FIRST <= n <= FILE_COUNT:
                        add(n, "code: " + names.get(addr, hex(addr)))
    return labels


MOVESET_TABLE = 0x800E74CC     # ptr per roster group -> s16 base file per wrestler (moves.md)


def moveset_file(rom, group, index):
    o = MOVESET_TABLE - OVL_B_VRAM + OVL_B_ROM
    ptr = struct.unpack_from(">I", rom, o + 4 * group)[0]
    return struct.unpack_from(">h", rom, ptr - OVL_B_VRAM + OVL_B_ROM + 2 * index)[0]


# --- arena --------------------------------------------------------------------------

ARENA_RING_MESHES = [995, 996, 997, 998, 999]  # arena_build: ring sides, apron, canvas, logo
ARENA_TABLES = 0x800E62C4                      # word table used by arena_build / arena_crowd_build
ARENA_SIDE_MESH = 985                          # barrier, drawn once per side


def arena_pieces(rom):
    """Static scenery of each of the 4 arenas as {mesh, tex, pal, side}; side is None for
    pieces drawn once, else 0-3 for pieces repeated per ring side (rotated 90 deg each).
    Follows arena_build (0x80091100) and arena_crowd_build (0x800917B0), 2-wrestler build;
    see _re_docs/individual_games/wcw_world_tour/arena.md."""
    def u16s(addr, n):
        return list(struct.unpack_from(f">{n}H", rom, addr - OVL_B_VRAM + OVL_B_ROM))

    def u32(addr):
        return struct.unpack_from(">I", rom, addr - OVL_B_VRAM + OVL_B_ROM)[0]

    def u8(addr):
        return rom[addr - OVL_B_VRAM + OVL_B_ROM]

    arc = Archive(rom)
    out = []
    for arena in range(4):
        pieces = []
        for mesh, pal in zip(ARENA_RING_MESHES, u16s(u32(ARENA_TABLES + 132 + 4 * arena), 5)):
            pieces.append({"mesh": mesh, "tex": pal + 1, "pal": pal, "side": None})
        flag = u8(0x800E6288 + arena)
        side_meshes = u16s(0x800E6294 if flag else 0x800E628C, 3)
        side_texs = u16s(0x800E62A4 if flag else 0x800E629C, 3)
        side_pals = u16s(0x800E62AC, 12)
        wall_pals = u16s(u32(0x800E6360 + 4 * arena), 2)
        lifts = [u8(0x800E6284 + i) for i in range(3)]  # y offset of each corner piece
        for side in range(4):
            for i in range(3):
                pieces.append({"mesh": side_meshes[i], "tex": side_texs[i],
                               "pal": side_pals[3 * side + i], "side": side, "dy": lifts[i]})
            for mesh, pal in zip(u16s(ARENA_TABLES + 148, 2), wall_pals):  # floor 1000, wall 988
                pieces.append({"mesh": mesh, "tex": pal + 1, "pal": pal, "side": side})
            pieces.append({"mesh": ARENA_SIDE_MESH, "tex": wall_pals[1] + 1, "pal": wall_pals[1], "side": side})
        n = u8(ARENA_TABLES + 172 + arena)
        crowd = u16s(u32(ARENA_TABLES + 176 + 4 * arena), n)
        crowd_pals = u16s(u32(0x800E6384 + 4 * arena), n)
        for mesh, pal in zip(crowd, crowd_pals):
            if pal and is_mesh(arc.read(mesh)):
                pieces.append({"mesh": mesh, "tex": pal + 1, "pal": pal, "side": None})
        out.append(pieces)
    return out


# --- viewer -------------------------------------------------------------------------

def build_viewer(rom, out_path, sym_db=None):
    arc = Archive(rom)
    main = Main(rom)
    meshes, textures, roster = {}, {}, []
    for g, (group, names) in enumerate(ROSTER):
        for i, name in enumerate(names):
            cs = costumes(main, g, i)
            for c in cs:
                for node in c["nodes"]:
                    if node["mesh"] not in meshes:
                        meshes[node["mesh"]] = decode_mesh(arc.read(node["mesh"]))
                    key = f'{node["tex"]}_{node["pal"]}'
                    if key not in textures:
                        w, h, rgba, wrap = decode_texture(arc.read(node["tex"]),
                                                          arc.read(node["pal"]))
                        textures[key] = {"w": w, "h": h, "wrap": wrap,
                                         "png": base64.b64encode(png(w, h, rgba)).decode()}
                    node["texkey"] = key
            roster.append({"name": name, "group": group, "costumes": cs})
    meta = arc.read(ANIM_META)
    blob = bytearray()
    anims = []
    for n in range(ANIM_FIRST, FILE_COUNT + 1):
        d = arc.read(n)
        anims.append([n, len(blob), len(d), meta[32 * (n - ANIM_FIRST):32 * (n - ANIM_FIRST + 1)].hex()])
        blob += d
    def add_tex(tex, pal):
        key = f'{tex}_{pal}'
        if key not in textures:
            w, h, rgba, wrap = decode_texture(arc.read(tex), arc.read(pal))
            textures[key] = {"w": w, "h": h, "wrap": wrap,
                             "png": base64.b64encode(png(w, h, rgba)).decode()}
        return key

    arenas = []
    for arena in arena_pieces(rom):
        pieces = []
        for p in arena:
            if p["mesh"] not in meshes:
                meshes[p["mesh"]] = decode_mesh(arc.read(p["mesh"]))
            pieces.append({"mesh": p["mesh"], "texkey": add_tex(p["tex"], p["pal"]), "side": p["side"],
                           "dy": p.get("dy", 0)})
        arenas.append(pieces)
    data = {"skeleton": skeleton(main), "seams": SEAMS, "roster": roster, "arenas": arenas,
            "meshes": {str(k): v for k, v in meshes.items()}, "textures": textures,
            "anims": anims, "labels": {str(k): v for k, v in anim_labels(rom, arc, sym_db).items()}}
    here = os.path.dirname(os.path.abspath(__file__))
    page = open(os.path.join(here, "wcw_viewer.html"), encoding="utf-8").read()
    page = page.replace("/*DATA*/null", json.dumps(data, separators=(",", ":")))
    page = page.replace("/*ANIMBLOB*/\"\"", '"' + base64.b64encode(bytes(blob)).decode() + '"')
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(page)
    print(f"wrote {out_path}: {len(roster)} wrestlers, {len(meshes)} meshes, "
          f"{len(textures)} textures, {len(anims)} animations")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("roster")
    a = sub.add_parser("anim")
    a.add_argument("id", type=int)
    a.add_argument("--frames", action="store_true")
    v = sub.add_parser("viewer")
    v.add_argument("out")
    v.add_argument("--sym", default=os.path.join("_tools", "sym", "wcw.sqlite"),
                   help="sym index; adds the states that start each animation (optional)")
    args = ap.parse_args()
    rom = open(args.rom, "rb").read()
    if args.cmd == "roster":
        m = Main(rom)
        for g, (group, names) in enumerate(ROSTER):
            for i, name in enumerate(names):
                for k, c in enumerate(costumes(m, g, i)):
                    print(f"{group:3} {i:2} {name:20} costume {k}: body {c['body_type']} head {c['head']} "
                          f"meshes {[n['mesh'] for n in c['nodes']]}")
    elif args.cmd == "anim":
        an = decode_anim(Archive(rom).read(args.id))
        print(f"anim {args.id}: {an['frames']} frames, {len(an['keys'])} keyframes, "
              f"yaw {an['yaw']:#x}, pose class {an['pose']}")
        if args.frames:
            for k, joints in enumerate(an["keys"]):
                print(f"key {k}: " + " ".join(
                    f"j{j + 1}={tuple(round(x) for x in jt['rot'])}"
                    + (f"@{tuple(jt['pos'])}" if jt["pos"] else "") for j, jt in enumerate(joints)))
    elif args.cmd == "viewer":
        build_viewer(rom, args.out, args.sym)


if __name__ == "__main__":
    main()
