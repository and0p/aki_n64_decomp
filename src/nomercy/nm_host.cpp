// The host side of the decompilation: presentation calls the oracle ignores, and hooks for the
// parts still taken from the original (the replay tool sets them).
#include "nm_funcs.h"
#include "nm_host.h"

namespace nm {

HostHooks g_host;

void host_hud(const char *what, s32 a, s32 b) {
    if (g_host.hud)
        g_host.hud(what, a, b);
}

void host_sfx(s32 id) {
    if (g_host.sfx)
        g_host.sfx(-1, id);
}

void host_sfx_slot(s32 slot, s32 id) {
    if (g_host.sfx)
        g_host.sfx(slot, id);
}

void host_rom_read(u32 rom_addr, u8 *dst, u32 n) {
    if (!g_host.rom_read)
        NM_UNPORTED("the ROM (animation keyframes are streamed from it: give the replay --rom)");
    g_host.rom_read(rom_addr, dst, n);
}

void host_rope_push(s32 kind, s32 side, f32 along) {
    if (g_host.rope_push)
        g_host.rope_push(kind, side, along);
}

void host_anim_event_special(s32 slot, s32 kind) {
    if (g_host.anim_event_special)
        g_host.anim_event_special(slot, kind);
}

}  // namespace nm
