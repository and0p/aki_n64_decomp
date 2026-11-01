"""Wrestler models and animations of WCW/nWo Revenge (US), for the same previewer as World Tour.

Format notes: _re_docs/individual_games/wcw_revenge/README.md (models section); the mesh,
texture and animation formats are World Tour's (_re_docs/file_formats/).

    python tools/rom/revenge_models.py ROM roster
    python tools/rom/revenge_models.py ROM viewer OUT.html

`viewer` writes one self-contained HTML page (the World Tour template) with every Revenge
roster slot, costume and animation. It holds ROM-derived data, so keep it out of git (for
example under _tools/).
"""
import argparse
import base64
import json
import re
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from aki_archive import Archive  # noqa: E402
from wcw_models import Main, decode_mesh, decode_texture, png, KEYFRAME_SIZE, SEAMS  # noqa: E402

JOINTS = 19
BIND_XY = 0x800307C0       # same data as World Tour's 0x8002D3A0
JOINT_ORDER = 0x800307E9
JOINT_PARENT = 0x800307FC
BONE_PART = 0x80031089     # s8 per bone 1..19: body part index, -1 = head list
DESCS = 0x80031998         # ptr per roster group -> u16 per wrestler; & 0x7F = costume set
HEADS = 0x80032B44         # ptr per roster group -> ptr per wrestler -> head variant ptrs
COSTUME_SETS = 0x800360F0  # ptr per costume set -> ptr per costume -> ptr per texture variant
BODY_PARTS = 0x800317F0    # ptr per body type -> u16 mesh file per body part
COSTUME_COUNT = 0x8003673C  # u16 per costume set: number of costumes (model_load caps the choice)
GROUPS = 10
ARENA_TABLE = 0x80036C68   # see _re_docs/individual_games/wcw_revenge/README.md (arenas)
ARENAS = 6
# Identified from the renders (logos on the mat, entrance and banners).
ARENA_NAMES = ["Nitro", "Souled Out", "SuperBrawl", "Bash at the Beach", "Halloween Havoc", "Starrcade"]
ATTIRES = 0x80036710       # ptr per roster group -> ptr per wrestler -> 4 attires x 4 bytes (sub_8000B630)
SELECT_GROUPS = 0x8003A4F8  # 11 x (ptr to record pointers, u16 count): select-screen lists
SKIN_FILES = 0x8003086C     # u16 skin-ramp file per bone (0 = no skin recolour)
COLOUR_RAMPS = 2641         # 16 colours x 16 shades, RGBA5551 (sub_8000B884)
KEY_MASKS = (0xF801, 0x07C1, 0x003F)  # recolour key fields: skin, colour 1, colour 2
KEY_SHIFTS = (11, 6, 1)


def skeleton(main):
    xy = main.s8(BIND_XY, 2 * (JOINTS + 1))
    return {"bind": [[xy[2 * j], xy[2 * j + 1], 0] for j in range(JOINTS + 1)],
            "order": main.u8(JOINT_ORDER, JOINTS),
            "parent": main.u8(JOINT_PARENT, JOINTS + 1)}


def group_size(main, g):
    """Wrestlers in group g: the head pointer arrays of consecutive groups are contiguous."""
    a, b = main.u32(HEADS + 4 * g), main.u32(HEADS + 4 * (g + 1))
    return (b - a) // 4 if g + 1 < GROUPS else 4


def list_until_zero(main, ptr, limit=16):
    out = []
    for k in range(limit):
        p = main.u32(ptr + 4 * k)
        if p == 0 or not 0x80000400 <= p < 0x8003BB70:
            break
        out.append(p)
    return out


def attire_defaults(main, group, index):
    """The wrestler's 4 default attires (sub_8000B630): (head variant, costume, colour 1, colour 2).

    Byte 0 = head variant + 1, byte 1 = costume number + 1 in the body build's shared list,
    byte 3 / byte 2 = colour 1 / colour 2 + 1 (0 = keep the texture's colours, as 15 and up do)."""
    base = main.u32(main.u32(ATTIRES + 4 * group) + 4 * index)
    out = []
    for a in range(4):
        b = main.u8(base + 4 * a, 4)
        out.append((max(b[0] - 1, 0), max(b[1] - 1, 0), (b[3] - 1) & 0xFF, (b[2] - 1) & 0xFF))
    return out


