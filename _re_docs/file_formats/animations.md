# Animations and the skeleton (WCW vs. nWo: World Tour)

Every wrestler shares one 19-joint skeleton. An animation stores, for each keyframe, an
absolute rotation for every joint plus positions for the 7 joints that have no parent. All
2598 animation files (archive files 2901–5498, animation id = file number) parse with the
rules below, and the previewer built from them matches the poses seen in the live snapshots
of `individual_games/wcw_world_tour/match_state.md` (rear hold 4804, lying at the end of
3497, stance 2919).

Tool: `tools/rom/wcw_models.py ROM anim ID [--frames]` prints a decoded animation, and
`viewer OUT.html` builds the previewer (see the end of this file). Function and table names
below are in `symbols/wcw/models.toml`.

## Skeleton (code: `models_init`, `model_build_skin`)

Three tables in the main data segment:

| Table | Address | Contents |
|-------|---------|----------|
| `g_bind_xy` | `0x8002D3A0` | s8 (x, y) per joint 0–19; z is always 0 |
| `g_joint_order` | `0x8002D3C9` | 19 joint numbers, parents before children |
| `g_joint_parent` | `0x8002D3DC` | parent per joint; 0 means no parent |

Joint 0 is the model root and is never animated. Read from the US ROM, the skeleton is:

| Joint | Bind (x, y) | Parent | Body part (from the meshes bound to it) |
|-------|-------------|--------|------|
| 1 | (0, 10) | – | pelvis |
| 2 | (0, 23) | – | waist |
| 3 | (0, 35) | – | chest |
| 4 | (0, 58) | 3 | neck |
| 5 | (0, 72) | 4 | head |
| 7 | (9, 6) | – | hip / thigh, +x side |
| 6 | (9, −31) | 7 | knee / shin |
| 8 | (9, −74) | 6 | ankle / foot |
| 12 | (22, 56) | – | shoulder / upper arm, +x side |
| 11 | (24, 29) | 12 | elbow / forearm |
| 9 | (24, 7) | 11 | wrist / hand |
| 10 | (24, −4) | 9 | fingers |
| 14, 13, 15 | x mirrored from 7, 6, 8 | 14 → 13 → 15 | −x leg |
| 19, 16, 17, 18 | x mirrored from 12, 11, 9, 10 | 19 → 16 → 17 → 18 | −x arm |

Rows are in evaluation order. The bind pose stands upright with the arms hanging down and
the soles at y ≈ −88. In animations the standing pelvis is at y ≈ 88, which puts the feet
near y = 0, so **model y = 0 is the mat**.

### Posing (exact order of `model_build_skin`)

For each joint `j` in `g_joint_order`:

```
R[j] = mtx_rotate_xyz(rot[j])                        // from the animation
if parent[j] == 0:  P[j] = pos[j]                    // from the animation
else:               P[j] = P[p] + (bind[j] - bind[p]) * R[p]   // p = parent[j]
skin[j] = translate(-bind[j]) * [R[j] | P[j]]        // row vectors: v' = v * M
```

So joint rotations are **absolute in model space**, not relative to the parent. The parent
only places the child's pivot. A vertex `v` (stored in bind pose, model space) bound to joint
`j` ends up at `(v - bind[j]) * R[j] + P[j]`.

`mtx_rotate_xyz(ax, ay, az)` (degrees, `func_80008BD0`), with `s/c` = sin/cos of each angle,
gives the row-vector matrix:

```
[ cz*cy,   sz*cx + cz*sy*sx,   sz*sx - cz*sy*cx ]
[ -sz*cy,  cz*cx - sz*sy*sx,   cz*sx + sz*sy*cx ]
[ sy,      -cy*sx,             cy*cx            ]
```

The finished skin matrices go into the render nodes (`g_model_nodes`, node `j` for joint
`j`); the wrestler's world placement, yaw and a per-wrestler scale (`func_80009CA4`, a float
table at `0x8002E5B0` picked through the costume tables) are applied by the root node.
The in-game scale is below 1: a standing model is ~175 units tall here, but the live joint
positions put the head about 120 units above the mat. Not traced further yet.

## File layout

