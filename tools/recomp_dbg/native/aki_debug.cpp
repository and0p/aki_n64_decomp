// aki_debug: native half of the recomp debug mod.
//
// The mod (../mod/aki_debug.c) hooks the game's per-frame function and calls
// akidbg_frame() at its entry, on the game thread. This library counts frames,
// tells the mod when to pause (the mod then waits for retraces in osRecvMesg and
// calls akidbg_paused() after each one), and serves a small HTTP API on
// 127.0.0.1 (AKIDBG_PORT, default 5664) for pausing, stepping, reading and
// writing RDRAM, breakpoints on memory values, pad overrides and snapshots.
// See ../README.md for the endpoints.
//
// RDRAM in the recomp is stored as 32-bit words in host (little-endian) order,
// so the N64 byte at physical address p lives at rdram[p ^ 3].

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

// Must match N64Recomp's include/recomp.h.
typedef uint64_t gpr;
typedef union {
    double d;
    struct { float fl; float fh; };
    struct { uint32_t u32l; uint32_t u32h; };
    uint64_t u64;
} fpr;
typedef struct {
    gpr r[32];
    fpr f[32];
    uint64_t hi, lo;
    uint32_t* f_odd;
    uint32_t status_reg;
    uint8_t mips3_float_mode;
} recomp_context;

#define DLLEXPORT extern "C" __declspec(dllexport)

DLLEXPORT uint32_t recomp_api_version = 1;

namespace {

constexpr uint32_t kRamSize = 0x800000;  // 8 MB (neither game uses the expansion pak)

// Per-game addresses (physical unless noted). Build with -DAKI_GAME_REVENGE=1 for WCW/nWo
// Revenge; the default is World Tour. Revenge's were read from its code (2026-11-01): the
// scheduler struct is the first argument of its init (0x80000AF0), thread structs and stack
// tops come from the osCreateThread calls.
//
// A thread is kept on snapshot load as its OSThread struct plus its MIPS stack
// [stack_lo, stack_hi) (see restore_ram_keeping_threads); the game thread keeps only its struct.
struct ThreadArea { uint32_t thread, stack_lo, stack_hi; };
constexpr uint32_t kThreadSize = 0x1B0;
#if AKI_GAME_REVENGE
constexpr uint32_t kPadsAddr = 0x8006E178;  // g_pads: 4 x 12 bytes, held at +4, pressed at +6, stick at +0xA/+0xB
constexpr ThreadArea kThreads[] = {
    {0x0003BAD0, 0x0003BC80, 0x0003DC80},  // boot, then idle (id 1)
    {0x0003DEE0, 0x0003E0C0, 0x000400C0},  // scheduler (id 4, priority 120); stack assumed 0x2000
    {0x00044210, 0x000400C0, 0x000420C0},  // id 18, priority 110 (audio?); stack assumed 0x2000
    {0x000443C0, 0x000420C0, 0x000440C0},  // id 17, priority 100 (audio?); stack assumed 0x2000
    {0x000445B0, 0x00044760, 0x00046760},  // graphics (id 3, priority 70)
    {0x0006E1C0, 0x00060520, 0x00062520},  // id 3 (sound?); stack assumed 0x2000
    {0x0005B2F0, 0, 0},                    // game (id 6, priority 8): its stack is restored
};
constexpr uint32_t kGfxRequestQueue = 0x000467C0, kGfxThread = 0x800445B0;
constexpr uint32_t kSched = 0x0003DE30;          // the scheduler (OSSched-like) struct
#else
constexpr uint32_t kPadsAddr = 0x80047B20;
// As the runtime logs them at creation ("[wcw][thread] ..." in WCWRecompiled.log).
constexpr ThreadArea kThreads[] = {
    {0x000387F0, 0x000389A0, 0x0003C990},  // boot, then idle (id 1): delivers the runtime's VI and RSP events
    {0x0003CB28, 0x0003CCD8, 0x0003ECF8},  // scheduler (id 4, priority 108)
    {0x0003EDB0, 0x0003EF60, 0x00040F50},  // graphics (id 3, priority 99)
    {0x00042CC0, 0x00042E70, 0x00044E60},  // audio (id 2, priority 100)
    {0x000455E8, 0x00045798, 0x00047788},  // sound (id 11, priority 6)
    {0x00058820, 0, 0},                    // game (id 6, priority 8): its stack is restored
};
constexpr uint32_t kGfxRequestQueue = 0x00040FC0, kGfxThread = 0x8003EDB0;
constexpr uint32_t kSched = 0x0003CA78;
#endif
constexpr uint32_t kSchedInterruptQueue = kSched + 0x40, kSchedThread = 0x80000000u | (kSched + 0xB0);
constexpr uint32_t kSchedClients = kSched + 0x260;  // client list head: {next, queue}
constexpr uint32_t kRetraceMsg = 0x80000000u | kSched;  // the message the scheduler sends on retrace

struct Snapshot {
    std::vector<uint8_t> ram;  // host word order, as in memory
    recomp_context ctx;
    uint64_t frame;
};

struct Break {
    uint32_t addr = 0;
    int size = 0;        // 1, 2 or 4; 0 = none
    int kind = 0;        // 0 eq, 1 ne, 2 change
    uint32_t value = 0;  // compare value, or the last value for "change"
};

struct PadOverride {
    bool active = false;
    uint16_t held = 0;
    int8_t sx = 0, sy = 0;
    int64_t frames = -1;  // -1 = until cleared
    uint16_t prev_held = 0;
};

std::mutex g_mu;
std::condition_variable g_cv;
uint8_t* g_rdram = nullptr;
recomp_context* g_ctx = nullptr;     // valid while the game thread is blocked in the hook
uint64_t g_frame = 0;                // hook calls so far; the frame about to run while paused
bool g_paused = false;
bool g_at_hook = false;              // game thread is blocked in akidbg_frame
int64_t g_steps_left = 0;            // > 0: pause once this many more frames have run
uint64_t g_run_until = 0;            // nonzero: pause on reaching this frame
std::string g_pause_reason;
std::vector<Break> g_breaks;
PadOverride g_pads[4];
std::map<std::string, Snapshot> g_snaps;
std::deque<std::pair<uint64_t, std::function<void()>>> g_jobs;  // run on the game thread while paused
uint64_t g_job_seq = 0;
std::once_flag g_server_once;
int g_port = 5664;

// ---- RDRAM access ----

bool phys_range(uint32_t addr, uint32_t len, uint32_t& phys) {
    phys = addr & 0x1FFFFFFF;
    return g_rdram && phys < kRamSize && len <= kRamSize - phys;
}

uint8_t ram_u8(uint32_t phys) { return g_rdram[phys ^ 3]; }
void ram_set_u8(uint32_t phys, uint8_t v) { g_rdram[phys ^ 3] = v; }

uint32_t ram_read(uint32_t phys, int size) {
    uint32_t v = 0;
    for (int i = 0; i < size; i++) v = (v << 8) | ram_u8(phys + i);
    return v;
}

void ram_write(uint32_t phys, int size, uint32_t v) {
    for (int i = size - 1; i >= 0; i--, v >>= 8) ram_set_u8(phys + i, (uint8_t)v);
}

// ---- small helpers ----

std::string hexstr(uint64_t v) {
    char b[24];
    snprintf(b, sizeof b, "\"0x%llX\"", (unsigned long long)v);
    return b;
}

bool parse_u64(const std::string& s, uint64_t& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = strtoull(s.c_str(), &end, 0);
    return end && *end == 0;
}

std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += c;
    }
    return o;
}

