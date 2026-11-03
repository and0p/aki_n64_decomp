// Maths helpers: angles (4096 to a turn), vectors, and the main segment's float routines.
// Float order is the original's, including the steps it does in double precision.
#include "nm_math.h"

namespace nm {

// ---- main segment: libultra sinf / cosf, the game's atanf / atan2f ----

// N64 0x80036460 (libultra sinf)
f32 sinf_n64(f32 x) {
    u32 bits;
    std::memcpy(&bits, &x, 4);
    s32 xpt = (s32)bits >> 22 & 0x1FF;
    if (xpt < 255) {
        if (xpt < 230)
            return x;
        f64 dx = x;
        f64 xsq = dx * dx;
        f64 poly = ((2.605780637968037e-06 * xsq + -0.0001980960290193795) * xsq + 0.008333066246082155) * xsq;
        return (f32)(dx + dx * xsq * (poly + -0.16666659550427756));
    }
    if (xpt < 310) {
        f64 dx = x;
        f64 xpi = dx * 0.3183098861837907;
        s32 n = (s32)(0.0 <= xpi ? xpi + 0.5 : xpi - 0.5);
        f64 dn = n;
        dx = dx - dn * 3.1415926218032837;
        dx = dx - dn * 3.178650954705639e-08;
        f64 xsq = dx * dx;
        f64 poly = ((2.605780637968037e-06 * xsq + -0.0001980960290193795) * xsq + 0.008333066246082155) * xsq;
        f64 r = dx + dx * xsq * (poly + -0.16666659550427756);
        return (n & 1) ? -(f32)r : (f32)r;
    }
    if (x != x)
        return mem_f32(0x80056470);  // NaN
    return 0.0f;
}

// N64 0x80034EF0 (libultra cosf)
f32 cosf_n64(f32 x) {
    u32 bits;
    std::memcpy(&bits, &x, 4);
    s32 xpt = (s32)bits >> 22 & 0x1FF;
    if (xpt < 310) {
        f32 ax = 0.0f < x ? x : -x;
        f64 dx = ax;
        f64 xpi = dx * 0.3183098861837907 + 0.5;
        s32 n = (s32)(0.0 <= xpi ? xpi + 0.5 : xpi - 0.5);
        f64 dn = (f64)n - 0.5;
        dx = dx - dn * 3.1415926218032837;
        dx = dx - dn * 3.178650954705639e-08;
        f64 xsq = dx * dx;
        f64 poly = ((2.605780637968037e-06 * xsq + -0.0001980960290193795) * xsq + 0.008333066246082155) * xsq;
        f64 r = dx + dx * xsq * (poly + -0.16666659550427756);
        return (n & 1) ? -(f32)r : (f32)r;
    }
    if (x != x)
        return mem_f32(0x80056470);
    return 0.0f;
}

// N64 0x800054E0: arctangent by a continued fraction of 8 terms.
f32 atanf_n64(f32 x) {
    s32 flip = 0;
    if (1.0 < (f64)x) {
        flip = 1;
        x = 1.0f / x;
    }
    if ((f64)x < -1.0) {
        flip = -1;
        x = 1.0f / x;
    }
    f32 n = 8.0f;
    f32 frac = 0.0f;
    for (s32 i = 8; i > 0; i--) {
        f32 num = n * n * x * x;
        f32 den = n + n + 1.0f + frac;
        n = (f32)((f64)n - 1.0);
        frac = num / den;
    }
    f32 r = x / (frac + 1.0f);
    if (flip > 0)
        return (f32)(1.5707963267948966 - (f64)r);
    if (flip < 0)
        return (f32)(-1.5707963267948966 - (f64)r);
    return r;
}

// N64 0x800055D8: atan2f(x, z) as the game uses it: the angle of (x, z) measured from +z.
f32 atan2f_n64(f32 a, f32 b) {
    if ((f64)a == 0.0)
        return 0.0 <= (f64)b ? 0.0f : 3.14159274f;  // 0x40490FDB
    if (0.0 <= (f64)a)
        return (f32)(1.5707963267948966 - (f64)atanf_n64(b / a));
    return (f32)-((f64)atanf_n64(b / a) + 1.5707963267948966);
}

// ---- angles ----

// N64 0x8012E0DC
s32 abs_i(s32 v) { return v < 0 ? -v : v; }

// N64 0x8012E0F0
f32 absf(f32 v) { return 0.0 <= (f64)v ? v : -v; }

// N64 0x8012E11C: wraps to -2048..2047.
s32 angle_wrap(s32 a) {
    a &= 0xFFF;
    if (a & 0x800)
        a |= -4096;
    return a;
}

// N64 0x8012E13C
s32 angle_add(s32 a, s32 b) { return angle_wrap(a + b); }

// N64 0x8012E294: a - b, the short way round (not wrapped past one turn).
s32 angle_diff(s32 a, s32 b) {
    s32 d = a - b;
    if (abs_i(d) < 2049)
        return d;
    return d >= 0 ? d - 4096 : d + 4096;
}

// N64 0x8012E208: turns *angle toward target by at most step; 1 once there.
s32 angle_approach(s32 *angle, s32 target, s32 step) {
    s32 d = angle_diff(target, *angle);
    if (abs_i(d) < step) {
        *angle = target;
        return 1;
    }
    *angle = angle_add(*angle, d >= 0 ? step : -step);
    return 0;
}

// N64 0x8012E158
f32 angle_to_float(s32 a) { return (f32)((f64)a * 0.0015339807878856412); }

// N64 0x8012E178
s32 float_to_angle(f32 r) { return (s32)((f64)r * 651.8986469044033); }

// ---- vectors ----

// N64 0x8012ED34
s32 vec_xz_angle(f32 x, f32 z) { return float_to_angle(atan2f_n64(x, z)); }

// N64 0x8012ECA0: out = b - a.
void vec_sub(const Vec3 *a, const Vec3 *b, Vec3 *out) {
    out->x = b->x - a->x;
    out->y = b->y - a->y;
    out->z = b->z - a->z;
}

// N64 0x8012EE14
Vec3 *vec_add(const Vec3 *a, const Vec3 *b, Vec3 *out) {
    out->x = a->x + b->x;
    out->y = a->y + b->y;
    out->z = a->z + b->z;
    return out;
}

// N64 0x8012EE4C
Vec3 *vec_scale(const Vec3 *a, f32 s, Vec3 *out) {
    out->x = a->x * s;
    out->y = a->y * s;
    out->z = a->z * s;
    return out;
}

// N64 0x8012ECD4
f32 vec_len(const Vec3 *v) { return sqrtf_n64(v->x * v->x + v->y * v->y + v->z * v->z); }

// N64 0x8012ED0C
f32 vec2_len(f32 x, f32 z) { return sqrtf_n64(x * x + z * z); }

// N64 0x8012EDCC
void vec_normalize(Vec3 *v) {
    f32 len = vec_len(v);
    f32 x = v->x / len, y = v->y / len, z = v->z / len;
    v->x = x;
    v->y = y;
    v->z = z;
}

// N64 0x8012EEE4
f32 vec_dot(const Vec3 *a, const Vec3 *b) { return a->x * b->x + a->y * b->y + a->z * b->z; }

// N64 0x8012EE80
void vec_cross(const Vec3 *a, const Vec3 *b, Vec3 *out) {
    out->x = a->y * b->z - a->z * b->y;
    out->y = a->z * b->x - a->x * b->z;
    out->z = a->x * b->y - a->y * b->x;
}

// N64 0x8012EAC4: the xz vector of length len pointing along angle.
void vec_from_angle(s32 angle, f32 len, Vec3 *out) {
    out->x = len * sinf_n64(angle_to_float(angle));
    f32 c = cosf_n64(angle_to_float(angle));
    out->y = 0.0f;
    out->z = len * c;
}

// N64 0x8012EC5C
void vec_from_s16(const s16 *p, Vec3 *out) {
    out->x = (f32)p[0];
    out->y = (f32)p[1];
    out->z = (f32)p[2];
}

// N64 0x8012EC5C, for a point in the N64 image (collision data).
void vec_from_s16_mem(u32 addr, Vec3 *out) {
    out->x = (f32)mem_s16(addr);
    out->y = (f32)mem_s16(addr + 2);
    out->z = (f32)mem_s16(addr + 4);
}

// N64 0x8012EA74: distance from a to b in the xz plane.
f32 dist_to(const Vec3 *a, const Vec3 *b) {
    Vec3 d;
    vec_sub(a, b, &d);
    return vec2_len(d.x, d.z);
}

// N64 0x8012EA9C: the angle from a toward b.
s32 angle_to(const Vec3 *a, const Vec3 *b) {
    Vec3 d;
    vec_sub(a, b, &d);
    return vec_xz_angle(d.x, d.z);
}

// N64 0x8012EC28: which quarter of the ring a point is in (0-3, by its angle round the centre).
s32 ring_zone_of(const Vec3 *p) {
    s32 a = vec_xz_angle(p->x, p->z) + 4608;
    if (a < 0)
        a += 1023;
    return a >> 10 & 3;
}

// N64 0x8012E374: out (x, z) = base + offset (x, z) turned by angle; base may be null (origin).
// out->y is left alone.
void point_at_offset(const Vec3 *base, const Vec3 *offset, s32 angle, Vec3 *out) {
    f32 bx, bz;
    if (base == nullptr) {
        bx = 0.0f;
        bz = 0.0f;
    } else {
        bx = base->x;
        bz = base->z;
    }
    f32 c = cosf_n64(angle_to_float(angle));
    f32 s = sinf_n64(angle_to_float(angle));
    f32 ox = offset->x, oz = offset->z;
    out->x = bx + (ox * c + oz * s);
    out->z = bz + (oz * c - ox * s);
}

}  // namespace nm
