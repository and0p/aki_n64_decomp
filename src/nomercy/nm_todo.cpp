// Functions the frame calls that are not decompiled yet. Each stops a replay when it is reached,
// naming itself, so the decompilation proceeds in the order the traces need.
#include "nm_funcs.h"

namespace nm {

// N64 0x80142620: clears spent move objects (type2 31 / 32 whose model record's +0 is 1) and poses
// every object's model (object_pose_in_place).
void objects_cleanup() {
    for (s32 j = 0; j < 7; j++) {
        u32 obj = 0x8016AE10 + 180 * j;
        if ((u32)(mem_s32(obj + 4) - 31) < 2 && mem_u8(0x800A9C70 + 140 * j) == 1) {
            mem_w32(obj + 0, (u32)-1);
            mem_w32(obj + 4, (u32)-1);
            mem_w32(obj + 8, (u32)-1);
            mem_w32(obj + 12, (u32)-1);
            mem_w16(obj + 0x20, 0);
        }
        object_pose_in_place(j);
    }
}

}  // namespace nm
