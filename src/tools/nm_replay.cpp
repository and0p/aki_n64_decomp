// nm_replay: runs the No Mercy decompilation over an emulator trace and writes its own trace of
// the same regions, for tools/trace/diff.py to compare.
//
//     nm_replay IN.akt OUT.akt [--step | --free] [--from K] [--frames N] [--rom ROM.z64] [--keep-going]
//
// --step (default) starts every frame from the recorded state of the frame before, so each frame
// is checked on its own; --free starts once from record K (default 0) and then runs on its own
// state. In both modes the host supplies what the decompilation does not compute yet, from the
// trace's next record: the pads as the game read them and rng_main's state (the camera also draws
// from it, outside the match logic). The animations stream from the ROM (--rom, big-endian .z64).
// Record k of the output is the state after k frames, like the input; record 0 is copied.
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../nomercy/nm_funcs.h"
#include "../nomercy/nm_host.h"

using namespace nm;

namespace {

struct Region {
    std::string name;
    u32 addr = 0, size = 0, off = 0;
};

struct Trace {
    std::string header;
    std::vector<Region> regions;
    std::vector<u8> image;
    u32 frame_size = 0;
    std::vector<std::vector<u8>> frames;  // each: u32 tick, u32 emu frame, region bytes
};

u32 rd32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

// The header is JSON written by tools/trace/trace.py; only the region list is needed here.
bool parse_regions(const std::string &h, std::vector<Region> &out) {
    size_t p = h.find("\"regions\"");
    if (p == std::string::npos)
        return false;
    size_t end = h.find(']', p);
    u32 off = 0;
    while (true) {
        size_t a = h.find('{', p);
        if (a == std::string::npos || a > end)
            break;
        size_t b = h.find('}', a);
        std::string obj = h.substr(a, b - a);
        auto field = [&](const char *key) -> std::string {
            size_t k = obj.find(std::string("\"") + key + "\"");
            if (k == std::string::npos)
                return "";
            k = obj.find(':', k) + 1;
            while (obj[k] == ' ')
                k++;
            size_t e = k;
            if (obj[k] == '"') {
                e = obj.find('"', k + 1);
                return obj.substr(k + 1, e - k - 1);
            }
            while (e < obj.size() && obj[e] != ',' && obj[e] != '}')
                e++;
            return obj.substr(k, e - k);
        };
        Region r;
        r.name = field("name");
        r.addr = (u32)std::strtoul(field("addr").c_str(), nullptr, 10);
        r.size = (u32)std::strtoul(field("size").c_str(), nullptr, 10);
        r.off = off;
        off += r.size;
        out.push_back(r);
        p = b;
    }
    return !out.empty();
}

bool read_trace(const char *path, Trace &t) {
    FILE *f = std::fopen(path, "rb");
    if (!f)
        return false;
    std::vector<u8> d;
    u8 buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    if (d.size() < 16 || std::memcmp(d.data(), "AKITRC1\0", 8) != 0)
        return false;
    u32 hl = rd32(&d[8]);
    t.header.assign((const char *)&d[12], hl);
    size_t p = 12 + hl;
    u32 il = rd32(&d[p]);
    t.image.assign(d.begin() + p + 4, d.begin() + p + 4 + il);
    p += 4 + il;
    if (!parse_regions(t.header, t.regions))
        return false;
    for (const Region &r : t.regions)
        t.frame_size += r.size;
    while (p + 8 + t.frame_size <= d.size()) {
        t.frames.emplace_back(d.begin() + p, d.begin() + p + 8 + t.frame_size);
        p += 8 + t.frame_size;
    }
    return true;
}

const Region *region(const Trace &t, const char *name) {
    for (const Region &r : t.regions)
        if (r.name == name)
            return &r;
    return nullptr;
}

// Copies a recorded region of frame k into the RDRAM image.
void load_region(const Trace &t, size_t k, const Region &r) {
    std::memcpy(g_ram + ram_off(r.addr), t.frames[k].data() + 8 + r.off, r.size);
}

const Trace *g_in;
size_t g_next;  // the record the frame being run ends at
size_t g_frame_running;
FILE *g_out;

// The ROM, for the animation keyframes (anim_stream_fetch's DMAs).
std::vector<u8> g_rom;

void rom_read(u32 rom_addr, u8 *dst, u32 n) {
    if ((size_t)rom_addr + n > g_rom.size()) {
        std::fprintf(stderr, "frame %zu: ROM read past the end: 0x%X + %u\n", g_frame_running, rom_addr, n);
        std::exit(2);
    }
    std::memcpy(dst, g_rom.data() + rom_addr, n);
}

// --keep-going (with --step): a frame that reaches unported code is written as recorded (so it
// passes the diff) and the replay goes on; the unported reasons are counted and printed at the end.
bool g_keep_going;
std::jmp_buf g_frame_jmp;
std::map<std::string, size_t> g_unported_counts;
size_t g_multi_tick_frames;  // records run as several ticks (the clock went up by 2-4)

void on_unported(const char *func, const char *what) {
    if (g_keep_going) {
        g_unported_counts[std::string(func) + ": " + what]++;
        std::longjmp(g_frame_jmp, 1);
    }
    std::fprintf(stderr, "frame %zu: unported: %s: %s\n", g_frame_running, func, what);
    if (g_out)
        std::fclose(g_out);
    std::exit(2);
}

// One match_tick; false when it reached unported code under --keep-going.
bool run_tick() {
    if (g_keep_going && setjmp(g_frame_jmp))
        return false;
    match_tick();
    return true;
}

void write_frame(FILE *f, const Trace &t, size_t k) {
    std::fwrite(t.frames[k].data(), 1, 8, f);  // the recorded tick and emulator frame
    for (const Region &r : t.regions)
        std::fwrite(g_ram + ram_off(r.addr), 1, r.size, f);
}

}  // namespace

