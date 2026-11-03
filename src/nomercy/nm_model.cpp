// The posing of a wrestler's model (main segment): the animation stream from the ROM, the keyframe
// decode and the in-between frames, the blend from the previous animation, the skin matrices and the
// render tree, the root anchoring on an animation switch, and the frame events.
//
// Matrices are row-major 4 x 4 with row vectors (v' = v * M), so a product A * B applies A first.
// The joint records live in the slot's model block (g_models), the render tree in g_model_nodes,
// the stream state in g_anim_streams; the keyframe buffers are heap memory in the RDRAM image,
// filled from the ROM through the host (the original DMAs them).
#include "nm_funcs.h"
#include <cstring>
#include "nm_host.h"

namespace nm {

namespace {

// ---- the skeleton (main data segment) ----
enum : u32 {
    A_BIND_XY = 0x80047ED0,       // s8 (x, y) per joint 0-19: the bind pose
    A_JOINT_ORDER = 0x80047EF8,   // s8[20]: joints 1-19 in evaluation order (parents first), [0] = 0
    A_JOINT_PARENT = 0x80047F0C,  // s8[20]: parent per joint, 0 none
    A_ROPE_KINDS = 0x80047F20,    // s8[20]: rope push kind per frame-event code 200-219; also the limb of codes 240-243
    A_LIMB_JOINTS = 0x80047F38,   // s8[8]: the joint whose world point places limb 1-7 on a rope
    A_JOINT_FIT = 0x80047F40,     // s8[20]: -1 for a joint drawn at its bind offset, else a row of A_FIT_X
    A_FIT_SX = 0x80047F54,        // f32[5]: the model's part scale by build (x, y, z)
    A_FIT_SY = 0x80047F68,
    A_FIT_SZ = 0x80047F7C,
    A_FIT_Y = 0x80047FB8,         // f32[5]: part offset y by build
    A_FIT_Z = 0x80047FCC,         // f32[5]: part offset z by build
    A_FIT_X = 0x80047FE0,         // f32 *[]: rows of part offset x by build, picked by A_JOINT_FIT
    A_BLEND_WEIGHTS = 0x80047FF8, // f32[5]: 0.75 0.6 0.45 0.3 0.15, the blend from the previous pose
    A_INTERP_WEIGHTS = 0x8004800C,// f32[3]: 0.5 1/3 0.5, the in-between frame's step
    A_DEG_TO_RAD = 0x80047EC0,    // f32 pi / 180
    A_LAST_ABS_POS = 0x80069F10,  // Vec3: the last absolute joint position decoded (deltas add to it)
    A_BUILD = 0x8006A268,         // s8[4]: each slot's model build (1 from anim_system_init), indexes the fit tables
    A_ANIM_TABLE = 0x8006A224,    // pointer to file 703's 36-byte animation records
    A_FILE_STARTS = 0x80062C10,   // pointer to the archive's u32 file starts (bit 0: compressed)
    A_FILE_SIZES = 0x80062C14,    // pointer to the archive's u32 unpacked sizes
    A_SOUND_ON = 0x8009EDD4,      // s16: 1 when animation sound events play (anim_system_init)
    A_HAND_POSE = 0x80076588,     // u8[4]: each slot's hand shape 0-19
    A_LIMB_ROPE = 0x8006A228,     // s8[4][8]: the rope each limb holds, -1 none
    A_LIMB_SIDE = 0x8006A248,     // s8[4][8]: the ring side of that rope
    A_ROPE_LIMBS = 0x800845F0,    // s16 (slot, limb) per [side][rope]: who holds each rope (rope_attach)
    A_CARRY_SLOT = 0x8009FDE0,    // u16: 1 + the slot drawn with the height offset below, 0 none (set outside the match)
    A_CARRY_Y = 0x800A9352,       // s16: that slot's height offset
    A_NODES = 0x80091CC0,         // the render nodes: 28 of 220 bytes a slot
    A_ROM_FILES = 0x1BD150,       // ROM address of the archive's data
};
enum { NODE_SIZE = 220, NODES_PER_SLOT = 28 };

inline ModelJoint *joint(s32 slot, s32 j) { return &g_models[slot].joint[j - 1]; }
inline ModelNode *node(s32 slot, s32 j) { return &g_model_nodes[slot][j]; }
inline ModelNode *node_at(u32 addr) {
    u32 i = (addr - A_NODES) / NODE_SIZE;
    return &g_model_nodes[i / NODES_PER_SLOT][i % NODES_PER_SLOT];
}
inline u32 node_addr(const ModelNode *n) {
    u32 i = (u32)(n - &g_model_nodes[0][0]);
    return A_NODES + NODE_SIZE * i;
}

// ---- matrices (main segment) ----

// N64 0x80038820
void mtx_identity(f32 *m) {
    for (s32 i = 0; i < 16; i++)
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

// N64 0x80038AC0
void mtx_translate(f32 *m, f32 x, f32 y, f32 z) {
    mtx_identity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

// N64 0x80038A00
void mtx_scale(f32 *m, f32 x, f32 y, f32 z) {
    mtx_identity(m);
    m[0] = x;
    m[5] = y;
    m[10] = z;
}

// N64 0x80010FC4
void mtx_set_translation(f32 *m, f32 x, f32 y, f32 z) {
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

// N64 0x80010FD4: a turn of `deg` about y.
void mtx_rotate_y(f32 *m, f32 deg) {
    f32 r = deg * mem_f32(A_DEG_TO_RAD);
    f32 s = sinf_n64(r);
    f32 c = cosf_n64(r);
    m[8] = s;
    m[0] = c;
    m[1] = 0.0f;
    m[3] = 0.0f;
    m[4] = 0.0f;
    m[6] = 0.0f;
    m[7] = 0.0f;
    m[9] = 0.0f;
    m[10] = c;
    m[11] = 0.0f;
    m[12] = 0.0f;
    m[13] = 0.0f;
    m[14] = 0.0f;
    m[2] = -s;
    m[5] = 1.0f;
    m[15] = 1.0f;
}

// N64 0x80010E50: the joint rotation from three Euler angles in degrees (the animations' convention).
void mtx_rotate_joint(f32 *m, f32 ax, f32 ay, f32 az) {
    f32 k = mem_f32(A_DEG_TO_RAD);
    f32 rx = ax * k;
    f32 ry = ay * k;
    f32 rz = az * k;
    f32 sx = sinf_n64(rx);
    f32 cx = cosf_n64(rx);
    f32 sy = sinf_n64(ry);
    f32 cy = cosf_n64(ry);
    f32 sz = sinf_n64(rz);
    f32 cz = cosf_n64(rz);
    f32 czcy = cz * cy;
    f32 czsy = cz * sy;
    f32 szcx = sz * cx;
    f32 szsx = sz * sx;
    f32 nsz = -sz;
    f32 szsy = sz * sy;
    f32 czcx = cz * cx;
    f32 czsx = cz * sx;
    f32 nszcy = nsz * cy;
    f32 ncy = -cy;
    f32 ncysx = ncy * sx;
    f32 cycx = cy * cx;
    f32 czsysx = czsy * sx;
    f32 czsycx = czsy * cx;
    m[3] = 0.0f;
    m[7] = 0.0f;
    m[8] = sy;
    f32 szsysx = szsy * sx;
    m[11] = 0.0f;
    f32 m1 = szcx + czsysx;
    m[12] = 0.0f;
    m[13] = 0.0f;
    f32 m2 = szsx - czsycx;
    f32 szsycx = szsy * cx;
    m[14] = 0.0f;
    m[0] = czcy;
    f32 m5 = czcx - szsysx;
    m[10] = cycx;
    m[4] = nszcy;
    m[9] = ncysx;
    f32 m6 = czsx + szsycx;
    m[15] = 1.0f;
    m[1] = m1;
    m[2] = m2;
    m[5] = m5;
    m[6] = m6;
}

// N64 0x80036140: the body's rotation from three Euler angles in degrees (a different convention
// from mtx_rotate_joint: the x turn is applied first).
void mtx_rotate_xyz_b(f32 *m, f32 ax, f32 ay, f32 az) {
    f32 k = mem_f32(0x80053790);  // pi / 180 again
    f32 rx = ax * k;
    f32 ry = ay * k;
    f32 rz = az * k;
    f32 sx = sinf_n64(rx);
    f32 cx = cosf_n64(rx);
    f32 sy = sinf_n64(ry);
    f32 cy = cosf_n64(ry);
    f32 sz = sinf_n64(rz);
    f32 cz = cosf_n64(rz);
    mtx_identity(m);
    f32 cycz = cy * cz;
    f32 cysz = cy * sz;
    f32 cxcz = cx * cz;
    f32 cxsy = cx * sy;
    f32 sxsy = sx * sy;
    f32 cxsz = cx * sz;
    f32 cxcy = cx * cy;
    f32 cxsycz = cxsy * cz;
    f32 sxsycz = sxsy * cz;
    f32 sxsysz = sxsy * sz;
    f32 sxcy = sx * cy;
    f32 sxsz = sx * sz;
    f32 m4 = sxsycz - cxsz;
    f32 sxcz = sx * cz;
    f32 nsy = -sy;
    f32 cxsysz = cxsy * sz;
    f32 m5 = sxsysz + cxcz;
    m[2] = nsy;
    m[0] = cycz;
    f32 m8 = cxsycz + sxsz;
    m[1] = cysz;
    m[10] = cxcy;
    m[6] = sxcy;
    f32 m9 = cxsysz - sxcz;
    m[4] = m4;
    m[5] = m5;
    m[8] = m8;
    m[9] = m9;
}

// N64 0x80001E08: out = a * b (row vectors: a applied first).
void mtx_mul(const f32 *a, const f32 *b, f32 *out) {
    for (s32 i = 0; i < 4; i++) {
        f32 a0 = a[4 * i], a1 = a[4 * i + 1], a2 = a[4 * i + 2], a3 = a[4 * i + 3];
        for (s32 j = 0; j < 4; j++) {
            f32 s = a0 * b[j];
            f32 t1 = a1 * b[4 + j];
            f32 t2 = a2 * b[8 + j];
            f32 t3 = a3 * b[12 + j];
            s = s + t1;
            s = s + t2;
            s = s + t3;
            out[4 * i + j] = s;
        }
    }
}

// N64 0x80038B20: (x, y, z, 1) * m.
void mtx_mul_point(const f32 *m, f32 x, f32 y, f32 z, f32 *ox, f32 *oy, f32 *oz) {
    f32 a = m[0] * x;
    f32 b = m[4] * y;
    f32 c = m[8] * z;
    a = a + b;
    a = a + c;
    *ox = a + m[12];
    a = m[1] * x;
    b = m[5] * y;
    c = m[9] * z;
    a = a + b;
    a = a + c;
    *oy = a + m[13];
    a = m[2] * x;
    b = m[6] * y;
    c = m[10] * z;
    a = a + b;
    a = a + c;
    *oz = a + m[14];
}

// N64 0x80002268: world = local * parent's world down the tree (a root's world is its local
// matrix, which model_animate builds for the body).
void node_tree_update(ModelNode *n) {
    for (; n; n = n->next ? node_at(n->next) : nullptr) {
        if (n->parent == 0)
            std::memcpy(n->world, n->local, sizeof n->world);  // bcopy(local, world)
        else
            mtx_mul(n->local, node_at(n->parent)->world, n->world);
        if (n->child)
            node_tree_update(node_at(n->child));
    }
}

// ---- angles ----

// N64 0x8000567C
f32 absf_b(f32 x) {
    if ((f64)x < 0.0)
        x = -x;
    return x;
}

// N64 0x80011270: wraps an angle in degrees to (-180, 180].
f32 angle_wrap_deg(f32 x) {
    f64 d = (f64)x;
    if (0.0 <= d) {
        if (180.0 < d) {
            d = d - 360.0;
            for (;;) {
                x = (f32)d;
                if (!(180.0 < (f64)x))
                    break;
                d = (f64)x - 360.0;
            }
        }
    } else if (d <= -180.0) {
        d = (f64)x + 360.0;
        for (;;) {
            x = (f32)d;
            if (!((f64)x <= -180.0))
                break;
            d = (f64)x + 360.0;
        }
    }
    return x;
}

// N64 0x80011324: b - a, the short way round.
f32 angle_diff_deg(f32 a, f32 b) { return angle_wrap_deg(b - a); }

// ---- the animation stream ----

// N64 0x80003BD0: archive file `id` (1-19458): out[0] / out[1] its ROM start and end, out[2] its
// unpacked size, out[3] its packed length (0 when stored raw).
void file_lookup(u32 *out, u16 id) {
    if (id == 0 || id >= 19459)
        NM_UNPORTED("file_lookup: a file number out of range (the original hangs)");
    u32 e = mem_u32(A_FILE_STARTS) + 4u * (u32)(id - 1);
    u32 start = mem_u32(e);
    u32 end = mem_u32(e + 4);
    out[0] = (start & ~1u) + A_ROM_FILES;
    out[1] = (end & ~1u) + A_ROM_FILES;
    out[2] = mem_u32(mem_u32(A_FILE_SIZES) + 4u * (u32)(id - 1));
    out[3] = (start & 1) ? out[1] - out[0] : 0;
}

// N64 0x80011340: fetches the next keyframe (at most 98 bytes) into buf[cur_buf], waits for the
// fetch before it, and makes the buffer that holds that one current. `wait` is the DMA priority.
void anim_stream_fetch(s32 slot, s32 wait) {
    AnimStream *st = &g_anim_streams[slot];
    if (st->bytes_left == 0)
        return;
    u8 cur = st->cur_buf;
    s32 n = st->bytes_left < 98 ? st->bytes_left : 98;
    (void)wait;
    // The original starts a PI DMA of n bytes from rom_next to buf[cur] (OSIoMesg at +0x88 + 24 *
    // cur, reply queue at +0x58 + 24 * cur); the host copies them now.
    host_rom_read(st->rom_next, g_ram + ram_off(st->buf[cur]), (u32)n);
    cur ^= 1;
    if (st->dma_pending) {
        // osRecvMesg on the other buffer's queue: that fetch is complete here.
    }
    st->cur_buf = cur;
    st->dma_pending = 1;
    st->rom_next = st->rom_next + (u32)n;
    st->bytes_left = st->bytes_left - n;
}

// N64 0x80012B24: moves the stream on a keyframe: fetches the one after next, or when the file is
// exhausted waits for the last fetch and makes its buffer current.
void anim_stream_advance(s32 slot) {
    AnimStream *st = &g_anim_streams[slot];
    if (st->bytes_left != 0) {
        anim_stream_fetch(slot, 0);
        return;
    }
    if (st->dma_pending) {
        u8 cur = st->cur_buf ^ 1;
        // osRecvMesg on that buffer's queue
        st->cur_buf = cur;
        st->dma_pending = 0;
    }
}

// N64 0x80012A8C: starts streaming animation `anim` (an archive file stored raw): the first
// keyframe, then the second.
void anim_stream_open(s32 slot, s16 anim) {
    AnimStream *st = &g_anim_streams[slot];
    u32 info[4];
    file_lookup(info, (u16)anim);
    st->rom_next = info[0] + 4;
    st->bytes_left = (s32)info[2] - 4;
    anim_stream_fetch(slot, 1);
    st->anim = anim;
    anim_stream_advance(slot);
}

// N64 0x800135D0: decodes one joint: a word with three 10-bit angles (x 360 / 1024 degrees); bit
// 31 adds a position, absolute (bit 30 clear: a word of 11 / 11 / 10-bit sign-magnitude units) or
// a delta from the last absolute one (bit 30 set: three sign-magnitude bytes x 0.5). Returns the
// next source byte.
const u8 *anim_decode_joint(const u8 *src, Vec3 *rot, Vec3 *pos) {
    u32 w = (u32)src[0] << 24 | (u32)src[1] << 16 | (u32)src[2] << 8 | src[3];
    src += 4;
    u32 has_pos = w & 0x80000000u;
    u32 is_delta = w & 0x40000000u;
    rot->x = (f32)((f64)(f32)(s32)(w & 0x3FF) * 0.3515625);
    rot->y = (f32)((f64)(f32)(s32)((w >> 10) & 0x3FF) * 0.3515625);
    rot->z = (f32)((f64)(f32)(s32)((w >> 20) & 0x3FF) * 0.3515625);
    if (!has_pos)
        return src;
    if (is_delta) {
        s32 x = src[0] & 0x7F, y = src[1] & 0x7F, z = src[2] & 0x7F;
        if (src[0] & 0x80)
            x = -x;
        if (src[1] & 0x80)
            y = -y;
        if (src[2] & 0x80)
            z = -z;
        src += 3;
        pos->x = (f32)x;
        pos->y = (f32)y;
        pos->z = (f32)z;
        pos->x = (f32)((f64)pos->x * 0.5);
        pos->z = (f32)((f64)pos->z * 0.5);
        pos->y = (f32)((f64)pos->y * 0.5);
        pos->x = pos->x + mem_f32(A_LAST_ABS_POS);
        pos->y = pos->y + mem_f32(A_LAST_ABS_POS + 4);
        pos->z = pos->z + mem_f32(A_LAST_ABS_POS + 8);
        return src;
    }
    u32 v = (u32)src[0] << 24 | (u32)src[1] << 16 | (u32)src[2] << 8 | src[3];
    src += 4;
    s32 x = (s32)(v & 0x3FF), y = (s32)((v >> 11) & 0x3FF), z = (s32)((v >> 22) & 0x1FF);
    if (v & 0x400)
        x = -x;
    if ((v >> 11) & 0x400)
        y = -y;
    if ((v >> 22) & 0x200)
        z = -z;
    pos->x = (f32)x;
    pos->y = (f32)y;
    pos->z = (f32)z;
    mem_wf32(A_LAST_ABS_POS, pos->x);
    mem_wf32(A_LAST_ABS_POS + 4, pos->y);
    mem_wf32(A_LAST_ABS_POS + 8, pos->z);
    return src;
}

// N64 0x80013234: decodes the current keyframe (buf[cur_buf]) into joints 1-19.
void model_decode_frame(s32 slot) {
    const AnimStream *st = &g_anim_streams[slot];
    const u8 *src = g_ram + ram_off(st->buf[st->cur_buf]);
    for (s32 j = 1; j < 20; j++) {
        ModelJoint *r = joint(slot, j);
        src = anim_decode_joint(src, &r->rot, &r->pos);
    }
}

// N64 0x800132D8: an in-between frame: moves the stream on, decodes the next keyframe and moves
// every joint toward it by the phase's weight (angles the short way round, with the Euler flip
// when x or z would turn more than 120 degrees).
void model_decode_frame_interp(s32 slot, s32 phase) {
    Vec3 trot[20], tpos[20];
    f32 w = mem_f32(A_INTERP_WEIGHTS + 4 * (u32)(phase % 3));
    anim_stream_advance(slot);
    const AnimStream *st = &g_anim_streams[slot];
    const u8 *src = g_ram + ram_off(st->buf[st->cur_buf]);
    for (s32 j = 1; j < 20; j++)
        src = anim_decode_joint(src, &trot[j], &tpos[j]);
    for (s32 k = 1; k < 20; k++) {
        s32 j = mem_s8(A_JOINT_ORDER + k);
        ModelJoint *r = joint(slot, j);
        if (mem_s8(A_JOINT_PARENT + j) == 0) {
            f32 d = tpos[j].x - r->pos.x;
            d = d * w;
            r->pos.x = r->pos.x + d;
            d = tpos[j].y - r->pos.y;
            d = d * w;
            r->pos.y = r->pos.y + d;
            d = tpos[j].z - r->pos.z;
            d = d * w;
            r->pos.z = r->pos.z + d;
        }
        trot[j].x = angle_diff_deg(r->rot.x, trot[j].x);
        trot[j].y = angle_diff_deg(r->rot.y, trot[j].y);
        trot[j].z = angle_diff_deg(r->rot.z, trot[j].z);
        if (120.0 < (f64)absf_b(trot[j].x) || 120.0 < (f64)absf_b(trot[j].z)) {
            trot[j].x = angle_wrap_deg(trot[j].x + 180.0f);
            trot[j].z = angle_wrap_deg(trot[j].z + 180.0f);
        }
        f32 d = trot[j].x * w;
        r->rot.x = r->rot.x + d;
        d = trot[j].y * w;
        r->rot.y = r->rot.y + d;
        d = trot[j].z * w;
        r->rot.z = r->rot.z + d;
    }
}

// N64 0x80012F48: the first frames of a new animation (0-4) lean toward the previous pose by the
// frame's weight. On frame 0 the previous pose is first re-centred on its pelvis (x and z; y too with
// mode bit 8) unless mode bit 4 is set.
void model_blend_prev(s32 slot, s32 frame, s8 mode) {
    if (frame == 0 && !(mode & 4)) {
        f32 bx = joint(slot, 1)->prev_pos.x;
        f32 bz = joint(slot, 1)->prev_pos.z;
        for (s32 j = 1; j < 20; j++) {
            ModelJoint *r = joint(slot, j);
            r->prev_pos.x = r->prev_pos.x - bx;
            r->prev_pos.z = r->prev_pos.z - bz;
        }
        if (mode & 8) {
            f32 by = joint(slot, 1)->prev_pos.y;
            for (s32 j = 1; j < 20; j++) {
                ModelJoint *r = joint(slot, j);
                r->prev_pos.y = r->prev_pos.y - by;
            }
        }
    }
    f32 w = mem_f32(A_BLEND_WEIGHTS + 4 * (u32)frame);
    for (s32 j = 1; j < 20; j++) {
        ModelJoint *r = joint(slot, j);
        if (mem_s8(A_JOINT_PARENT + j) == 0) {
            f32 d = r->prev_pos.x - r->pos.x;
            d = d * w;
            r->pos.x = r->pos.x + d;
            d = r->prev_pos.y - r->pos.y;
            d = d * w;
            r->pos.y = r->pos.y + d;
            d = r->prev_pos.z - r->pos.z;
            d = d * w;
            r->pos.z = r->pos.z + d;
        }
        r->prev_rot.x = angle_diff_deg(r->rot.x, r->prev_rot.x);
        r->prev_rot.y = angle_diff_deg(r->rot.y, r->prev_rot.y);
        r->prev_rot.z = angle_diff_deg(r->rot.z, r->prev_rot.z);
        if (110.0 < (f64)absf_b(r->prev_rot.x) && 110.0 < (f64)absf_b(r->prev_rot.z)) {
            r->prev_rot.x = angle_wrap_deg(r->prev_rot.x + 180.0f);
            r->prev_rot.z = angle_wrap_deg(r->prev_rot.z + 180.0f);
        }
        f32 d = r->prev_rot.x * w;
        r->rot.x = r->rot.x + d;
        d = r->prev_rot.y * w;
        r->rot.y = r->rot.y + d;
        d = r->prev_rot.z * w;
        r->rot.z = r->rot.z + d;
    }
}

// N64 0x80012BE0: walks the joints in order: a child's position is its parent's plus the bind
// offset turned by the parent's rotation; the joint matrix is its rotation and position; the
// node's local matrix is the part's fit (its bind offset, or the build's scale and offsets) times
// that. The joint's rot / pos are kept as the previous frame's.
void model_build_skin(s32 slot) {
    f32 m[20][16];
    f32 fit[16];
    for (s32 k = 1; k < 20; k++) {
        s32 j = mem_s8(A_JOINT_ORDER + k);
        ModelJoint *r = joint(slot, j);
        ModelNode *n = node(slot, j);
        s32 p = mem_s8(A_JOINT_PARENT + j);
        s32 bx = mem_s8(A_BIND_XY + 2 * j), by = mem_s8(A_BIND_XY + 2 * j + 1);
        if (p != 0) {
            s32 px = mem_s8(A_BIND_XY + 2 * p), py = mem_s8(A_BIND_XY + 2 * p + 1);
            mtx_mul_point(m[p], (f32)(bx - px), (f32)(by - py), 0.0f, &r->pos.x, &r->pos.y, &r->pos.z);
        }
        mtx_rotate_joint(m[j], r->rot.x, r->rot.y, r->rot.z);
        mtx_set_translation(m[j], r->pos.x, r->pos.y, r->pos.z);
        s32 fitrow = mem_s8(A_JOINT_FIT + j);
        if (fitrow == -1) {
            mtx_translate(fit, (f32)-bx, (f32)-by, 0.0f);
        } else {
            u32 b = 4 * (u32)mem_s8(A_BUILD + slot);
            mtx_scale(fit, mem_f32(A_FIT_SX + b), mem_f32(A_FIT_SY + b), mem_f32(A_FIT_SZ + b));
            f32 ty = mem_f32(A_FIT_Y + b) - (f32)by;
            f32 tx = mem_f32(mem_u32(A_FIT_X + 4 * (u32)fitrow) + b) - (f32)bx;
            mtx_set_translation(fit, tx, ty, mem_f32(A_FIT_Z + b));
        }
        mtx_mul(fit, m[j], n->local);
        r->prev_pos = r->pos;
        r->prev_rot = r->rot;
    }
}

// N64 0x80017160: the slot's hand shape (0-19).
void anim_hand_pose_set(s32 slot, s32 pose) {
    if (pose < 20)
        mem_w8(A_HAND_POSE + (u32)slot, (u8)pose);
}

// N64 0x8002240C: records who holds rope `rope` (0-2) on ring side `side`: the slot and its limb.
void rope_attach(s32 slot, s32 limb, s32 side, s32 rope) {
    u32 a = A_ROPE_LIMBS + 4 * (u32)rope + 12 * (u32)side;
    mem_w16(a, (u16)slot);
    mem_w16(a + 2, (u16)limb);
}

// N64 0x80013E40: lets go of the rope limb `limb` holds, if any.
void rope_release(s32 slot, s32 limb) {
    u32 a = A_LIMB_ROPE + 8 * (u32)slot + (u32)limb;
    s32 rope = mem_s8(a);
    if (rope < 0)
        return;
    rope_attach(slot, 0, mem_s8(A_LIMB_SIDE + 8 * (u32)slot + (u32)limb), rope);
    mem_w8(a, 0xFF);
}

// N64 0x80013EB0
void ropes_release_all(s32 slot) {
    for (s32 limb = 0; limb < 8; limb++)
        rope_release(slot, limb);
}

// The ring side (0-3) nearest a world point: by which diagonal half-plane (x, z) falls in.
s32 side_of_point(f32 x, f32 z) {
    if (z <= x)
        return (-z <= x) ? 1 : 2;
    return (-z <= x) ? 0 : 3;
}

// N64 0x80013860: fires the animation's (frame, event) byte pairs (file 703 record bytes 20-35)
// whose frame is `frame`: 1-169 a sound, 170-175 a special, 176-195 a hand shape, 200-219 a rope
// push, 220-243 a limb grabbing or releasing a rope.
void anim_frame_events(s32 slot, s32 frame) {
    const AnimStream *st = &g_anim_streams[slot];
    u32 ev = mem_u32(A_ANIM_TABLE) + 36 * (u32)(st->anim - 11737) + 20;  // anim_events_ptr (0x80011D30)
    if (ev == 0)
        return;
    const ModelBlock *mb = &g_models[slot];
    for (s32 i = 0; i < 8; i++, ev += 2) {
        s32 code = mem_u8(ev + 1);
        if (code == 0 || mem_u8(ev) != frame)
            continue;
        if (code < 170) {
            if (mem_s16(A_SOUND_ON) != 0)
                host_sfx_slot(slot, code | 0x100);
            continue;
        }
        if (code < 176) {
            host_anim_event_special(slot, code - 170);
            continue;
        }
        if (code < 200) {
            anim_hand_pose_set(slot, code - 176);
            continue;
        }
        if (code - 200 < 20) {
            s32 kind = mem_s8(A_ROPE_KINDS + (u32)(code - 200));
            const Vec3 *root = &mb->joint[0].pt_a_world;
            if (kind == 6 && 280.0 <= (f64)root->z)
                kind = 0;
            if (kind == 10 && (f64)mb->translation.y == 0.0)
                kind = 0;
            if (kind <= 0)
                continue;
            f32 wx = root->x, wz = root->z;
            s32 side = side_of_point(wx, wz);
            s32 quarter = (s32)(((f64)-mb->yaw + 45.0) * 0.011111111111111112);
            s32 facing_side = quarter + 2;
            if (kind == 9) {
                side = quarter + 3;
            } else if ((side & 1) != (facing_side & 1)) {
                if (side & 1)
                    side = (0.0f <= wz) ? 0 : 2;
                else
                    side = (0.0f <= wx) ? 1 : 3;
            }
            side = (s16)side % 4;
            f32 along = (side & 1) ? wz : wx;
            if (kind == 6 && 230.0f <= wz)
                kind = 12;
            host_rope_push(kind, side, along);
            continue;
        }
        s32 k = code - 220;
        if (k >= 20) {
            rope_release(slot, mem_s8(A_ROPE_KINDS + (u32)k));
            continue;
        }
        if (k % 10 == 0) {
            if (k < 10) {
                rope_release(slot, 1);
                rope_release(slot, 2);
                rope_release(slot, 3);
            } else {
                rope_release(slot, 5);
                rope_release(slot, 6);
                rope_release(slot, 7);
            }
            continue;
        }
        s32 limb, rope;
        if (k < 10) {
            rope = 2 - (k - 1) % 3;
            limb = (s32)((f64)(k - 1) * 0.3333333333333333) + 1;
        } else {
            rope = 2 - (k - 11) % 3;
            limb = (s32)((f64)(k - 11) * 0.3333333333333333) + 5;
        }
        const Vec3 *pt = &mb->joint[mem_s8(A_LIMB_JOINTS + (u32)limb) - 1].pt_a_world;
        s32 side = side_of_point(pt->x, pt->z);
        rope_attach(slot, limb, side, rope);
        rope_release(slot, limb);
        mem_w8(A_LIMB_ROPE + 8 * (u32)slot + (u32)limb, (u8)rope);
        mem_w8(A_LIMB_SIDE + 8 * (u32)slot + (u32)limb, (u8)side);
    }
}

// A tilt in 1/4096 turns (as the ladder states keep it) to whole degrees, the long way round
// ((4096 - t) * 360 / 4096, truncated and wrapped into one turn).
f32 tilt_to_degrees(s32 t) {
    if (t == 0)
        return 0.0f;
    s32 v = (4096 - t) * 360;
    s32 q = v / 4096;
    s32 r = q - (q / 4096) * 4096;
    return (f32)r;
}

// Builds the body's matrix from the yaw and tilts, the drawn translation and the stream's scale.
void body_matrix(s32 slot, f32 yaw, f32 tilt_x, f32 tilt_z, const Vec3 *tr, f32 *out) {
    f32 ry[16], rxyz[16], m[16];
    mtx_rotate_y(ry, yaw);
    mtx_rotate_xyz_b(rxyz, tilt_x, 0.0f, tilt_z);
    mtx_mul(rxyz, ry, m);
    mtx_set_translation(m, tr->x, tr->y, tr->z);
    mtx_mul(g_anim_streams[slot].scale, m, out);
}

}  // namespace

// The point (x, 0, z) on a ladder, turned by its tilts (+0x50, +0x54) and yaw (+0x5C), in 4096ths of a
// turn (ladder_pose, ladder_object_update; they build the rotation with mtx_rotate_xyz_b and mtx_mul_obj,
// 0x80038900, the same product as mtx_mul).
Vec3 ladder_point(u32 o, f32 x, f32 z) {
    f32 m[16], r[16];
    const f64 k = 0.087890625;
    mtx_rotate_xyz_b(m, (f32)((f64)mem_s32(o + 0x50) * k), 0.0f, (f32)((f64)mem_s32(o + 0x54) * k));
    mtx_rotate_xyz_b(r, 0.0f, (f32)((f64)-mem_s32(o + 0x5C) * k), 0.0f);
    mtx_mul(m, r, m);
    Vec3 p;
    mtx_mul_point(m, x, 0.0f, z, &p.x, &p.y, &p.z);
    return p;
}

// N64 0x80012130: poses wrestler `slot`'s model for `frame` of `anim`. Opens the stream on a
// change (or a restart at frame 0), decodes the frame (keyframes on even frames, the halfway
// pose on odd ones), blends the first five frames of a new animation from the previous pose
// (mode bit 2), aims the head by `lean`, builds the skin matrices, and with mode bit 1 moves
// *pos so that the root joint stays where the previous animation left it (x and z; y too with
// bit 8). Then the body's world matrix from pos (x mirrored), facing and the tilts, the whole
// render tree, the world points of every joint, and the frame's events. (The original takes pos
// and facing as one (x, y, z, facing) record.)
void model_animate(s32 slot, s32 anim, s32 frame, Vec3 *pos, s32 facing, const Vec3 *lean, s8 mode, s32 tilt_a,
                   s32 tilt_b) {
    AnimStream *st = &g_anim_streams[slot];
    ModelBlock *mb = &g_models[slot];
    s16 yoff = 0;
    if ((s16)slot == (s16)(mem_u16(A_CARRY_SLOT) - 1))
        yoff = mem_s16(A_CARRY_Y);
    s32 anchor = 0;
    s16 posed = 0;
    f32 tilt_x = tilt_to_degrees(tilt_a);
    f32 tilt_z = tilt_to_degrees(tilt_b);
    f32 yaw = -(f32)((f64)(facing + 2048) * 0.087890625);
    Vec3 saved = {0.0f, 0.0f, 0.0f};
    if ((s16)anim != st->anim) {
        st->frame = -1;
        anim_hand_pose_set(slot, 0);
        frame = 0;
    }
    if ((s16)frame == 0 && st->frame != 0) {
        st->same_anim = (u8)((s16)anim == st->anim);
        anim_stream_open(slot, (s16)anim);
        ropes_release_all(slot);
        st->anim = (s16)anim;
        if (st->first == 0) {
            saved = mb->joint[0].pt_a_world;
            anchor = 1;
        }
    }
    if ((s16)frame != st->frame) {
        st->frame = (s16)frame;
        s32 count = (s16)anim_frame_count((s16)anim);
        if ((s16)frame < count) {
            if ((frame & 1) && (s16)frame != count - 1) {
                model_decode_frame_interp(slot, 0);
            } else {
                model_decode_frame(slot);
                if ((s16)frame == count - 2)
                    anim_stream_advance(slot);
            }
            if ((mode & 2) && st->first == 0 && (s16)frame < 5 && st->same_anim == 0)
                model_blend_prev(slot, (s16)frame, mode);
            if (lean) {
                ModelJoint *head = &mb->joint[4];
                head->rot.x = (f32)((f64)lean->x + 180.0);
                head->rot.z = 180.0f;
                head->rot.y = lean->y;
            }
            model_build_skin(slot);
            posed = 1;
        }
    }
    if ((mode & 1) && anchor) {
        // The root joint's place under the previous translation and the new pose: the body matrix
        // goes straight into node 1's parent's world matrix, node 1 follows.
        ModelNode *n1 = node(slot, 1);
        body_matrix(slot, yaw, tilt_x, tilt_z, &mb->translation, node_at(n1->parent)->world);
        mtx_mul(n1->local, node_at(n1->parent)->world, n1->world);
        ModelJoint *root = &mb->joint[0];
        mtx_mul_point(n1->world, root->pt_a.x, root->pt_a.y, root->pt_a.z, &root->pt_a_world.x,
                      &root->pt_a_world.y, &root->pt_a_world.z);
        root->pt_a_world.x = -root->pt_a_world.x;
        f32 d = saved.x - root->pt_a_world.x;
        pos->x = pos->x + d;
        d = saved.z - root->pt_a_world.z;
        pos->z = pos->z + d;
        if (mode & 8) {
            d = saved.y - root->pt_a_world.y;
            pos->y = pos->y + d;
        }
    }
    mb->translation.x = -pos->x;
    mb->translation.y = pos->y + (f32)yoff;
    mb->translation.z = pos->z;
    body_matrix(slot, yaw, tilt_x, tilt_z, &mb->translation, node(slot, 0)->local);
    node_tree_update(node(slot, 0));
    mb->yaw = yaw;
    for (s32 j = 1; j < 23; j++) {
        if (j == 20 || j == 21)
            continue;
        ModelJoint *r = joint(slot, j);
        const ModelNode *n = node(slot, j);
        r->yaw = r->rot.y + yaw;
        mtx_mul_point(n->world, r->pt_a.x, r->pt_a.y, r->pt_a.z, &r->pt_a_world.x, &r->pt_a_world.y, &r->pt_a_world.z);
        r->pt_a_world.x = -r->pt_a_world.x;
        mtx_mul_point(n->world, r->pt_b.x, r->pt_b.y, r->pt_b.z, &r->pt_b_world.x, &r->pt_b_world.y, &r->pt_b_world.z);
        r->pt_b_world.x = -r->pt_b_world.x;
    }
    if (posed)
        anim_frame_events(slot, (s16)(frame + 1));
    if (st->first)
        st->first = 0;
}

// The part of objects_draw (N64 0x80025F40, main segment, run by the draw after each tick) that the
// match logic reads back: each posed object's matrix (its model record's angles in 4096ths, as
// degrees, then the yaw, then the place at +4) under its holder's hand node (joint 17's, which
// object_model_update hangs it from), its probe points (the per-type s8 offsets at 0x80052914 / 0x800529E4 /
// 0x80052AB4, 8 a type, as many as the record's +1) into the record's +0x2C, and its origin into +0x20.
// object_fly and weapon_release read them on the next tick; the rest of the draw (the render tree,
// lighting, the ladder camera) is left to the host.
void objects_probe_points() {
    for (s32 j = 0; j < 7; j++) {
        u32 r = 0x800A9C70 + 140u * (u32)j;
        s32 type = mem_s8(r + 3);
        if (type == -1)
            continue;
        if (type >= 31)
            NM_UNPORTED("objects_probe_points: a move's debris object (sub_8002365C)");
        const f64 k = 0.087890625;
        f32 rot[16], yaw[16], local[16], world[16];
        mtx_rotate_joint(rot, (f32)((f64)mem_s32(r + 0x10) * k), (f32)((f64)mem_s32(r + 0x14) * k),
                         (f32)((f64)mem_s32(r + 0x18) * k));
        mtx_rotate_y(yaw, (f32)((f64)mem_s32(r + 0x1C) * k));
        mtx_mul(rot, yaw, local);
        mtx_set_translation(local, mem_f32(r + 4), mem_f32(r + 8), mem_f32(r + 0xC));
        // The node's parent (node +0) is the hand node of the holder the record was posed for (+2).
        s32 holder = mem_s8(r + 2);
        if (holder >= 0)
            mtx_mul(local, node(holder, 17)->world, world);
        else
            std::memcpy(world, local, sizeof world);
        s32 n = mem_s8(r + 1);
        for (s32 p = 0; p < n; p++) {
            u32 t = 8u * (u32)type + (u32)p;
            f32 x, y, z;
            mtx_mul_point(world, (f32)mem_s8(0x80052914 + t), (f32)mem_s8(0x800529E4 + t), (f32)mem_s8(0x80052AB4 + t),
                          &x, &y, &z);
            u32 q = r + 0x2C + 12u * (u32)p;
            mem_wf32(q, x);
            mem_wf32(q + 4, y);
            mem_wf32(q + 8, z);
        }
        f32 x, y, z;
        mtx_mul_point(world, 0.0f, 0.0f, 0.0f, &x, &y, &z);
        mem_wf32(r + 0x20, x);
        mem_wf32(r + 0x24, y);
        mem_wf32(r + 0x28, z);
    }
}

}  // namespace nm