std::string url_decode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else if (s[i] == '+') o += ' ';
        else o += s[i];
    }
    return o;
}

// Hotkey events, so a client can see what was pressed: F4 marks a moment for a client to look at.
struct Event { uint64_t seq, frame; std::string what; };
std::deque<Event> g_events;
uint64_t g_event_seq = 0;

// Caller holds g_mu.
std::string status_json() {
    std::string s = "{\"frame\":" + std::to_string(g_frame) +
        ",\"paused\":" + (g_paused ? "true" : "false") +
        ",\"at_hook\":" + (g_at_hook ? "true" : "false") +
        ",\"attached\":" + (g_rdram ? "true" : "false") +
        ",\"reason\":\"" + json_escape(g_pause_reason) + "\"" +
        ",\"steps_left\":" + std::to_string(g_steps_left) +
        ",\"sp\":" + (g_ctx ? hexstr((uint32_t)g_ctx->r[29]) : std::string("null")) +
        ",\"run_until\":" + std::to_string(g_run_until) +
        ",\"breaks\":[";
    for (size_t i = 0; i < g_breaks.size(); i++) {
        const Break& b = g_breaks[i];
        static const char* kinds[] = {"eq", "ne", "change"};
        s += (i ? "," : "") + std::string("{\"addr\":") + hexstr(0x80000000u | b.addr) +
             ",\"size\":" + std::to_string(b.size) + ",\"kind\":\"" + kinds[b.kind] +
             "\",\"value\":" + hexstr(b.value) + "}";
    }
    s += "],\"snapshots\":[";
    bool first = true;
    for (auto& kv : g_snaps) {
        s += (first ? "" : ",") + std::string("{\"slot\":\"") + json_escape(kv.first) +
             "\",\"frame\":" + std::to_string(kv.second.frame) + "}";
        first = false;
    }
    s += "],\"events\":[";
    for (size_t i = 0; i < g_events.size(); i++) {
        const Event& e = g_events[i];
        s += (i ? "," : "") + std::string("{\"seq\":") + std::to_string(e.seq) + ",\"frame\":" +
             std::to_string(e.frame) + ",\"what\":\"" + json_escape(e.what) + "\"}";
    }
    s += "]}";
    return s;
}

