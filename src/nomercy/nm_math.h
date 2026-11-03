// Angles (4096 units to a turn, -2048..2047), vectors and the float routines (nm_math.cpp).
#pragma once
#include <cmath>
#include "nm_types.h"

namespace nm {

f32 sinf_n64(f32 x);
f32 cosf_n64(f32 x);
f32 atanf_n64(f32 x);
f32 atan2f_n64(f32 a, f32 b);
inline f32 sqrtf_n64(f32 x) { return std::sqrt(x); }  // N64 0x80034B40: sqrt.s

s32 abs_i(s32 v);
f32 absf(f32 v);
s32 angle_wrap(s32 a);
s32 angle_add(s32 a, s32 b);
s32 angle_diff(s32 a, s32 b);
s32 angle_approach(s32 *angle, s32 target, s32 step);
f32 angle_to_float(s32 a);
s32 float_to_angle(f32 r);

s32 vec_xz_angle(f32 x, f32 z);
void vec_sub(const Vec3 *a, const Vec3 *b, Vec3 *out);
Vec3 *vec_add(const Vec3 *a, const Vec3 *b, Vec3 *out);
Vec3 *vec_scale(const Vec3 *a, f32 s, Vec3 *out);
f32 vec_len(const Vec3 *v);
f32 vec2_len(f32 x, f32 z);
void vec_normalize(Vec3 *v);
f32 vec_dot(const Vec3 *a, const Vec3 *b);
void vec_cross(const Vec3 *a, const Vec3 *b, Vec3 *out);
void vec_from_angle(s32 angle, f32 len, Vec3 *out);
void vec_from_s16(const s16 *p, Vec3 *out);
void vec_from_s16_mem(u32 addr, Vec3 *out);
f32 dist_to(const Vec3 *a, const Vec3 *b);
s32 angle_to(const Vec3 *a, const Vec3 *b);
s32 ring_zone_of(const Vec3 *p);
void point_at_offset(const Vec3 *base, const Vec3 *offset, s32 angle, Vec3 *out);

}  // namespace nm