```
u32 header
    bits  0-15  frame count (anim_frame_count)
    bits 16-25  yaw change; * 4 = 1/4096 turns (0x800 = half turn). Copied to
                AnimPlayer.yaw_delta and added to Wrestler.facing
    bits 26-30  pose class (anim_pose_class), copied to Wrestler.pose every frame
                (0 = standing/normal, 1 = stance, 7..0x1C lying variants, see state_27_recover)
then K keyframes of exactly 98 bytes, K = frames/2 + 1 (80 files have one extra keyframe)
```

The game reads the header words of all animations at match start (`anim_headers_load` into
`g_anim_headers`) and then **streams** the keyframes from ROM through PI DMA, 128 bytes at a
time, into one 0x6880-byte ring buffer per wrestler (`anim_stream_*`). Animation files are
always stored uncompressed, which is why they can be streamed.

### Keyframe

19 joint records for joints 1–19 in joint-number order. Each starts with a big-endian u32:

| Bits | Meaning |
|------|---------|
| 0–9 | x angle, × 360/1024 degrees |
| 10–19 | y angle |
| 20–29 | z angle |
| 30 | position is a delta (only when bit 31 is set) |
| 31 | a position follows |

Position, when bit 31 is set (`anim_decode_joint`):

- **bit 30 clear:** a u32, sign-magnitude: x = bits 0–10 (bit 10 = sign), y = bits 11–21
  (bit 21 = sign), z = bits 22–31 (bit 31 = sign). Units are model units. This also becomes
  the reference for deltas.
- **bit 30 set:** 3 bytes, each sign-magnitude (bit 7 = sign) × 0.5, added to the **last
  absolute position** (not chained from the previous delta).

In every keyframe of every file, joint 1 (pelvis) has an absolute position and joints 2, 3,
7, 12, 14 and 19 have deltas, which is exactly the parentless joints. Every other joint is a
bare 4-byte rotation, so a keyframe is always 4 + 4 + 6×(4+3) + 12×4 = 98 bytes.

### Frames between keyframes

Keyframes are 2 frames apart. Frame `f` uses keyframe `f/2`; an odd frame (other than the
last) decodes keyframe `f/2 + 1` and moves every joint halfway toward it
(`model_decode_frame_interp`), angles by the shortest arc. A Euler fix-up flips x and z by
180° when both exceed ±90° after the difference (`func_80009158`, not decoded in detail).

When a new animation is started with `wrestler_set_anim` flag bit 1 (value 2), its first 5
frames blend from the previous pose with weights 0.75, 0.6, 0.45, 0.3, 0.15
(`model_blend_prev`, table `0x8002D410`). Flag bit 0 keeps the pelvis in place across the
switch; see `individual_games/wcw_world_tour/animation_states.md`. A host port needs this
for frame-exact poses.

Whether one animation frame equals one 30 Hz match tick has not been checked against a live
run; the previewer assumes it does.

## Per-animation metadata: file 618

32 bytes per animation (`g_anim_meta`, indexed by `id − 2901`). Bytes 0–15 feed
`anim_player_set` (location class, reversal window, etc., see `AnimPlayer` in
`symbols/wcw/types.h`). Bytes 16–31 are the animation's **frame events**, below.

### Frame events (bytes 16–31)

Eight `(frame, event)` byte pairs; event 0 = unused. `model_animate` calls
`anim_frame_events(slot, frame + 1)` each time it decodes a new frame, so a pair fires on
the frame whose 1-based number equals its first byte (once; it does not fire again on a
held frame). 1973 of the 2598 animations have at least one event, at most 8. Codes seen
(4442 events in all):