// Caller holds g_mu. Returns true if the game should stop at this frame.
bool check_stop() {
    if (g_paused) return true;
    if (g_steps_left > 0 && --g_steps_left == 0) { g_pause_reason = "step"; return true; }
    if (g_run_until && g_frame >= g_run_until) { g_run_until = 0; g_pause_reason = "run_until"; return true; }
    bool hit = false;
    for (Break& b : g_breaks) {
        uint32_t v = ram_read(b.addr, b.size);
        bool h = b.kind == 0 ? v == b.value : b.kind == 1 ? v != b.value : v != b.value;
        if (b.kind == 2) b.value = v;
        if (h && !hit) {
            hit = true;
            char buf[64];
            snprintf(buf, sizeof buf, "break 0x%08X = 0x%X", 0x80000000u | b.addr, v);
            g_pause_reason = buf;
        }
    }
    return hit;
}

// Waits (caller holds lock) until the game thread is parked at the hook, or times out.
bool wait_at_hook(std::unique_lock<std::mutex>& lk, int timeout_ms) {
    return g_cv.wait_for(lk, std::chrono::milliseconds(timeout_ms), [] { return g_at_hook && g_paused; });
}

// Runs fn on the game thread while it is parked; caller holds lock and has checked g_at_hook.
// Gives up after timeout_ms (the job then never runs) and returns whether it ran.
bool run_on_game_thread(std::unique_lock<std::mutex>& lk, std::function<void()> fn, int timeout_ms) {
    bool done = false;
    uint64_t id = ++g_job_seq;
    g_jobs.push_back({id, [&] { fn(); done = true; }});
    g_cv.notify_all();
    if (g_cv.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return done; })) return true;
    // Not run yet (the game thread holds g_mu while running jobs): drop it before its locals go.
    for (auto it = g_jobs.begin(); it != g_jobs.end(); ++it) {
        if (it->first == id) { g_jobs.erase(it); break; }
    }
    return false;
}

bool save_file(const std::string& path, const Snapshot& s) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const char magic[8] = {'A', 'K', 'I', 'D', 'B', 'G', '1', 0};
    fwrite(magic, 1, 8, f);
    fwrite(&s.frame, sizeof s.frame, 1, f);
    fwrite(&s.ctx, offsetof(recomp_context, f_odd), 1, f);
    fwrite(s.ram.data(), 1, s.ram.size(), f);
    return fclose(f) == 0;
}

bool load_file(const std::string& path, Snapshot& s) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char magic[8];
    bool ok = fread(magic, 1, 8, f) == 8 && memcmp(magic, "AKIDBG1", 8) == 0;
    ok = ok && fread(&s.frame, sizeof s.frame, 1, f) == 1;
    ok = ok && fread(&s.ctx, offsetof(recomp_context, f_odd), 1, f) == 1;
    s.ram.resize(kRamSize);
    ok = ok && fread(s.ram.data(), 1, kRamSize, f) == kRamSize;
    fclose(f);
    return ok;
}

// A snapshot restores the game thread, but the other threads keep running natively while the
// game is paused and are never where they were at save time. So a load keeps, as they are live:
// each thread's OSThread (the runtime's scheduling bookkeeping: run-queue link, state, the queue
// it waits on), each other thread's MIPS stack (its saved registers and locals, which its native
// code will pop), and the wait-list heads of the queues the threads wait on. Threads and stacks
// are in kThreads at the top.
void restore_ram_keeping_threads(const std::vector<uint8_t>& ram) {
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> keep;
    auto keep_range = [&](uint32_t phys, uint32_t len) {
        if (phys + len > kRamSize) return;
        keep.push_back({phys, std::vector<uint8_t>(g_rdram + phys, g_rdram + phys + len)});
    };
    auto snap_word = [&](uint32_t phys) {
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) v = (v << 8) | ram[(phys + i) ^ 3];
        return v;
    };
    for (const ThreadArea& t : kThreads) {
        keep_range(t.thread, kThreadSize);
        if (t.stack_hi > t.stack_lo) keep_range(t.stack_lo, t.stack_hi - t.stack_lo);
        // OSThread +8: the wait-list head (&mq->mtqueue or &mq->fullqueue) it waits on, live or then.
        for (uint32_t q : {ram_read(t.thread + 8, 4), snap_word(t.thread + 8)}) {
            if (q & 0x80000000) keep_range(q & 0x1FFFFFFC, 4);
        }
    }
    memcpy(g_rdram, ram.data(), kRamSize);
    for (auto& k : keep) memcpy(g_rdram + k.first, k.second.data(), k.second.size());  // word-aligned, so ^3 is moot
}

