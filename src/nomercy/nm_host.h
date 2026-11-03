// Hooks the host fills in (nm_host.cpp).
#pragma once
#include "nm_types.h"

namespace nm {

struct HostHooks {
    void (*hud)(const char *what, s32 a, s32 b);  // HUD drawing calls
    void (*sfx)(s32 slot, s32 id);                // sound effects (slot -1: sfx_play)
    // Reads `n` bytes of the ROM at `rom_addr` into dst: the animation stream's keyframe DMAs
    // (anim_stream_fetch). Required: a replay without it stops at the first animation.
    void (*rom_read)(u32 rom_addr, u8 *dst, u32 n);
    // Rendering state the oracle does not keep: a rope push (ring_rope_push 0x800225BC: `kind`
    // 1-12 on ring side `side` at `along` units along it) and the special frame events 170-175
    // (anim_event_special 0x800172D4: a controller rumble / shake value, or a texture swap).
    void (*rope_push)(s32 kind, s32 side, f32 along);
    void (*anim_event_special)(s32 slot, s32 kind);
};
extern HostHooks g_host;

}  // namespace nm
