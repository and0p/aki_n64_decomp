"""Dump World Tour's CPU AI data tables from the ROM as JSON.

    py tools/rom/wcw_ai.py ROM.z64 OUT.json

Writes the skill per difficulty level, the perception error limits and every weighted
mode table used by the AI decision code (see _re_docs/individual_games/wcw_world_tour/ai.md).
Nothing ROM-derived is committed; the JSON goes wherever the converter output lives.
"""
import json
import struct
import sys

OVL_B_VRAM, OVL_B_ROM = 0x80090000, 0xA69570

# Weighted mode tables: address -> where the decision code uses it.
MODE_TABLES = {
    0x800E7568: "same area, opponent in reach (nearest band)",
    0x800E75A4: "same area, opponent in reach (far band)",
    0x800E75D4: "same area, opponent in reach (middle band)",
    0x800E760C: "same area, standing opponent in reach (half the time)",
    0x800E762C: "same area, opponent in an unhandled situation",
    0x800E7638: "self in the ring, opponent on the floor",
    0x800E765C: "self in the ring, opponent on the floor, ring-out count under 5",
    0x800E7674: "self on the apron, opponent in the ring, not allowed in",
    0x800E768C: "self on the floor, opponent in the ring",
    0x800E76A4: "self on the floor, opponent on the apron",
    0x800E76BC: "opponent attempting a grapple, out of reach",
    0x800E76E0: "opponent attempting a grapple, mid range",
    0x800E7718: "opponent attempting a grapple, close",
    0x800E7730: "opponent attempting a strong grapple, close (skill roll)",
    0x800E775C: "opponent striking, out of reach",
    0x800E7788: "opponent down, near (skill >= 100)",
    0x800E77AC: "opponent down, near (skill < 100)",
    0x800E77D0: "opponent dazed, near (skill >= 100)",
    0x800E77E8: "opponent dazed, near (skill < 100)",
    0x800E7808: "opponent staggered, near (skill >= 100)",
    0x800E7820: "opponent staggered, near (skill < 100)",
    0x800E7838: "opponent in situation 12 (skill >= 100)",
    0x800E7850: "opponent in situation 12 (skill < 100)",
    0x800E7868: "opponent in the corner",
    0x800E7874: "opponent in the corner, low recover count",
}


def ram(rom, addr, size):
    o = addr - OVL_B_VRAM + OVL_B_ROM
    return rom[o:o + size]


def mode_table(rom, addr):
    """(bound, mode, flags) triples; ai_pick_mode stops at the first bound >= roll (0-999)."""
    out = []
    for i in range(32):
        bound, mode, flags = struct.unpack(">hhH", ram(rom, addr + 6 * i, 6))
        if bound == -1:
            break
        out.append({"upto": bound, "mode": mode, "flags": flags})
        if bound >= 1000:
            break
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    rom = open(sys.argv[1], "rb").read()
    data = {
        "skill_by_level": list(struct.unpack(">3h", ram(rom, 0x800E7560, 6))),
        "skill_jitter": 30,
        "perceive_max_error": {
            "health": struct.unpack(">f", ram(rom, 0x800E9288, 4))[0],
            "spirit": struct.unpack(">f", ram(rom, 0x800E9290, 4))[0],
            "recover_count": struct.unpack(">f", ram(rom, 0x800E9298, 4))[0],
            "distance": struct.unpack(">f", ram(rom, 0x800E92A0, 4))[0],
        },
        "mode_tables": {"0x%08X" % a: {"use": u, "entries": mode_table(rom, a)}
                        for a, u in MODE_TABLES.items()},
    }
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1)
    print("wrote %s: %d mode tables" % (sys.argv[2], len(MODE_TABLES)))


if __name__ == "__main__":
    main()