// Snapshot helpers. Run on the game thread in the paused loop (g_ctx valid), with g_mu held.
bool snapshot_save(const std::string& slot, const std::string& file, std::string& msg) {
    Snapshot s;
    s.ram.assign(g_rdram, g_rdram + kRamSize);
    memcpy(&s.ctx, g_ctx, sizeof s.ctx);
    s.frame = g_frame;
    bool ok = true;
    if (!file.empty() && !save_file(file, s)) { ok = false; msg = "could not write " + file; }
    if (!slot.empty()) g_snaps[slot] = std::move(s);
    return ok;
}

bool snapshot_load(const std::string& slot, const std::string& file, std::string& msg) {
    Snapshot fs;
    const Snapshot* s = nullptr;
    if (!slot.empty()) {
        auto it = g_snaps.find(slot);
        if (it != g_snaps.end()) s = &it->second;
    } else if (load_file(file, fs)) s = &fs;
    if (!s) { msg = "no such snapshot"; return false; }
    // The native call stack above the hook is not part of the snapshot, so it can only be
    // restored into the same call path, which the stack pointer tells apart.
    if (s->ctx.r[29] != g_ctx->r[29]) {
        char b[120];
        snprintf(b, sizeof b, "snapshot was taken with sp 0x%08X, the game is now at sp 0x%08X (another frame loop?)",
                 (uint32_t)s->ctx.r[29], (uint32_t)g_ctx->r[29]);
        msg = b;
        return false;
    }
    // Every other thread must be waiting on the same queue as when the snapshot was taken, or the
    // data the snapshot restores won't match what that thread's live stack expects.
    for (const ThreadArea& t : kThreads) {
        uint32_t live = ram_read(t.thread + 8, 4), then = 0;
        for (int i = 0; i < 4; i++) then = (then << 8) | s->ram[(t.thread + 8 + i) ^ 3];
        if (live != then) {
            char b[160];
            snprintf(b, sizeof b, "thread 0x%08X waits on 0x%08X now but on 0x%08X in the snapshot; step a frame and try again",
                     0x80000000u | t.thread, live, then);
            msg = b;
            return false;
        }
    }
    restore_ram_keeping_threads(s->ram);
    // Registers up to (not including) the host pointer f_odd.
    memcpy(g_ctx, &s->ctx, offsetof(recomp_context, f_odd));
    g_ctx->status_reg = s->ctx.status_reg;
    g_frame = s->frame;
    for (Break& b : g_breaks) if (b.kind == 2) b.value = ram_read(b.addr, b.size);
    return true;
}

void add_event(const std::string& what) {
    g_events.push_back({++g_event_seq, g_frame, what});
    if (g_events.size() > 32) g_events.pop_front();
    fprintf(stderr, "[aki_debug] frame %llu: %s\n", (unsigned long long)g_frame, what.c_str());
}

// ---- HTTP ----

struct Request {
    std::string path;
    std::map<std::string, std::string> q;
    std::string body;
};

struct Response {
    int code = 200;
    std::string type = "application/json";
    std::string body;
};

Response err(int code, const std::string& msg) {
    Response r;
    r.code = code;
    r.body = "{\"error\":\"" + json_escape(msg) + "\"}";
    return r;
}

Response ok_status(std::unique_lock<std::mutex>&) {
    Response r;
    r.body = status_json();
    return r;
}

bool qnum(const Request& rq, const char* key, uint64_t& out) {
    auto it = rq.q.find(key);
    return it != rq.q.end() && parse_u64(it->second, out);
}

std::string qstr(const Request& rq, const char* key) {
    auto it = rq.q.find(key);
    return it == rq.q.end() ? std::string() : it->second;
}

int qtimeout(const Request& rq) {
    uint64_t t = 10000;
    qnum(rq, "timeout", t);
    return (int)t;
}

