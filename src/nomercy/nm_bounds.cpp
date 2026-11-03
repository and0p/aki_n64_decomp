// Staying inside the arena (the collision walls) and pushing apart from the other wrestlers.
//
// A collision set is a 0-ended list of N64 pointers to groups; a group is a (first, last) pair of
// walls, linked through each wall's +0x2C. A wall (48 bytes, static data in the match overlay) is
// four s16 corners (+0x00), its normal (+0x18, three floats filled at match start), an id (+0x24),
// direction bits (+0x26) and flags (+0x28). Walls are read from the RDRAM image.
#include "nm_funcs.h"

namespace nm {

namespace {

enum { WALL_NORMAL = 0x18, WALL_DIRS = 0x26, WALL_NEXT = 0x2C };

Vec3 wall_normal(u32 wall) {
    Vec3 n = {mem_f32(wall + WALL_NORMAL), mem_f32(wall + WALL_NORMAL + 4), mem_f32(wall + WALL_NORMAL + 8)};
    return n;
}

// |a x b| scaled by 0.01 first, as quad_contains_point does it.
f32 cross_len(const Vec3 *a, const Vec3 *b) {
    Vec3 c;
    vec_cross(a, b, &c);
    vec_scale(&c, 0.01f, &c);
    return sqrtf_n64(c.x * c.x + c.y * c.y + c.z * c.z);
}

}  // namespace

// N64 0x8012F000: signed distance of p from the plane through `origin` with normal n.
f32 plane_distance(const Vec3 *origin, const Vec3 *n, const Vec3 *p) {
    Vec3 d;
    vec_sub(origin, p, &d);
    f32 dot = vec_dot(&d, n);
    return dot / vec_len(n);
}

// N64 0x8012EF14: the cosine of the angle between a and b.
f32 vec_normalize_two(const Vec3 *a, const Vec3 *b) {
    Vec3 na = *a, nb = *b;
    vec_normalize(&na);
    vec_normalize(&nb);
    return na.x * nb.x + na.y * nb.y + nb.z * na.z;
}

namespace {

// The quad test of quad_contains_point on four corners already read.
s32 quad_contains(const Vec3 *c, const Vec3 *p) {
    Vec3 a, b, cc, d;
    vec_sub(&c[0], &c[1], &a);
    vec_sub(&c[0], &c[2], &b);
    vec_sub(&c[0], &c[3], &cc);
    f32 area = cross_len(&a, &b);
    area = area + cross_len(&b, &cc);
    vec_sub(p, &c[0], &a);
    vec_sub(p, &c[1], &b);
    vec_sub(p, &c[2], &cc);
    vec_sub(p, &c[3], &d);
    f32 sum = cross_len(&a, &b);
    sum = sum + cross_len(&b, &cc);
    sum = sum + cross_len(&cc, &d);
    sum = sum + cross_len(&d, &a);
    return (f64)absf(area - sum) <= 1.0;
}

}  // namespace

// N64 0x8012F328: whether p (on the wall's plane) is inside the wall's quad: the four triangles
// from p to the edges add up to the quad's area (within 1, in units of 100).
s32 quad_contains_point(u32 wall, const Vec3 *p) {
    Vec3 c[4];
    for (s32 k = 0; k < 4; k++)
        vec_from_s16_mem(wall + 6u * (u32)k, &c[k]);
    return quad_contains(c, p);
}

// N64 0x8012D58C: whether a move from the wrestler crosses an arena wall: the start is point_120 at
// pos.y + 10 (on the apron pos at y + 10, with flags8a 0x800 point_120's x on ring sides 0 / 2 and
// its z on 1 / 3), the walls the per-state set (bounds_override) or the place's (as keep_in_bounds).
// The first wall crossed (segment_hit kind 0, groups in order) goes into zone_info and zone_info2;
// returns its direction bits (+0x26 & 0xF00), 0 for none.
u16 move_hits_segment(Wrestler *w, const Vec3 *move) {
    Vec3 p;
    if (w->location & 2) {
        p = w->pos;
        p.y = (f32)((f64)p.y + 10.0);
        if (w->flags8a & 0x800) {
            if (w->ring_zone == 0 || w->ring_zone == 2)
                p.x = w->point_120.x;
            else if (w->ring_zone == 1 || w->ring_zone == 3)
                p.z = w->point_120.z;
        }
    } else {
        p = w->point_120;
        p.y = (f32)((f64)w->pos.y + 10.0);
    }
    Vec3 q;
    vec_add(&p, move, &q);
    u32 set = w->bounds_override;
    if (set == 0) {
        u16 loc = w->location;
        if (loc & 0x001) {
            set = 0x80146BF0;
        } else if (loc & 0x002) {
            set = mem_u32(0x8014D390 + 4 * w->ring_zone);
        } else if (loc & 0x004) {
            s32 arena = mem_s32(A_ARENA);
            s32 area = arena < 10 ? 0 : arena - 9;
            set = mem_u32(0x8014D290 + 16 * area + 4 * ring_zone_of(&w->point_120));
        } else if (loc & 0x040) {
            set = 0x80147E20;
        } else if (loc & 0x080) {
            set = 0x8014BC8C;
        } else if (loc & 0x100) {
            set = 0x8014B704;
        } else if (loc & 0x400) {
            set = mem_u32(0x8014D3A0 + 4 * w->ring_zone);
        } else {
            set = 0x8014C500;
        }
    }
    u32 hit = 0;
    for (u32 g = set; mem_u32(g) != 0 && !hit; g += 4) {
        u32 group = mem_u32(g);
        for (u32 wall = mem_u32(group);; wall = mem_u32(wall + WALL_NEXT)) {
            if (segment_hit(&p, &q, wall, nullptr, nullptr, 0)) {
                hit = wall;
                break;
            }
            if (wall == mem_u32(group + 4))
                break;
        }
    }
    if (!hit)
        return 0;
    w->zone_info = hit;
    w->zone_info2 = hit;
    return mem_u16(hit + WALL_DIRS) & 0xF00;
}

// N64 0x8012F050: the segment from -> to against one wall quad (4 s16 points, the normal at +0x18):
// 1 when `to` lies on or behind the plane, `from` on or in front of it, and the crossing point is
// inside the quad. dist (when given) gets the distance from `from` to the crossing, push (when given)
// the normal scaled by 1.02 x the depth of `to`. The test runs on a copy of the quad: kind 1 with
// flags +0x28 0x800 stretched down (the top edge moved to the bottom one, the bottom 1000 lower),
// kind 2 with 0x1000 up (the top 1000 higher), kind 3 unless +0x26 0x01 both ways; else unchanged.
s32 segment_hit(const Vec3 *from, const Vec3 *to, u32 wall, Vec3 *push, f32 *dist, s32 kind) {
    s16 y[4];
    for (s32 k = 0; k < 4; k++)
        y[k] = mem_s16(wall + 6u * (u32)k + 2);
    if (kind == 1 && (mem_u16(wall + 0x28) & 0x800)) {
        s16 y2 = y[2], y3 = y[3];
        y[1] = y2;
        y[0] = y3;
        y[2] = (s16)(y2 - 1000);
        y[3] = (s16)(y3 - 1000);
    } else if (kind == 2 && (mem_u16(wall + 0x28) & 0x1000)) {
        y[0] = (s16)(y[0] + 1000);
        y[1] = (s16)(y[1] + 1000);
    } else if (kind == 3 && !(mem_u16(wall + 0x26) & 1)) {
        y[0] = (s16)(y[0] + 1000);
        y[1] = (s16)(y[1] + 1000);
        y[2] = (s16)(y[2] - 1000);
        y[3] = (s16)(y[3] - 1000);
    }
    Vec3 c[4];
    for (s32 k = 0; k < 4; k++) {
        c[k].x = (f32)mem_s16(wall + 6u * (u32)k);
        c[k].y = (f32)y[k];
        c[k].z = (f32)mem_s16(wall + 6u * (u32)k + 4);
    }
    Vec3 n = {mem_f32(wall + WALL_NORMAL), mem_f32(wall + WALL_NORMAL + 4), mem_f32(wall + WALL_NORMAL + 8)};
    f32 d_to = plane_distance(&c[0], &n, to);
    if (!((f64)d_to <= 0.0))
        return 0;
    f32 d_from = plane_distance(&c[0], &n, from);
    if (!(0.0 <= (f64)d_from))
        return 0;
    f32 sum = absf(d_to) + d_from;
    if ((f64)sum == 0.0)
        return 0;
    f32 t = d_from / sum;
    Vec3 d, step, cross;
    vec_sub(from, to, &d);
    vec_scale(&d, t, &step);
    vec_add(from, &step, &cross);
    if (!quad_contains(c, &cross))
        return 0;
    if (dist)
        *dist = dist_to(from, &cross);
    if (push)
        vec_scale(&n, (f32)((f64)absf(d_to) * 1.02), push);
    return 1;
}

// N64 0x8012D404: a sphere at `center` against one wall: 1 with the overlap in *depth when it
// touches the wall's quad (or one of its first two corners).
s32 run_step_corner(u32 wall, const Vec3 *center, f32 radius, f32 *depth) {
    Vec3 c0;
    vec_from_s16_mem(wall, &c0);
    Vec3 n = wall_normal(wall);
    f32 dist = plane_distance(&c0, &n, center);
    f32 over = dist - radius;
    if (!((f64)over < 0.0))
        return 0;
    f64 r = radius;
    if (!(r == 0.0) && !(-radius < dist))
        return 0;
    Vec3 push, q;
    vec_scale(&n, -dist, &push);
    vec_add(center, &push, &q);
    f32 near = (f32)(r * 0.98);
    if (!quad_contains_point(wall, &q)) {
        vec_from_s16_mem(wall, &c0);
        if (!(dist_to(&c0, &q) < near)) {
            vec_from_s16_mem(wall + 6, &c0);
            if (!(dist_to(&c0, &q) < near))
                return 0;
        }
    }
    *depth = absf(over);
    return 1;
}

// N64 0x8012CB8C: pushes the wrestler (and whatever moves with it) back inside its collision set.
// Returns the direction bits (+0x26 & 0xF00) of the walls it touched.
//   other / other2: wrestlers moved by the same push; scale: the push is divided by it;
//   until: an animation player whose byte 0x11 limits when `other` moves; pts3 / pt: points pushed too;
//   mask: the directions allowed to push (0: all); joints: bit k includes joint k in the bounding
//   box (bit 1: point_120 +-30); apply: 0 only measures.
u16 keep_in_bounds(Wrestler *w, Wrestler *other, Wrestler *other2, f32 scale, const u8 *until, Vec3 *pts3, Vec3 *pt,
                   u16 mask, u32 joints, s32 apply) {
    if (!(g_match_flags[0] & 0x10))
        return 0;
    Vec3 center;
    f32 radius = 0.0f;
    if ((w->location & 3) == 2) {
        // On the apron: the position, 10 up; along the apron line the joint centre.
        center = w->pos;
        center.y = (f32)((f64)center.y + 10.0);
        radius = 30.0f;
        if (w->flags8a & 0x800) {
            switch (w->ring_zone) {
            case 0: case 2: center.x = w->point_120.x; break;
            case 1: case 3: center.z = w->point_120.z; break;
            default: break;
            }
        }
    } else if (w->location & 0x400) {
        center = w->point_120;
        radius = 30.0f;
    } else {
        // A box round point_120 (and the joints asked for); the radius is its larger xz half-size.
        Vec3 lo = w->point_120, hi = w->point_120;
        if (joints & 2) {
            lo.x = (f32)((f64)lo.x - 30.0);
            hi.x = (f32)((f64)hi.x + 30.0);
            lo.y = (f32)((f64)lo.y - 30.0);
            hi.y = (f32)((f64)hi.y + 30.0);
            lo.z = (f32)((f64)lo.z - 30.0);
            hi.z = (f32)((f64)hi.z + 30.0);
        }
        u32 bit = 4;
        for (s32 k = 2; k < 20; k++, bit <<= 1) {
            if (!(joints & bit))
                continue;
            const Vec3 *j = wrestler_joint(w, k - 1);
            if (j->x < lo.x) lo.x = j->x;
            if (hi.x < j->x) hi.x = j->x;
            if (j->y < lo.y) lo.y = j->y;
            if (hi.y < j->y) hi.y = j->y;
            if (j->z < lo.z) lo.z = j->z;
            if (hi.z < j->z) hi.z = j->z;
        }
        center.x = (f32)((f64)(lo.x + hi.x) * 0.5);
        center.y = (f32)((f64)(lo.y + hi.y) * 0.5);
        center.z = (f32)((f64)(lo.z + hi.z) * 0.5);
        f32 half_x = (f32)((f64)(hi.x - lo.x) * 0.5);
        radius = (f32)((f64)(hi.z - lo.z) * 0.5);
        if (radius < half_x)
            radius = half_x;
        if (w->location & 0x340)
            radius = 30.0f;
        if (w->state == state::GroundHoldDefender && w->state_step == 1)
            radius = 30.0f;
        center.y = (f32)((f64)w->pos.y + 10.0);
    }

    u32 set = w->bounds_override;
    if (set == 0) {
        u16 loc = w->location;
        if (loc & 0x001) {
            set = 0x80146BF0;
        } else if (loc & 0x002) {
            set = mem_u32(0x8014D390 + 4 * w->ring_zone);
        } else if (loc & 0x004) {
            s32 arena = mem_s32(A_ARENA);
            s32 area = arena < 10 ? 0 : arena - 9;
            set = mem_u32(0x8014D290 + 16 * area + 4 * ring_zone_of(&w->point_120));
        } else if (loc & 0x040) {
            set = 0x80147E20;
        } else if (loc & 0x080) {
            set = 0x8014BC8C;
        } else if (loc & 0x100) {
            set = 0x8014B704;
        } else if (loc & 0x400) {
            set = mem_u32(0x8014D3A0 + 4 * ring_zone_of(&w->point_120));
        } else {
            set = 0x8014C500;
        }
    }

    // The nearest wall, then the nearest other one after the first push.
    u16 dirs = 0;
    u32 first = 0, second = 0;
    Vec3 push = {0.0f, 0.0f, 0.0f};
    f32 best = 10000.0f;
    for (u32 g = set; mem_u32(g) != 0; g += 4) {
        u32 group = mem_u32(g);
        for (u32 wall = mem_u32(group);; wall = mem_u32(wall + WALL_NEXT)) {
            f32 depth;
            if (run_step_corner(wall, &center, radius, &depth) && depth < best) {
                best = depth;
                first = wall;
            }
            if (wall == mem_u32(group + 4))
                break;
        }
    }
    if (first) {
        dirs |= mem_u16(first + WALL_DIRS) & 0xF00;
        Vec3 n = wall_normal(first);
        vec_scale(&n, best, &push);
        vec_add(&center, &push, &center);
        best = 10000.0f;
        for (u32 g = set; mem_u32(g) != 0; g += 4) {
            u32 group = mem_u32(g);
            for (u32 wall = mem_u32(group);; wall = mem_u32(wall + WALL_NEXT)) {
                f32 depth;
                if (wall != first && run_step_corner(wall, &center, radius, &depth) && depth < best) {
                    best = depth;
                    second = wall;
                }
                if (wall == mem_u32(group + 4))
                    break;
            }
        }
        if (second) {
            dirs |= mem_u16(second + WALL_DIRS) & 0xF00;
            Vec3 n2 = wall_normal(second), p2;
            vec_scale(&n2, best, &p2);
            vec_add(&push, &p2, &push);
        }
    }

    if (apply) {
        if (mask == 0)
            mask = 0xF00;
        if (dirs == 0 && first != 0) {
            push.x = push.x / scale;
            push.z = push.z / scale;
            dirs = 0xF00;
        } else {
            if ((dirs & mask) & 0x300)
                push.x = push.x / scale;
            else
                push.x = 0.0f;
            if ((dirs & mask) & 0xC00)
                push.z = push.z / scale;
            else
                push.z = 0.0f;
        }
        push.y = push.y / scale;
        wrestler_translate(w, &push);
        if (!(until && until[AP_PAIR_FRAME] < w->anim_frame)) {
            if (other)
                wrestler_translate(other, &push);
            if (other2)
                wrestler_translate(other2, &push);
        }
        if (pts3) {
            vec_add(&pts3[0], &push, &pts3[0]);
            vec_add(&pts3[1], &push, &pts3[1]);
            vec_add(&pts3[2], &push, &pts3[2]);
        }
        if (pt)
            vec_add(pt, &push, pt);
    }

    if (!first)
        return dirs;
    if (!second) {
        w->zone_info = first;
        w->zone_info2 = first;
        return dirs;
    }
    // Two walls: the one the wrestler faces more squarely is zone_info.
    Vec3 f;
    vec_from_angle(w->facing, 1.0f, &f);
    Vec3 n1 = wall_normal(first), n2 = wall_normal(second);
    f32 a = absf(vec_normalize_two(&f, &n1));
    f32 b = absf(vec_normalize_two(&f, &n2));
    if (b <= a) {
        w->zone_info = first;
        w->zone_info2 = second;
    } else {
        w->zone_info = second;
        w->zone_info2 = first;
    }
    return dirs;
}

// N64 0x8012C928
u16 keep_in_bounds_self(Wrestler *w, f32 scale, u16 mask, u32 joints) {
    return keep_in_bounds(w, nullptr, nullptr, scale, nullptr, nullptr, nullptr, mask, joints, 1);
}

// ---- wrestler against wrestler ----

// N64 0x8012EB34: from a to b: the difference, the three planar distances and the three planar
// angles; returns the xz distance.
f32 segment_distance(const Vec3 *a, const Vec3 *b, f32 out[9]) {
    Vec3 d;
    vec_sub(a, b, &d);
    f32 r[9];
    r[0] = d.x;
    r[1] = d.y;
    r[2] = d.z;
    r[3] = vec2_len(d.y, d.z);
    r[4] = vec2_len(d.x, d.z);
    r[5] = vec2_len(d.x, d.y);
    r[6] = (f32)vec_xz_angle(d.z, d.y);
    r[7] = (f32)vec_xz_angle(d.x, d.z);
    r[8] = (f32)vec_xz_angle(d.x, d.y);
    if (out)
        for (s32 i = 0; i < 9; i++)
            out[i] = r[i];
    return r[4];
}

// N64 0x8012E434: the radius of an elliptical body toward `point` (the body is rx by rz, turned by
// its angle; a zero radius becomes 0.1, in the body itself).
f32 lying_body_dist(Body *b, const Vec3 *point) {
    if ((f64)b->rx == 0.0)
        b->rx = 0.1f;
    if ((f64)b->rz == 0.0)
        b->rz = 0.1f;
    Vec3 d, r;
    vec_sub(&b->center, point, &d);
    point_at_offset(nullptr, &d, angle_diff(0, b->angle), &r);
    if ((f64)r.x == 0.0)
        return b->rz;
    if ((f64)r.z == 0.0)
        return b->rx;
    f32 t = r.z / r.x;
    f32 rx2 = b->rx * b->rx;
    f32 rz2 = b->rz * b->rz;
    f32 k = rz2 / rx2;
    k = k + t * t;
    k = rz2 / k;
    f32 u = k / rx2;
    f32 rest = (f32)((1.0 - (f64)u) * (f64)rz2);
    return sqrtf_n64(k + rest);
}

// N64 0x8012E578: the body of a lying wrestler for collision: an ellipse 36 wide and half the
// longer of the shoulder / hip joint pairs long (at least 29.7), along the mean angle of that
// pair, placed from the pair's midpoint. Up on the cage wall (location 0x400) the body lies in the
// wall's plane: joints 17 / 9 / 15 / 8 (0x8014DAB0) with y swapped for z (joint_zone 0 / 2) or for x
// (1 / 3), the pairs 17-8 and 9-15, placed from the midpoint of the first two.
void lying_body_axis(Wrestler *w, Body *out) {
    if (w->location & 0x400) {
        Vec3 p[4];
        for (s32 k = 0; k < 4; k++) {
            p[k] = *wrestler_joint(w, mem_s8(0x8014DAB0 + (u32)k) - 1);
            s32 jz = w->joint_zone;
            if (jz == 0 || jz == 2) {
                f32 y = p[k].y;
                p[k].y = p[k].z;
                p[k].z = y;
            } else if (jz == 1 || jz == 3) {
                f32 y = p[k].y;
                p[k].y = p[k].x;
                p[k].x = y;
            }
        }
        f32 r0[9], r1[9];
        segment_distance(&p[0], &p[3], r0);
        segment_distance(&p[1], &p[2], r1);
        s32 d = angle_diff((s32)r0[7], (s32)r1[7]);
        out->angle = angle_add((s32)r1[7], d / 2);
        out->rx = 36.0f;
        out->rz = (f32)((f64)(r0[4] + r1[4]) * 0.25);
        out->ry = 0.0f;
        if ((f64)out->rz < 29.7)
            out->rz = 29.7f;
        Vec3 mid, off;
        mid.x = (f32)((f64)(p[1].x - p[0].x) * 0.5 + (f64)p[0].x);
        mid.y = 0.0f;
        mid.z = (f32)((f64)(p[1].z - p[0].z) * 0.5 + (f64)p[0].z);
        off.x = 0.0f;
        off.y = 0.0f;
        off.z = out->rz;
        point_at_offset(&mid, &off, out->angle, &out->center);
        out->center.y = 0.0f;
        return;
    }
    const Vec3 *A = wrestler_joint(w, 14), *B = wrestler_joint(w, 4), *C = wrestler_joint(w, 7);
    const Vec3 *D = wrestler_joint(w, 0), *E = wrestler_joint(w, 16), *F = wrestler_joint(w, 8);
    f32 r[8][9];
    segment_distance(A, B, r[0]);
    segment_distance(C, B, r[1]);
    segment_distance(A, D, r[2]);
    segment_distance(C, D, r[3]);
    segment_distance(E, A, r[4]);
    segment_distance(F, C, r[5]);
    segment_distance(E, B, r[6]);
    segment_distance(F, B, r[7]);
    const f32 *s1, *s0;
    s32 j_far = 15, j_near = 8;
    if (r[2][4] + r[3][4] < r[0][4] + r[1][4]) {
        s1 = r[0];
        s0 = r[1];
    } else {
        s1 = r[2];
        s0 = r[3];
    }
    if (s1[4] + s0[4] < r[4][4] + r[5][4]) {
        s1 = r[4];
        s0 = r[5];
        j_far = 17;
        j_near = 9;
    }
    if (s1[4] + s0[4] < r[6][4] + r[7][4]) {
        s1 = r[6];
        s0 = r[7];
        j_far = 17;
        j_near = 9;
    }
    s32 d = angle_diff((s32)s1[7], (s32)s0[7]);
    out->angle = angle_add((s32)s0[7], d / 2);
    out->rx = 36.0f;
    out->rz = (f32)((f64)(s1[4] + s0[4]) * 0.25);
    out->ry = 0.0f;
    if ((f64)out->rz < 29.7)
        out->rz = 29.7f;
    const Vec3 *jn = wrestler_joint(w, j_near - 1), *jf = wrestler_joint(w, j_far - 1);
    Vec3 mid, off;
    mid.x = (f32)((f64)(jn->x - jf->x) * 0.5 + (f64)jf->x);
    mid.y = 0.0f;
    mid.z = (f32)((f64)(jn->z - jf->z) * 0.5 + (f64)jf->z);
    off.x = 0.0f;
    off.y = 0.0f;
    off.z = out->rz;
    point_at_offset(&mid, &off, out->angle, &out->center);
    out->center.y = 0.0f;
}

// N64 0x800EBD2C: everyone (and every standing ladder) near enough to collide with, nearest first.
// entries[10] holds the wrestler's own body.
void move_step_collide(Wrestler *w, Contact entries[11]) {
    for (s32 i = 10; i >= 0; i--)
        entries[i].slot = -1;
    Body *self = &entries[10].body;
    if ((w->flags80 & 8) || (w->flags8a & 0x2000) || (w->location & 0x400)) {
        lying_body_axis(w, self);
    } else {
        self->ry = 0.0f;
        self->angle = w->facing;
        self->rx = 33.0f;
        self->rz = 33.0f;
        self->center = w->point_120;
    }
    s32 n = 0;
    f32 out[9];
    for (s32 i = 0; i < 4; i++) {
        Wrestler *o = &g_wrestlers[i];
        if (o->roster_id == -1)
            continue;
        if (o->slot == w->slot)
            continue;
        if (o->state == state::RunIn && o->state_step < 15)
            continue;
        if (!(w->location & o->location & 0x7FF) && !((w->location | o->location) & 0x1000))
            continue;
        if (o->flags8a & 0x40)
            continue;
        if ((o->location & 0x400) && o->joint_zone != w->ring_zone)
            continue;
        Contact *e = &entries[n];
        if ((o->flags80 & 8) || (o->flags8a & 0x2000) || (o->location & 0x400)) {
            lying_body_axis(o, &e->body);
        } else {
            e->body.rx = 33.0f;
            e->body.rz = 33.0f;
            e->body.ry = 0.0f;
            e->body.angle = o->facing;
            e->body.center = o->point_120;
        }
        segment_distance(&self->center, &e->body.center, out);
        e->slot = i;
        e->dist = out[4];
        e->angle = (s32)out[7];
        n++;
    }
    if (!(w->location & 2)) {
        for (s32 j = 0; j < 7; j++) {
            u32 obj = 0x8016AE10 + 180 * j;
            if (mem_s32(obj + 4) != 25)
                continue;
            u16 fl = mem_u16(obj + 0x20);
            if (!(fl & 4) || !(fl & 0x100))
                continue;
            f32 y = (f32)((f64)mem_f32(obj + 0x48) - 126.0);
            if (w->location & 0x1000)
                y = w->pos.y;
            if (!((f64)absf(y - w->pos.y) < 150.0))
                continue;
            Contact *e = &entries[n];
            e->body.rx = 60.0f;
            e->body.rz = 60.0f;
            e->body.ry = 0.0f;
            e->body.angle = 0;
            e->body.center.x = mem_f32(obj + 0x44);
            e->body.center.y = mem_f32(obj + 0x48);
            e->body.center.z = mem_f32(obj + 0x4C);
            segment_distance(&self->center, &e->body.center, out);
            e->slot = j + 100;
            e->dist = out[4];
            e->angle = (s32)out[7];
            n++;
        }
    }
    for (s32 i = 0; i < n - 1; i++) {
        for (s32 k = i + 1; k < n; k++) {
            if (entries[k].dist < entries[i].dist) {
                Contact t = entries[i];
                entries[i] = entries[k];
                entries[k] = t;
            }
        }
    }
}

// N64 0x8012C474: pushes the wrestler (and `other`) out of everyone it overlaps. Returns the slot
// of the nearest one it overlapped, or -1. mode 0 only measures; 2 is the walk's own rule.
s32 wrestler_move_step(Wrestler *w, Wrestler *other, f32 scale, u32 mask, s32 exclude, s32 mode) {
    if ((mask & 0xFFFF) == 0)
        mask = 0xF00;
    Contact e[11];
    move_step_collide(w, e);
    s32 hit = -1;
    Vec3 acc = {0.0f, 0.0f, 0.0f};
    Wrestler *o = nullptr;
    for (s32 i = 0; i < 10; i++) {
        if (e[i].slot == -1)
            continue;
        if (e[i].slot == exclude)
            continue;
        f32 reach = lying_body_dist(&e[10].body, &e[i].body.center);
        reach = reach + lying_body_dist(&e[i].body, &e[10].body.center);
        f32 over = -1.0f;
        if (e[i].slot < 4) {
            o = &g_wrestlers[e[i].slot];
            if (o->flags80 & 8)
                reach = (f32)((f64)reach + 20.0);
            else if (((o->flags8a & 0x2000) || (o->location & 0x400)) && !(o->flags8a & 0x4000) && pose_lying(o))
                reach = (f32)((f64)reach + 20.0);
        }
        if (e[i].dist <= reach)
            over = reach - e[i].dist;
        if ((f64)over == -1.0)
            continue;
        if (mode != 0) {
            Vec3 v;
            if (w->location & 0x400) {
                vec_from_angle(angle_add(e[i].angle, 2048), over, &v);
                f32 up;
                if (w->joint_zone & 1) {
                    up = v.x;
                    v.x = 0.0f;
                } else {
                    up = v.z;
                    v.z = 0.0f;
                }
                v.y = up;
                v.x = v.x / scale;
                v.z = v.z / scale;
                v.y = v.y / scale;
                if (w->state == state::DropDown)
                    v.y = 0.0f;
                wrestler_translate(w, &v);
                if (other)
                    wrestler_translate(other, &v);
            } else {
                s32 a = abs_i(angle_diff(w->facing, e[i].angle)) < 512 ? w->facing : e[i].angle;
                if (mode == 2) {
                    if (e[i].slot < 4 && (o->flags_e9 & 0x10))
                        a = e[i].angle;
                    else
                        a = angle_add(e[i].angle, -512);
                }
                vec_from_angle(angle_add(a, 2048), over, &v);
                if (mask & 0x300)
                    v.x = v.x / scale;
                else
                    v.x = 0.0f;
                if (mask & 0xC00)
                    v.z = v.z / scale;
                else
                    v.z = 0.0f;
                v.y = 0.0f;
                vec_add(&acc, &v, &acc);
            }
        }
        if (hit == -1)
            hit = e[i].slot;
        if (e[i].slot >= 100)
            w->contact = e[i].slot;
    }
    if (hit == -1 || mode == 0 || (w->location & 0x400))
        return hit;
    if (15.0 <= (f64)vec2_len(acc.x, acc.z)) {
        vec_normalize(&acc);
        vec_scale(&acc, 7.5f, &acc);
    }
    wrestler_translate(w, &acc);
    if (other)
        wrestler_translate(other, &acc);
    return hit;
}

// N64 0x8012C00C
s32 wrestler_slide(Wrestler *w, f32 scale) { return wrestler_move_step(w, nullptr, scale, 0, -1, 1); }

// N64 0x80131BD0: moves the wrestler and its joints.
void wrestler_translate(Wrestler *w, const Vec3 *d) {
    vec_add(&w->pos, d, &w->pos);
    for (s32 k = 1; k < 20; k++) {
        Vec3 *j = wrestler_joint(w, k - 1);
        vec_add(j, d, j);
    }
}

// N64 0x8012DDD8: the floor under (x, z): the first wall of the arena's floor table (0x8014DA6C by
// arena, 20-byte entries {group, area by ring zone x 4}) that a drop from y 10000 to -10000 crosses.
// *area (when given) gets that entry's collision set for the point's ring zone (0 for none); returns
// the height of y 0 above the wall's plane (absolute), or 0.
f32 arena_area_lookup(f32 x, f32 z, u32 *area) {
    Vec3 top = {x, 10000.0f, z}, bottom = {x, -10000.0f, z};
    s32 arena = mem_s32(A_ARENA);
    u32 e = mem_u32(0x8014DA6C + 4u * (u32)(arena < 10 ? 0 : arena - 9));
    u32 hit = 0;
    if (e) {
        for (;;) {
            u32 group = mem_u32(e);
            if (group) {
                for (u32 wall = mem_u32(group);; wall = mem_u32(wall + WALL_NEXT)) {
                    if (segment_hit(&top, &bottom, wall, nullptr, nullptr, 0)) {
                        hit = wall;
                        break;
                    }
                    if (wall == mem_u32(group + 4))
                        break;
                }
                if (hit)
                    break;
            } else if (!mem_u32(e + 4)) {
                break;
            }
            e += 20;
        }
    }
    if (!hit) {
        if (area)
            *area = 0;
        return 0.0f;
    }
    if (area)
        *area = mem_u32(e + 4 + 4u * (u32)ring_zone_of(&top));
    top.y = 0.0f;
    Vec3 c0;
    vec_from_s16_mem(hit, &c0);
    Vec3 n = wall_normal(hit);
    return absf(plane_distance(&c0, &n, &top));
}

// N64 0x8012ED58: which way a wall faces, as the direction bits a slide may use: within 2 degrees
// of the axis at 0x8014D19C (|cos| above 0.99939) 0x300, within 2 degrees of square to it 0xC00,
// else 0xF00.
u16 dist_to_ring_point(const Vec3 *n) {
    Vec3 axis = {mem_f32(0x8014D19C), mem_f32(0x8014D1A0), mem_f32(0x8014D1A4)};
    f64 c = (f64)absf(vec_normalize_two(n, &axis));
    u16 m = 0xF00;
    if (0.999390827 < c)
        m = 0x300;
    if (c < 0.034899496)
        m = 0xC00;
    return m;
}

// N64 0x8012C070: wrestler_move_step with a direction mask (the pushes it may make).
s32 wrestler_move_step_b(Wrestler *w, u16 mask, f32 scale) { return wrestler_move_step(w, nullptr, scale, mask, -1, 1); }

}  // namespace nm