def costumes(main, group, index):
    """Nodes of the wrestler's 4 default attires (func_8000C91C, func_8000BDD0, func_8000C1B8).

    The costume number indexes the body build's shared list (desc & 0x7F), capped by its count;
    the head variant indexes the wrestler's head list, also capped. Head field [11] is the skin
    tone: it picks the texture record of the costume and the skin ramp (sub_8000B884)."""
    bone_part = main.s8(BONE_PART, JOINTS)
    desc = main.u16(main.u32(DESCS + 4 * group) + 2 * index, 1)[0]
    cset = main.u32(COSTUME_SETS + 4 * (desc & 0x7F))
    heads = list_until_zero(main, main.u32(main.u32(HEADS + 4 * group) + 4 * index))
    count = main.u16(COSTUME_COUNT + 2 * (desc & 0x7F), 1)[0]
    result = []
    for attire, (hv, c, col1, col2) in enumerate(attire_defaults(main, group, index)):
        c = c if c < count else 0
        hv = hv if hv < len(heads) else 0
        records = list_until_zero(main, main.u32(cset + 4 * c))
        if not records:
            continue
        body_type = main.u16(records[0], 1)[0] & 0x7F
        body = main.u16(main.u32(BODY_PARTS + 4 * body_type), 17)
        head = main.u16(heads[hv], 12)
        skin = head[11]
        rec = main.u16(records[skin] if skin < len(records) else records[0], 35)
        nodes = []
        for bone in range(1, JOINTS + 1):
            part = bone_part[bone - 1]
            if part >= 0:
                nodes.append({"bone": bone, "mesh": body[part],
                              "tex": rec[2 + 2 * part], "pal": rec[1 + 2 * part], "recolour": True})
            elif bone == 4:
                nodes.append({"bone": 4, "mesh": head[0], "tex": head[4], "pal": head[3], "recolour": True})
            else:
                nodes.append({"bone": 5, "mesh": head[1], "tex": head[6], "pal": head[5], "recolour": False})
        if head[2]:
            nodes.append({"bone": 5, "mesh": head[2], "tex": head[8], "pal": head[7], "recolour": False})
        result.append({"body_type": body_type, "head": hv, "costume": c, "attire": attire,
                       "colours": [skin, col1, col2], "nodes": nodes})
    return result