int main(int argc, char **argv) {
    const char *in = nullptr, *out = nullptr, *rom = nullptr;
    bool step = true;
    size_t from = 0, count = (size_t)-1;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--step"))
            step = true;
        else if (!std::strcmp(argv[i], "--free"))
            step = false;
        else if (!std::strcmp(argv[i], "--from") && i + 1 < argc)
            from = (size_t)std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc)
            count = (size_t)std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--keep-going"))
            g_keep_going = true;
        else if (!std::strcmp(argv[i], "--rom") && i + 1 < argc)
            rom = argv[++i];
        else if (!in)
            in = argv[i];
        else if (!out)
            out = argv[i];
    }
    if (!in || !out) {
        std::fprintf(stderr, "usage: nm_replay IN.akt OUT.akt [--step | --free] [--from K] [--frames N] [--rom ROM.z64]\n");
        return 1;
    }
    if (rom) {
        FILE *f = std::fopen(rom, "rb");
        if (!f) {
            std::fprintf(stderr, "%s: cannot open the ROM\n", rom);
            return 1;
        }
        u8 buf[1 << 16];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
            g_rom.insert(g_rom.end(), buf, buf + n);
        std::fclose(f);
        if (g_rom.size() < 4 || g_rom[0] != 0x80 || g_rom[1] != 0x37) {
            std::fprintf(stderr, "%s: not a big-endian (.z64) ROM\n", rom);
            return 1;
        }
        g_host.rom_read = rom_read;
    }
    static Trace t;
    if (!read_trace(in, t) || t.image.size() != RAM_SIZE || t.frames.empty()) {
        std::fprintf(stderr, "%s: not a trace with a RAM image\n", in);
        return 1;
    }
    g_in = &t;
    std::vector<u8> ram(t.image);
    g_ram = ram.data();
    set_unported_hook(on_unported);

    // The output header: the input's, with the replay's mode; no image.
    std::string h = t.header;
    size_t brace = h.rfind('}');
    h = h.substr(0, brace) + ", \"replay\": \"" + (step ? "step" : "free") + "\"}";
    g_out = std::fopen(out, "wb");
    if (!g_out)
        return 1;
    u32 hl = (u32)h.size(), zero = 0;
    std::fwrite("AKITRC1\0", 1, 8, g_out);
    std::fwrite(&hl, 4, 1, g_out);
    std::fwrite(h.data(), 1, h.size(), g_out);
    std::fwrite(&zero, 4, 1, g_out);

    const Region *pads = region(t, "pads");
    const Region *misc = region(t, "main_misc");
    size_t last = t.frames.size() - 1;
    if (count != (size_t)-1 && from + count < last)
        last = from + count;
    // Records before `from` are copied so the indices line up.
    for (size_t k = 0; k <= from && k < t.frames.size(); k++)
        std::fwrite(t.frames[k].data(), 1, t.frames[k].size(), g_out);
    for (const Region &r : t.regions)
        load_region(t, from, r);
    state_import();
    for (size_t k = from; k < last; k++) {
        g_frame_running = k + 1;
        g_next = k + 1;
        if (step && k != from) {
            for (const Region &r : t.regions)
                load_region(t, k, r);
            state_import();
        }
        // What the host supplies for this frame.
        load_region(t, k + 1, *pads);
        load_region(t, k, *misc);
        state_import_external();
        // A record of an akiemu --tick-hook trace holds whole ticks, almost always one; when two
        // finished inside one emulator step, the match clock (counted once a tick in play) shows it.
        u32 dt = rd32(t.frames[k + 1].data()) - rd32(t.frames[k].data());
        int ticks = (dt >= 2 && dt <= 4) ? (int)dt : 1;
        bool ok = true;
        for (int n = 0; n < ticks && ok; n++)
            ok = run_tick();
        if (ticks > 1)
            g_multi_tick_frames++;
        if (!ok) {
            std::fprintf(stderr, "skipped frame %zu\n", k + 1);
            std::fwrite(t.frames[k + 1].data(), 1, t.frames[k + 1].size(), g_out);
            continue;
        }
        state_export();
        write_frame(g_out, t, k + 1);
    }
    if (g_multi_tick_frames)
        std::printf("%zu records ran as several ticks\n", g_multi_tick_frames);
    for (const auto &e : g_unported_counts)
        std::printf("%6zu frames: %s\n", e.second, e.first.c_str());
    std::fclose(g_out);
    std::printf("replayed frames %zu-%zu (%s)\n", from + 1, last, step ? "step" : "free");
    return 0;
}
