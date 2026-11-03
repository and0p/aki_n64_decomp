"""Trace files: one header, the RAM at the first frame, then one record per frame.

    magic  b"AKITRC1\\0"
    u32    header length (little-endian), then the header as JSON:
             game, regions [{name, addr, size}], frame_size, note, inputs (the script), and
             for a library replay: mode ("step" / "free"), owned (region names the replay writes)
    u32    image length, then the image: the 8 MB of RDRAM (N64 byte order) at frame 0, or nothing
    frames each: u32 tick, u32 emulator frame, then every region's bytes in header order

`tick` is the match tick, (minutes * 60 + seconds) * 30 + frames from the match clock
(g_match_minutes, 0x8016B930); it only runs in play (phase 2), so traces are keyed on it rather
than on the emulator's frame count. Everything is ROM- and RAM-derived: traces live in
_tools/traces (git-ignored).
"""
import json
import struct

MAGIC = b"AKITRC1\0"


class Trace:
    def __init__(self, header, image, frames):
        self.header, self.image, self.frames = header, image, frames  # frames: [(tick, emu_frame, bytes)]
        self.regions = header["regions"]
        off = 0
        for r in self.regions:
            r["frame_off"] = off
            off += r["size"]

    def region(self, k, name):
        """Bytes of region `name` in frame k."""
        for r in self.regions:
            if r["name"] == name:
                return self.frames[k][2][r["frame_off"]:r["frame_off"] + r["size"]]
        raise KeyError(name)


def read(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != MAGIC:
        raise ValueError("%s: not a trace" % path)
    n = struct.unpack_from("<I", data, 8)[0]
    header = json.loads(data[12:12 + n])
    p = 12 + n
    m = struct.unpack_from("<I", data, p)[0]
    image = data[p + 4:p + 4 + m]
    p += 4 + m
    size = header["frame_size"]
    frames = []
    while p + 8 + size <= len(data):
        tick, emu = struct.unpack_from("<II", data, p)
        frames.append((tick, emu, data[p + 8:p + 8 + size]))
        p += 8 + size
    return Trace(header, image, frames)


class Writer:
    def __init__(self, path, header, image=b""):
        header = dict(header)
        header["frame_size"] = sum(r["size"] for r in header["regions"])
        self.size = header["frame_size"]
        self.f = open(path, "wb")
        h = json.dumps(header).encode()
        self.f.write(MAGIC + struct.pack("<I", len(h)) + h + struct.pack("<I", len(image)) + image)

    def frame(self, tick, emu_frame, data):
        assert len(data) == self.size
        self.f.write(struct.pack("<II", tick, emu_frame) + data)

    def close(self):
        self.f.close()
