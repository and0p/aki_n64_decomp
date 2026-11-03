// Random numbers: rng_main (main segment), rng_match and its pool.
#include "nm_funcs.h"

namespace nm {

// N64 0x80036110: the main segment's generator (also driven by the camera and, on hardware, the
// idle loop; traces are recorded with the idle loop's call patched out).
u32 rng_main() {
    u32 v = g_rng_main_state << 2;
    g_rng_main_state = (v + 2) * (v + 3) >> 2;
    return g_rng_main_state;
}

// N64 0x8012E07C: 0-999.
s32 rng_match() {
    u32 r;
    do {
        g_rng_match_state = g_rng_match_state * 0x5D588B65u + g_rng_match_inc;
        r = (g_rng_match_state & 0xFFFFF) / 1000;
    } while (!((s32)r < 1000));
    return (s32)r;
}

// N64 0x8012DFF4: a value from the 32-entry pool (picked by rng_main), replaced by a new draw.
s32 rand_pool_next() {
    s16 *p = &g_rand_pool[rng_main() & 0x1F];
    s32 v = *p;
    *p = (s16)rng_match();
    return v;
}

// N64 0x8012E040: true with probability n / 1000.
s32 chance_check(s32 n) {
    if (n == 0)
        return 0;
    return !(n < rand_pool_next());
}

}  // namespace nm