Response handle(const Request& rq) {
    std::unique_lock<std::mutex> lk(g_mu);
    const std::string& p = rq.path;

    if (p == "/" || p == "/status") return ok_status(lk);

    if (p == "/pause") {
        g_paused = true;
        g_pause_reason = "pause";
        if (!wait_at_hook(lk, qtimeout(rq))) return err(504, "game did not reach the frame hook (is a match or menu running?)");
        return ok_status(lk);
    }

    if (p == "/resume") {
        g_paused = false;
        g_steps_left = 0;
        g_pause_reason.clear();
        g_cv.notify_all();
        return ok_status(lk);
    }

    if (p == "/step" || p == "/run") {
        uint64_t n = 1;
        if (p == "/step") {
            qnum(rq, "n", n);
            if (n == 0) return err(400, "n must be >= 1");
            g_steps_left = (int64_t)n;
        } else {
            if (!qnum(rq, "frame", n)) return err(400, "run needs frame=<absolute frame>");
            if (n <= g_frame) return err(400, "frame is not in the future");
            g_run_until = n;
        }
        g_paused = false;
        g_pause_reason.clear();
        g_cv.notify_all();
        // Wait for the game to leave the hook, then to stop again.
        uint64_t start = g_frame;
        g_cv.wait_for(lk, std::chrono::milliseconds(qtimeout(rq)), [&] { return g_frame != start && g_at_hook && g_paused; });
        return ok_status(lk);
    }

    if (p == "/read") {
        uint64_t addr, len = 4;
        if (!qnum(rq, "addr", addr)) return err(400, "read needs addr");
        qnum(rq, "len", len);
        uint32_t phys;
        if (!phys_range((uint32_t)addr, (uint32_t)len, phys)) return err(400, "address out of RDRAM");
        Response r;
        if (rq.q.count("binary")) {
            r.type = "application/octet-stream";
            r.body.resize(len);
            for (uint32_t i = 0; i < len; i++) r.body[i] = (char)ram_u8(phys + i);
            return r;
        }
        std::string hex;
        hex.reserve(len * 2);
        static const char* d = "0123456789abcdef";
        for (uint32_t i = 0; i < len; i++) { uint8_t b = ram_u8(phys + i); hex += d[b >> 4]; hex += d[b & 15]; }
        r.body = "{\"addr\":" + hexstr(0x80000000u | phys) + ",\"len\":" + std::to_string(len) + ",\"hex\":\"" + hex + "\"}";
        return r;
    }

    if (p == "/write") {
        uint64_t addr;
        if (!qnum(rq, "addr", addr)) return err(400, "write needs addr");
        std::string data;
        std::string hex = qstr(rq, "hex");
        if (!hex.empty()) {
            if (hex.size() % 2) return err(400, "odd hex length");
            for (size_t i = 0; i < hex.size(); i += 2) data += (char)strtol(hex.substr(i, 2).c_str(), nullptr, 16);
        } else data = rq.body;
        uint32_t phys;
        if (data.empty() || !phys_range((uint32_t)addr, (uint32_t)data.size(), phys)) return err(400, "nothing to write or out of RDRAM");
        for (size_t i = 0; i < data.size(); i++) ram_set_u8(phys + (uint32_t)i, (uint8_t)data[i]);
        Response r;
        r.body = "{\"written\":" + std::to_string(data.size()) + "}";
        return r;
    }

    if (p == "/break") {
        if (rq.q.count("clear")) { g_breaks.clear(); return ok_status(lk); }
        uint64_t addr, size = 4, v = 0;
        if (!qnum(rq, "addr", addr)) return err(400, "break needs addr (or clear=1)");
        qnum(rq, "size", size);
        if (size != 1 && size != 2 && size != 4) return err(400, "size must be 1, 2 or 4");
        Break b;
        uint32_t phys;
        if (!phys_range((uint32_t)addr, (uint32_t)size, phys)) return err(400, "address out of RDRAM");
        b.addr = phys;
        b.size = (int)size;
        if (qnum(rq, "eq", v)) { b.kind = 0; b.value = (uint32_t)v; }
        else if (qnum(rq, "ne", v)) { b.kind = 1; b.value = (uint32_t)v; }
        else if (rq.q.count("change")) { b.kind = 2; b.value = ram_read(phys, (int)size); }
        else return err(400, "break needs eq=, ne= or change=1");
        g_breaks.push_back(b);
        return ok_status(lk);
    }

    if (p == "/pad") {
        if (rq.q.count("clear")) { for (auto& o : g_pads) o.active = false; return ok_status(lk); }
        uint64_t port, held = 0, frames = 0, sx = 0, sy = 0;
        if (!qnum(rq, "port", port) || port > 3) return err(400, "pad needs port=0..3");
        qnum(rq, "held", held);
        bool has_frames = qnum(rq, "frames", frames);
        qnum(rq, "sx", sx);
        qnum(rq, "sy", sy);
        PadOverride& o = g_pads[port];
        o.active = true;
        o.held = (uint16_t)held;
        o.sx = (int8_t)(int64_t)sx;
        o.sy = (int8_t)(int64_t)sy;
        o.frames = has_frames ? (int64_t)frames : -1;
        return ok_status(lk);
    }

    if (p == "/save" || p == "/load") {
        std::string slot = qstr(rq, "slot");
        std::string file = qstr(rq, "file");
        if (slot.empty() && file.empty()) return err(400, "needs slot= and/or file=");
        if (!g_at_hook || !g_paused) return err(409, "pause first: snapshots are taken and restored at the frame hook");
        bool ok = true;
        std::string msg;
        bool ran = run_on_game_thread(lk, [&] {
            ok = p == "/save" ? snapshot_save(slot, file, msg) : snapshot_load(slot, file, msg);
        }, qtimeout(rq));
        if (!ran) return err(504, "the other game threads did not settle (graphics thread idle between frames, scheduler idle); this happens in menus that are loading");
        if (!ok) return err(400, msg);
        return ok_status(lk);
    }

    if (p == "/drop") {
        g_snaps.erase(qstr(rq, "slot"));
        return ok_status(lk);
    }

    if (p == "/regs") {
        if (!g_at_hook) return err(409, "pause first");
        Response r;
        r.body = "{\"gpr\":[";
        for (int i = 0; i < 32; i++) r.body += (i ? "," : "") + hexstr(g_ctx->r[i]);
        r.body += "],\"fpr\":[";
        for (int i = 0; i < 32; i++) r.body += (i ? "," : "") + hexstr(g_ctx->f[i].u64);
        r.body += "]}";
        return r;
    }

    if (p == "/quit") {
        // Ends the game process right after answering (no shutdown sequence, so nothing is saved).
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            TerminateProcess(GetCurrentProcess(), 0);
        }).detach();
        Response r;
        r.body = "{\"quit\":true}";
        return r;
    }

    return err(404, "unknown endpoint " + p);
}