def recolour(palette, bone, colours, main, arc):
    """Apply Revenge's palette recolouring (sub_8000B884).

    A 32-entry palette is 16 colours plus 16 keys: a key inside the green field (0x07C1) gives
    its colour a shade of colour 1 from the ramp file 2641, one inside the blue field (0x003F) a
    shade of colour 2. Then, for bones with a skin file, every colour that is itself a pure red
    value (only 0xF801 bits) is a skin placeholder and becomes a shade of the skin tone."""
    n = len(palette) // 2
    pal = list(struct.unpack(">%dH" % n, palette))
    if n >= 17:
        keys = pal[16:32]
        ramps = struct.unpack(">256H", arc.read(COLOUR_RAMPS)[:512])
        for field in (1, 2):
            colour = colours[field]
            if colour >= 15:
                continue
            for i, key in enumerate(keys):
                if key >= 2 and not key & ~KEY_MASKS[field] & 0xFFFF:
                    shade = ((key & KEY_MASKS[field]) >> KEY_SHIFTS[field]) >> 1
                    pal[i] = ramps[16 * colour + 15 - shade]
    skin_file = main.u16(SKIN_FILES + 2 * bone, 1)[0] if bone < 23 else 0
    if skin_file:
        skin = arc.read(skin_file)
        tones = struct.unpack(">%dH" % (len(skin) // 2), skin)
        for i in range(min(16, n)):
            c = pal[i]
            if c >= 2 and not c & ~KEY_MASKS[0] & 0xFFFF:
                k = 16 * colours[0] + (((c & KEY_MASKS[0]) >> KEY_SHIFTS[0]) >> 1)
                if k < len(tones):
                    pal[i] = tones[k]
    return struct.pack(">%dH" % n, *pal)


def roster_names(main):
    """Roster word -> display name, from the select-screen lists (records: u16 roster word,
    u8 profile, ...; the name string pointer at +8)."""
    names = {}
    for g in range(11):
        ptr, count = main.u32(SELECT_GROUPS + 8 * g), main.u16(SELECT_GROUPS + 8 * g + 4, 1)[0]
        for k in range(count):
            rec = main.u32(ptr + 4 * k)
            word = main.u16(rec, 1)[0]
            try:
                raw = bytes(main.u8(main.u32(rec + 8), 40))
                text = raw.split(b"\0")[0].decode("latin1")
            except Exception:  # noqa: BLE001
                text = None
            if text:
                names.setdefault(word, " ".join(re.sub(r"\{[^}]*\}|[<>]", "", text).split()))
    return names


def arena_pieces(main, arc):
    """Static scenery of each of the 6 arenas as {mesh, tex, pal, side}; side None = drawn once,
    0-3 = repeated per ring side (rotated 90 deg). From func_8000E920 (ring, sides, corners)
    and func_8000EF90 (crowd, first palette set); textures follow scenery_object's
    "texture = palette + 1" rule except the corner pieces, which name both."""
    from wcw_models import is_mesh
    t = ARENA_TABLE
    ring = main.u16(t, 6)
    sides = main.u16(t + 2 * 18, 3)
    fillers = main.u16(t + 2 * 74, 3)
    filler_pals = main.u16(t + 2 * 78, 3)
    corners = main.u16(0x80036C50, 3)
    corner_tex = main.u16(0x80036C58, 3)
    corner_pal = main.u16(0x80036C60, 3)
    counts = main.u8(t + 68, ARENAS)
    out = []
    for a in range(ARENAS):
        pieces = []
        rp = main.u32(t + 2 * (6 + 2 * a))
        if rp:
            for mesh, pal in zip(ring, main.u16(rp, 6)):
                if pal:
                    pieces.append({"mesh": mesh, "tex": pal + 1, "pal": pal, "side": None})
        side_pals = main.u16(main.u32(t + 2 * (22 + 2 * a)), 3)
        for side in range(4):
            for mesh, pal in zip(sides, side_pals):
                pieces.append({"mesh": mesh, "tex": pal + 1, "pal": pal, "side": side})
            if side:  # the entrance side (+z) has the aisle instead of the middle barrier
                pieces.append({"mesh": fillers[0], "tex": filler_pals[0] + 1, "pal": filler_pals[0], "side": side})
            for mesh, tex, pal in zip(corners, corner_tex, corner_pal):
                if tex:
                    pieces.append({"mesh": mesh, "tex": tex, "pal": pal, "side": side})
        n = counts[a]
        crowd = main.u16(main.u32(t + 2 * (38 + 2 * a)), n)
        pals = main.u16(main.u32(t + 2 * (50 + 2 * a)), n)
        for mesh, pal in zip(crowd, pals):
            if pal and is_mesh(arc.read(mesh)):
                pieces.append({"mesh": mesh, "tex": pal + 1, "pal": pal, "side": None})
        out.append(pieces)
    return out


def is_anim(d):
    if len(d) < 4 + KEYFRAME_SIZE or (len(d) - 4) % KEYFRAME_SIZE:
        return False
    k = (len(d) - 4) // KEYFRAME_SIZE
    fr = struct.unpack_from(">I", d, 0)[0] & 0xFFFF
    return k in (fr // 2 + 1, fr // 2 + 2)


def build_viewer(rom, out_path):
    main, arc = Main(rom), Archive(rom)
    roster, meshes, textures = [], {}, {}
    names = roster_names(main)
    for g in range(GROUPS):
        for i in range(group_size(main, g)):
            try:
                cs = costumes(main, g, i)
            except (struct.error, IndexError, ValueError):
                continue
            good = []
            for c in cs:
                try:
                    for node in c["nodes"]:
                        if node["mesh"] not in meshes:
                            meshes[node["mesh"]] = decode_mesh(arc.read(node["mesh"]))
                        pal = arc.read(node["pal"])
                        if node.pop("recolour"):
                            pal = recolour(pal, node["bone"], c["colours"], main, arc)
                        key = f'{node["tex"]}_{node["pal"]}_{hash(pal) & 0xFFFFFFFF:08x}'
                        if key not in textures:
                            w, h, rgba, wrap = decode_texture(arc.read(node["tex"]), pal)
                            textures[key] = {"w": w, "h": h, "wrap": wrap,
                                             "png": base64.b64encode(png(w, h, rgba)).decode()}
                        node["texkey"] = key
                    good.append(c)
                except Exception as e:  # noqa: BLE001 - a bad guess at a table shows up here
                    print(f"group {g} wrestler {i} costume {c['costume']}: {e}")
            if good:
                word = (g << 8) | (i + 1)
                label = names.get(word, f"group {g} #{i + 1}")
                roster.append({"name": f"{label} ({word:#05x})", "group": str(g), "costumes": good})
    blob, anims = bytearray(), []
    for n in range(1, arc.count + 1):
        d = arc.read(n)
        if is_anim(d):
            anims.append([n, len(blob), len(d), "00" * 32])
            blob += d
    arenas = []
    for arena in arena_pieces(main, arc):
        pieces = []
        for p in arena:
            if p["mesh"] not in meshes:
                meshes[p["mesh"]] = decode_mesh(arc.read(p["mesh"]))
            key = f'{p["tex"]}_{p["pal"]}'
            if key not in textures:
                w, h, rgba, wrap = decode_texture(arc.read(p["tex"]), arc.read(p["pal"]))
                textures[key] = {"w": w, "h": h, "wrap": wrap,
                                 "png": base64.b64encode(png(w, h, rgba)).decode()}
            pieces.append({"mesh": p["mesh"], "texkey": key, "side": p["side"], "dy": 0})
        arenas.append(pieces)
    data = {"skeleton": skeleton(main), "seams": SEAMS, "roster": roster, "arenas": arenas,
            "arenaNames": ARENA_NAMES,
            "meshes": {str(k): v for k, v in meshes.items()}, "textures": textures,
            "anims": anims, "labels": {}}
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
    v = sub.add_parser("viewer")
    v.add_argument("out")
    args = ap.parse_args()
    rom = open(args.rom, "rb").read()
    m = Main(rom)
    if args.cmd == "roster":
        for g in range(GROUPS):
            for i in range(group_size(m, g)):
                for c in costumes(m, g, i):
                    meshes = [n["mesh"] for n in c["nodes"]]
                    print(f"group {g} #{i + 1:2} attire {c['attire']}: costume {c['costume']:2} "
                          f"head {c['head']} colours {c['colours']} body {c['body_type']:2} meshes {meshes}")
    else:
        build_viewer(rom, args.out)


if __name__ == "__main__":
    main()
