// aki_debug mod: calls the native library (../native/aki_debug.cpp) once per game frame.
//
// The hook is the controller read, pads_read (World Tour 0x8000544C, Revenge 0x80003E78), which
// the match loop calls exactly once per frame, before match_tick. Build with -DAKI_GAME_REVENGE=1
// for Revenge (build.ps1 -Game revenge). A paused game sits between two frames: the last frame's
// logic is done, the next one's input is unread.
//
// While paused, the game thread must not just block in native code: the runtime runs one N64
// thread at a time, so the other threads (scheduler, graphics, audio) would stop too, and the
// VI and RSP events queued meanwhile would be dropped, which hangs the game on resume. Instead
// the paused loop waits in osRecvMesg on a queue that the game's scheduler sends every retrace
// to, like any game thread waiting for the next frame.

#include "modding.h"

typedef struct {
    void* mtqueue;
    void* fullqueue;
    int valid_count;
    int first;
    int msg_count;
    void** msg;
} OSMesgQueue;

typedef struct SchedClient {
    struct SchedClient* next;
    OSMesgQueue* queue;
} SchedClient;

void osCreateMesgQueue(OSMesgQueue* mq, void** msg, int count);
int osRecvMesg(OSMesgQueue* mq, void** msg, int flags);
#if AKI_GAME_REVENGE
#define PADS_READ "func_80003E78"
void func_80000580(SchedClient* client, OSMesgQueue* mq);  // the scheduler's add-client
#define sched_add_client func_80000580
#else
#define PADS_READ "func_8000544C"
void func_800005C0(SchedClient* client, OSMesgQueue* mq);
#define sched_add_client func_800005C0
#endif

RECOMP_IMPORT("*", unsigned int recomp_get_key_down(unsigned int scancode));
RECOMP_IMPORT(".", int akidbg_frame(unsigned int keys));
RECOMP_IMPORT(".", int akidbg_paused(unsigned int keys));
RECOMP_IMPORT(".", void akidbg_pads(void));

// The queue and client must sit in the game's 8 MB of RDRAM (the runtime rejects queues in mod
// memory). 0x80000200-0x800002FF is free: the exception vectors below it are never installed in
// the recomp, and the OS globals start at 0x80000300.
#define RETRACE_QUEUE ((OSMesgQueue*)0x80000200)
#define RETRACE_BUF ((void**)0x80000220)
#define CLIENT ((SchedClient*)0x80000228)

static int registered;

// Hotkeys, as a bit mask for the DLL (bit n = F(n+1)). F1 (RT64 inspector), F6 (free camera) and
// F11 (fullscreen) belong to the recomp and its bundled mods.
static unsigned int read_keys(void) {
    static const unsigned char scancodes[] = { 59, 60, 61, 62, 66 };  // F2, F3, F4, F5, F9
    static const unsigned char bits[] = { 1, 2, 3, 4, 8 };
    unsigned int keys = 0;
    for (int i = 0; i < 5; i++) {
        if (recomp_get_key_down(scancodes[i])) {
            keys |= 1u << bits[i];
        }
    }
    return keys;
}

RECOMP_HOOK(PADS_READ) void akidbg_on_pads_read(void) {
    if (!registered) {
        osCreateMesgQueue(RETRACE_QUEUE, RETRACE_BUF, 1);
        sched_add_client(CLIENT, RETRACE_QUEUE);
        registered = 1;
    }
    if (!akidbg_frame(read_keys())) {
        return;
    }
    do {
        void* msg;
        osRecvMesg(RETRACE_QUEUE, &msg, 1);
    } while (akidbg_paused(read_keys()));
}

RECOMP_HOOK_RETURN(PADS_READ) void akidbg_after_pads_read(void) {
    akidbg_pads();
}