bool recv_request(SOCKET c, Request& rq) {
    std::string buf;
    char tmp[4096];
    size_t hdr_end;
    while ((hdr_end = buf.find("\r\n\r\n")) == std::string::npos) {
        int n = recv(c, tmp, sizeof tmp, 0);
        if (n <= 0 || buf.size() > 65536) return false;
        buf.append(tmp, n);
    }
    size_t sp1 = buf.find(' '), sp2 = buf.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return false;
    std::string target = buf.substr(sp1 + 1, sp2 - sp1 - 1);
    size_t qm = target.find('?');
    rq.path = target.substr(0, qm);
    if (qm != std::string::npos) {
        std::string qs = target.substr(qm + 1);
        size_t pos = 0;
        while (pos <= qs.size()) {
            size_t amp = qs.find('&', pos);
            if (amp == std::string::npos) amp = qs.size();
            std::string kv = qs.substr(pos, amp - pos);
            size_t eq = kv.find('=');
            if (!kv.empty()) rq.q[url_decode(kv.substr(0, eq))] = eq == std::string::npos ? "" : url_decode(kv.substr(eq + 1));
            pos = amp + 1;
        }
    }
    size_t content_length = 0;
    std::string headers = buf.substr(0, hdr_end);
    for (auto& ch : headers) ch = (char)tolower((unsigned char)ch);
    size_t cl = headers.find("content-length:");
    if (cl != std::string::npos) content_length = strtoul(headers.c_str() + cl + 15, nullptr, 10);
    if (content_length > kRamSize) return false;
    rq.body = buf.substr(hdr_end + 4);
    while (rq.body.size() < content_length) {
        int n = recv(c, tmp, sizeof tmp, 0);
        if (n <= 0) return false;
        rq.body.append(tmp, n);
    }
    rq.body.resize(content_length);
    return true;
}

void send_response(SOCKET c, const Response& r) {
    const char* text = r.code == 200 ? "OK" : r.code == 400 ? "Bad Request" : r.code == 404 ? "Not Found"
                     : r.code == 409 ? "Conflict" : "Gateway Timeout";
    std::string out = "HTTP/1.1 " + std::to_string(r.code) + " " + text + "\r\nContent-Type: " + r.type +
        "\r\nContent-Length: " + std::to_string(r.body.size()) + "\r\nConnection: close\r\n\r\n" + r.body;
    size_t sent = 0;
    while (sent < out.size()) {
        int n = send(c, out.data() + sent, (int)(out.size() - sent), 0);
        if (n <= 0) break;
        sent += n;
    }
}