| Code | Count | What happens (code) |
|------|-------|---------------------|
| 1–44 | 1,832 | **sound effect** on the wrestler's channel: `sound_play_event(slot, 256 + code)` looks up sound id `0x8002D86E[code]` (s16), two play parameters (`0x8002D907`, `0x8002D953`, 100 = normal), and a grunt category 1–6 (`0x8002D9B7`) that also plays the wrestler's own voice through `wrestler_grunt` |
| 45–63 | 1,729 | **crowd reaction**: same table, but played on the shared channel by `crowd_sound_play`, with a cooldown, and only when the crowd is enabled (`0x80032B60`) |
| 190, 191 (196 once) | 634 | **the wrestler's own call**: sound id from the per-wrestler pair at `0x8002E670` (e.g. Hollywood Hogan 151/161; −1 = none). Mostly in tests of strength and taunts |
| 200–219 | 17 | **rope deflection** on the ring side the wrestler is nearest (from its pelvis x/z): `ring_rope_push(kind, side, position along the side)`. The kind (`0x8002D3F0[code − 200]`, 1–11) picks which of the 3 ropes bend, by ±10 units, over 6 or 12 frames. Used by the rope rebound (2995/2997), whips into the ropes and corner (2986, 2988, 3002) |
| 221–229 | 56 | **hand grabs a rope**: k = code − 220, limb `(k − 1) / 3 + 1` (1 = −x hand, 2 = +x hand, 3 = both), rope `2 − (k − 1) % 3` (0 bottom … 2 top: the turnbuckle climb 3012 puts a foot on rope 1 at frame 7, then the other foot on rope 2 at frame 25), side from that hand's position; recorded by `rope_attach` |
| 231–239 | 70 | **foot on a rope**: k = code − 230, limb `(k − 1) / 3 + 5` (5 = −x foot, 6 = +x foot, 7 = both), same rope numbering. Turnbuckle climbs (3012, 4690…) |
| 220, 230 | 55 | release all hands / all feet from the ropes |
| 240–243 | 49 | release one limb (`0x8002D3F0[code − 240]`: 0–3) |

Codes used: 2–55, 190, 191, 196 (once, 5385) and 203–243. Code 30 (used 26 times) maps to sound −1, so it is silent in this game. Floor substitutions in `sound_play_event`: when the
model root is below the mat (y < 90, outside the ring), code 6 is silent and codes 24–29
(the mat landings) become 8 or 9 (hard floor). So 24–29 are ring-mat impacts and 6 is a
ring-only sound (probably the ring shaking).

The limb joints for the rope attachments come from `0x8002D408`: limb 1 → joint 18, 2 → 10
(fingers), 5 → node 22, 6 → node 21 (the extra foot nodes). The rope tables
(`0x80058600`, 4 sides × 3 ropes × 40 bytes) drive the drawn rope shape; that part of the
renderer is not traced yet.

What each sound id sounds like is not known yet; that needs the audio bank decoded.

## Where animations are used

Two sources name most animations:

- **Move sets.** `wrestler_move_anim`: anim = move-set file 2 entry
  `[g_move_anim_first[slot] + part] + 0xD82`. Part 0 is the attacker, part 1 the receiver,
  some slots have more parts.
- **State code** that calls `wrestler_set_anim` with a constant (stance, run, whips, get-up,
  apron, …).

Together they label 2210 of the 2598 animations. The previewer shows these labels; the
remaining ~390 are started with computed ids or not used by the 43 roster wrestlers.

## Previewer

```bash
py tools/rom/wcw_models.py "_roms/WCW vs. nWo - World Tour (USA).z64" viewer _tools/viewer/wcw_viewer.html
```

It writes one self-contained page (about 10 MB, three.js from jsDelivr) holding every roster
wrestler, costume and animation, so it stays in git-ignored `_tools/`. Open it in a
browser. It skins on the CPU exactly as above, plays at 30 frames per second, and lets you
filter (`3000-3100`, `pose:7`, `untagged`, or text), step frames, and tag animations. Tags
are kept in the browser and can be exported as JSON. A link such as
`wcw_viewer.html#w=9&a=4804&f=20` opens wrestler 9 (Lex Luger), animation 4804, paused at
frame 20.

## Open questions

- Whether the game mirrors x when drawing. `model_animate` negates x when it stores the
  world joint positions and the root position, so the drawn model may be the mirror image of
  the raw data; the previewer has a toggle.
- ~~The per-wrestler scale table and its units.~~ Read 2026-11-01: five floats at `0x8002E5B0`
  (1.0, 0.98, 1.02, 1.04, 1.03), a plain uniform multiplier; `sub_8000B8B0` picks the entry from
  bits 12–14 of the costume descriptor and `sub_80009CA4` applies it to the root node.
- What the sound ids in the frame events sound like (needs the audio bank).