void server_main() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)g_port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (sockaddr*)&addr, sizeof addr) != 0 || listen(s, 8) != 0) {
        fprintf(stderr, "[aki_debug] could not listen on 127.0.0.1:%d\n", g_port);
        closesocket(s);
        return;
    }
    fprintf(stderr, "[aki_debug] listening on 127.0.0.1:%d\n", g_port);
    for (;;) {
        SOCKET c = accept(s, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        // One thread per connection, so a blocking step does not hold up status reads.
        std::thread([c] {
            Request rq;
            if (recv_request(c, rq)) send_response(c, handle(rq));
            closesocket(c);
        }).detach();
    }
}

void start_server() {
    const char* env = getenv("AKIDBG_PORT");
    if (env && atoi(env) > 0) g_port = atoi(env);
    if (const char* p = getenv("AKIDBG_PAUSE_AT")) {
        uint64_t f;
        if (parse_u64(p, f)) g_run_until = f;  // pause on reaching this frame
    }
    std::thread(server_main).detach();
}

// Hotkeys (bits of the mask the mod passes in a0): F2 pause/resume, F3 step one frame,
// F4 mark (pause and log a "mark" event for a client), F5 quick save, F9 quick load.
constexpr uint32_t kF2 = 1u << 1, kF3 = 1u << 2, kF4 = 1u << 3, kF5 = 1u << 4, kF9 = 1u << 8;
uint32_t g_keys_prev = 0;
int g_paused_retraces = 0;  // retraces since this pause began
uint32_t g_key_pending = 0;  // F5/F9 pressed while running: done once paused
bool g_key_resume = false;   // and then run on, since they were pressed while running

// Caller holds g_mu. Returns the keys newly pressed since the last call.
uint32_t key_edges(uint32_t keys) {
    uint32_t pressed = keys & ~g_keys_prev;
    g_keys_prev = keys;
    return pressed;
}

// Snapshots are only taken or restored once the other threads have settled into a paused game's
// steady state: a few retraces in, the graphics thread idle between frames (waiting for the game's
// next draw request; it also waits on its scheduler queue mid-frame, with state on its stack) and
// the scheduler waiting for an interrupt. Caller holds g_mu.
bool quiescent() {
    return g_paused_retraces >= 4 && ram_read(kGfxRequestQueue, 4) == kGfxThread &&
           ram_read(kSchedInterruptQueue, 4) == kSchedThread;
}

// Caller holds g_mu, on the game thread in the paused loop.
void run_key_actions(uint32_t pressed) {
    pressed |= g_key_pending;
    g_key_pending = 0;
    if ((pressed & (kF5 | kF9)) && !quiescent()) {
        g_key_pending = pressed & (kF5 | kF9);  // try again next retrace
        pressed &= ~(kF5 | kF9);
        if (!(pressed & (kF2 | kF3))) return;
    }
    bool resume = g_key_resume && !g_key_pending;
    if (resume) g_key_resume = false;
    std::string msg;
    if (pressed & kF5) add_event(snapshot_save("quick", "", msg) ? "F5 quick save" : "F5 quick save failed: " + msg);
    if (pressed & kF9) add_event(snapshot_load("quick", "", msg) ? "F9 quick load" : "F9 quick load failed: " + msg);
    if (pressed & kF3) { g_steps_left = 1; g_paused = false; g_pause_reason.clear(); }
    if (pressed & kF2) { add_event("F2 resume"); resume = true; }
    if (resume) { g_paused = false; g_steps_left = 0; g_pause_reason.clear(); }
}

}  // namespace

// Called by the mod at the entry of the per-frame hook, on the game thread, with the hotkey
// mask in a0. Returns (in v0) nonzero if the game should pause here; the mod then waits for
// retraces and calls akidbg_paused() after each one until it returns 0.
DLLEXPORT void akidbg_frame(uint8_t* rdram, recomp_context* ctx) {
    std::call_once(g_server_once, start_server);
    std::lock_guard<std::mutex> lk(g_mu);
    g_rdram = rdram;
    g_frame++;
    uint32_t pressed = key_edges((uint32_t)ctx->r[4]);
    bool stop = check_stop();
    if (pressed & (kF2 | kF3 | kF4 | kF5 | kF9)) {
        if (!stop) {
            g_pause_reason = (pressed & kF4) ? "F4 mark" : "hotkey";
            if (pressed & kF2) add_event("F2 pause");
            // A quick save or load while running pauses only for as long as it takes.
            g_key_resume = !(pressed & (kF2 | kF3 | kF4)) && (pressed & (kF5 | kF9));
        }
        if (pressed & kF4) add_event("F4 mark");
        g_key_pending = pressed & (kF5 | kF9);
        stop = true;
    }
    if (stop) {
        g_paused = true;
        g_at_hook = true;
        g_paused_retraces = 0;
        g_ctx = ctx;
        g_cv.notify_all();
    }
    ctx->r[2] = stop ? 1 : 0;
}

// While the game thread is paused, the graphics thread waits for the game's next draw request
// and stops reading its scheduler queue (World Tour 0x80040F68, Revenge 0x80046768; 16 messages). Once that queue is full, the
// scheduler blocks on its next blocking send to it and every thread stalls. So while paused,
// drop the oldest retrace messages from every scheduler client's queue, keeping the newest two
// and every other message. Runs on the game thread, the only N64 thread running at the time.
constexpr uint32_t kOwnQueue = 0x80000200;       // the mod's queue (see aki_debug.c)

void trim_client_queues() {
    uint32_t c = ram_read(kSchedClients, 4);
    for (int n = 0; c && n < 16; n++, c = ram_read(c & 0x1FFFFFFF, 4)) {
        uint32_t q = ram_read((c & 0x1FFFFFFF) + 4, 4);
        if (!q || q == kOwnQueue) continue;
        uint32_t qp = q & 0x1FFFFFFF;
        uint32_t valid = ram_read(qp + 8, 4), first = ram_read(qp + 0xC, 4);
        uint32_t count = ram_read(qp + 0x10, 4), buf = ram_read(qp + 0x14, 4) & 0x1FFFFFFF;
        if (valid <= 2 || count == 0 || valid > count || count > 64) continue;
        std::vector<uint32_t> msgs;
        for (uint32_t i = 0; i < valid; i++) msgs.push_back(ram_read(buf + ((first + i) % count) * 4, 4));
        uint32_t retraces = 0;
        for (uint32_t m : msgs) retraces += m == kRetraceMsg;
        uint32_t drop = retraces > 2 ? retraces - 2 : 0;
        if (!drop) continue;
        std::vector<uint32_t> kept;
        for (uint32_t m : msgs) {
            if (m == kRetraceMsg && drop) { drop--; continue; }
            kept.push_back(m);
        }
        for (uint32_t i = 0; i < kept.size(); i++) ram_write(buf + i * 4, 4, kept[i]);
        ram_write(qp + 0xC, 4, 0);
        ram_write(qp + 8, 4, (uint32_t)kept.size());
    }
}

// Called by the mod's paused loop once per retrace. Runs queued jobs (snapshots) and returns
// (in v0) whether to stay paused. Always called from the same place, so the registers and the
// stack pointer are the same on every call of a paused frame.
DLLEXPORT void akidbg_paused(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    std::lock_guard<std::mutex> lk(g_mu);
    g_ctx = ctx;
    g_paused_retraces++;
    if (quiescent()) {
        while (!g_jobs.empty()) {
            auto job = std::move(g_jobs.front().second);
            g_jobs.pop_front();
            job();
        }
    }
    uint32_t pressed = key_edges((uint32_t)ctx->r[4]);
    if (pressed & kF4) add_event("F4 mark");
    run_key_actions(pressed);
    trim_client_queues();
    if (!g_paused) {
        g_at_hook = false;
        g_ctx = nullptr;
    }
    g_cv.notify_all();
    ctx->r[2] = g_paused ? 1 : 0;
}

// Called by the mod when pads_read returns: applies pad overrides to g_pads.
DLLEXPORT void akidbg_pads(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    std::lock_guard<std::mutex> lk(g_mu);
    g_rdram = rdram;
    for (uint32_t port = 0; port < 4; port++) {
        PadOverride& o = g_pads[port];
        uint32_t base = (kPadsAddr & 0x1FFFFFFF) + port * 12;
        if (!o.active) { o.prev_held = (uint16_t)ram_read(base + 4, 2); continue; }
        ram_write(base + 4, 2, o.held);
        ram_write(base + 6, 2, (uint16_t)(o.held & ~o.prev_held));
        ram_write(base + 0xA, 1, (uint8_t)o.sx);
        ram_write(base + 0xB, 1, (uint8_t)o.sy);
        o.prev_held = o.held;
        if (o.frames > 0 && --o.frames == 0) o.active = false;
    }
}
