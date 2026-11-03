# No Mercy: per-animation data and frame events

Read from the code and the ROM files on 2026-11-02 (USA Rev 1). This is the data that times
the moves: a strike's active frames, where an animation leaves the wrestler, its sounds and its rope
contacts. World Tour's equivalent is file 618 (`../../file_formats/animations.md`).

## Where it comes from

- **File 703** holds the per-animation metadata: **36 bytes for each of the 5,787 animations**, file ids
  11737 to 17523. `anim_system_init` loads it into the heap (pointer at 0x8006A224), and
  `anim_record(id)` returns the record for id − 11737.
- The animation files' own header words (frame count, turn, pose class) are read separately into a
  table (pointer at 0x8006A220), 4 bytes each, as in World Tour.
- `anim_player_set` (0x80011BD8) copies a record into the wrestler's animation player (`Wrestler`
  +0x2E8) every time `wrestler_set_anim` starts an animation (ids below 11737 are skipped).

Related move files loaded by `match_load` (0x80114030 on): **494** the generic moves (0x800A6040),
**495** the shared move records, **496** the move-animation code table (0x8015A700, used by
`anim_style`), **497** code → move record, **498-503** the six follow-up variant sets (534 bytes each, at
0x80090BA0 + 720 × set; below).

## Move sets: from the roster to an animation

Read from the code and checked against RAM on 2026-11-02 (all 157 listed moves of The Rock, Austin,
Undertaker and Triple H match, codes, move numbers and records).

- **The roster's attire descriptor** (`ATTIRE_PTRS` 0x80047124, 20 bytes): **+12 the attribute file**,
  **+10 the move-set file** (The Rock 505 / 506, Austin 507 / 508, Undertaker 509 / 510, Triple H 521 /
  522 ...; the same for all four attires; 65 wrestlers).
- **The move-set file** (184 bytes, packed, `moveset_unpack` 0x8013CBAC): a list of **157 entries** at
  *0x800442DC (or *0x800442E0 when attrs +0x34 bit 1 is set), 10 bytes each: the move slot and four
  category ids. For each entry the stream holds **w + 5 bits** (most significant first), w being the
  widest of the four categories' index widths (0x800442E4): the low w bits index the category's list of
  animation codes (*0x80045500[category]), the next 2 bits choose one of the four categories, and bit
  w + 4 marks the move **preferred** (move flags 0x8000, the CPU's favourite). 157 entries fill exactly
  1,472 bits. After the loop the rest of the 548 slots are filled: **fixed defaults** from 0x8014DCE0
  ((slot, category, index) entries), **derived slots** from 0x8014DD34 ((source, n, step): n > 0 sets slot
  n to the source's code + step; n ≤ 0 fills the −n slots after the source with code + step, + 2 × step
  ...; this is how a move's variants follow it), and **the pins**: every move in slots 0-291 whose flags
  have 0x1E00 gets the next pin slot from 292 on (code + 1), its index stored in the byte table
  0x800A5720 (`state_44_pin_attacker` reads it). All 548 slots and the pin table match RAM for the four
  loaded wrestlers.
- The code goes to `g_move_anims[slot][move]`; **file 497** maps it to `g_move_anims2` (the move's index
  in the shared move table) and **file 495** holds that table: **2,136 move records of 36 bytes**
  (`struct MoveRecord`), copied into `g_movesets`.
- The animation: **file 496** = `anim_style`'s table (0x8015A700): animation file = file496[code] +
  13090 + part.

So everything a match needs about a wrestler's moves comes from the ROM: `nomercy_models.py moves` prints
every wrestler's 548 slots as code, preferred mark (*) and animation file (`move_codes` in the tool), and
the viewer labels each animation with the move slots and wrestlers that use it ("move 70 attacker: ROCK";
2,327 animations).

## The record (36 bytes) and the animation player

| Record byte | Player byte | Meaning (users) |
|-------------|-------------|-----------------|
| header | +0x00 | the turn over the animation (from the header table), added to facing at the end (`anim_end_apply`) |
| header | +0x04 | frame count |
| 1 bit 7 | +0x05 | set in 704 animations; no match code reads player +5 |
| 0 low 4 bits | +0x06 | **where it ends**: `anim_end_location` (1 ring, 2 floor, 3 apron, 6 cage wall, 7 on top of a raised place), `anim_end_height`, 0x55 / 0x56 |
| header | +0x07 | **end pose** (pose class), read by 0x30 `state_30_settle`, `wrestler_settle_pose`, the lying checks |
| 3 | +0x08 | **active window start** (frame): strikes, grapple attempts, the run, aerials; 0 is read as 1, 255 as the last frame (`wrestler_set_anim`) |
| 4 | +0x09 | **active window end**: the hit test only runs while anim_frame is between +8 and +9 (0 → 1, 255 → last frame) |
| 18, 19 | +0x0A, +0x0B | not read in the match code |
| 5 | +0x0C | **object spawn kind** for moves with a prop (`object_spawn_for_move`; 0 none), also read by 0x59 / 0x5A |
| 6 | +0x0D | **the frame the prop appears** (spawned when anim_frame equals it) |
| 7 | +0x0E | **a lead-in frame** (126 animations use it): the run's contact checks with the opponent start only from this frame, and the turnbuckle perch counts as perched (act_flags 0x04, location 0x800) until it |
| 9 | +0x10 | **the reversal frame**: at this frame, if the defender's reversal roll passed (flags_f0 0x02), 0x55 switches to the reversal (also the whip, 0x59, 0x5D; 0 → 1) |
| 10 | +0x11 | **contact window end**: `state_move_step` keeps flags8a 0x2000 (in a move) up to this frame (the ground hold, 0xCE too); 255 in most records |
| 11, 12 | +0x12, +0x13 | **the flight**: at frame byte 11 `aerial_step` aims the dive (`dive_aim`), and from byte 11 to byte 12 the wrestler is airborne (flags8a 0x40); 255 in byte 12 → last frame |
| 17 | +0x14 | only normalised by `wrestler_set_anim` (0 → 1), not read elsewhere |
| 16 | +0x15 | **the crossing frame of an apron move**: in 0x55 / 0x56 with flags78 0x600, until this frame the pair counts as still in the ring and `pair_follow` pulls the defender at 4.0, afterwards at 6.0 (`finisher_effects` reads it too) |
| 2 | +0x16 | always 0 |
| 0 high 4 bits | +0x17 | the place class while it plays; from frame **byte 13** on it is **byte 14** instead (`anim_slot_event`) |
| 13, 14, 15 | +0x18-+0x1A | that switch frame, the later place class, and byte 15 only normalised (0 → last frame) |
| 20-35 | (record) | **eight frame events** (below) |

Counts across the 5,787 records: 685 have an active window (byte 3 non-zero), 5,079 leave byte 1 at 0,
4,035 have byte 8 = 255. Read user by user on 2026-11-02: every byte is placed; byte 1's top bit and bytes
15, 17, 18 and 19 are never read by the match code.

## Placing a paired move (`pair_scripted_place`, 0x80134420)

Read 2026-11-02. When a paired state starts, `pair_scripted_place(self, other, ...)` (32 callers) picks a
target by the state (a jump table over states 0x0A-0xD0) and slides there:

- it first moves the root to the pelvis (`wrestler_move_pos_to_pelvis`), then `slide_vector` gives a
  per-frame step to the target over **N frames** (count_timer +0xB2) and the facing turns evenly to the
  target facing (+0x298 per frame); `pair_slide_step` applies both each frame;
- the target is a point at a distance and angle from an anchor (`point_at_offset`): a ring corner
  (0x8014D1A8 by the corner index +0xCC), the other wrestler or a zone (`place_from_zone`,
  `place_from_zone_pair`), or a joint position;
- per state (every case read 2026-11-02; distances are model units, N frames in brackets; "behind" is
  the other wrestler's facing + 2048, half a turn):

| States | Target | N |
|--------|--------|---|
| 0x0A (turnbuckle perch), 0x1C, 0x74 | 73.5 out from the corner (+0xCC), along the corner's angle; 0x1C picks the angle by the move's flags 0x18 | 8 / 8 / 7 |
| 0x88 (catching a flyer) | 91.5 out from the corner | 10 |
| 0x89 (caught in the air) | a fixed spot ±229 / ±230 in x and z by the corner index (which pair by the attacker's flags78 0x80) | 6 |
| 0x0B (whip, attacker) | with flags 0x9C 0x04: 56.5 out from the corner; otherwise a zone spot (kind 15, 40 or 50) | 15 |
| 0x0D / 0x0F, 0xD0 | a zone spot, kind 15, radius 40 / 50 | — |
| 0x11 / 0x13 | its own pelvis, clamped to ±125 in x / z by the flags passed | the whole animation |
| 0x17 (onto the ropes) | a zone spot, kind 7, 30 | — |
| 0x3A / 0x3C / 0x40 | 40 from the other wrestler, facing it (0x3C turns to the other's facing + half a turn when it is face down) | 12 |
| 0x5C (runner countered), 0x66 | 80 / 100 from the other's pelvis, behind it | 6 |
| 0x65 (strike countered) | 100 from the other's position, behind it | 6 |
| 0x69 | stays at its pelvis, turns to face the other | 6 |
| 0x6A | 100 from the other, on the line to itself, turned half round | 6 |
| 0x6E (dropping down) | a raised place: 30; with location 0x400: `place_from_zone_pair`; else a zone spot, kind 6, 55 | — |
| 0x6F (climbing onto) | with flags84 0x0C: a fixed spot 370 in x or z (facing −1536 / 512); else a zone spot, kind 6, 55 (65 with flags84 0x33) | 6 |
| 0x70 (over the ropes) | a zone spot, kind 6, 55, facing by hit_flags bit 0x2000000 | — |
| 0x72 / 0x73 | a zone spot, kind 7, 47 / 50 | — |
| 0x7F / 0x80 (rear lift / its receiver) | with flags84 0x10 / 0x08 a fixed spot 266.5 or 193.5 out (by zone); else 53.5 / 73 from the other | 6 |
| 0x8A, 0x8E, 0x91 | a zone spot, kind 6, 55 | — |
| 0x94 | 80 from the other's pelvis, by its facing or zone | 12 |
| 0x9A / 0x9B | a zone spot, kind 3, 80 / 55 | — |
| 0xA1 (running grapple, defender) | from the other's position, by the move: 100 straight, or 33 / 70 with a sideways 33 or 52 (move 104 and the move's byte +0x17); facing by flags78 0x80 | 6 |
| 0xC8 (cage climb start) | a zone spot, kind 8, 60, turned half round | — |
| 0xCD (turnbuckle grapple) | its own scripted path (point_at_offset, slide) | 12 |

  The zone spots come from `place_from_zone(w, kind, facing, radius)`, which takes the spot from the
  wrestler's ring-zone data. States not listed get no placement.

During the move both wrestlers just play their animations (the receiver's is the attacker's + 1); the
pair is held together by `pair_follow` / `state_bounds`, so **where the bodies are each frame comes from
the animations themselves**, starting from that placement.

## Frame events (record bytes 20-35)

Eight (frame, code) pairs, code 0 unused, fired once by `anim_frame_events(slot, frame)` when the
animation reaches that frame (16,665 events in all). Same scheme as World Tour, with more codes:

| Code | Count | What it does |
|------|-------|--------------|
| 1-169 | 9,556 | **a sound** on the wrestler: `sfx_play_slot(slot, 256 + code)` (when sounds are on, 0x8009EDD4). Common: 6 (1,572), 71 (1,335), 73 (1,171), 24 / 25 / 27 (mat impacts in World Tour), 70, 14, 69 |
| 170-174 | 70 | 171 / 172: a call with ±0.25 (`sub_80013FFC`, not read); 173 / 174: copy a 2,056-byte buffer per slot (0x80076CF8 / 0x80076CFC) into the model's texture area, a texture swap (probably the face) |
| **176-195** | 6,322 | **hand shape**: sets the slot's hand pose 0-19 (0x80076588); the model then swaps the meshes of parts 17 / 18 and 9 / 10 (the two hands) from tables at 0x8004847C-0x800484B8. 176 (pose 0, the default hand) is the most common event of all (5,873) |
| 200-219 | 206 | **rope push**: as World Tour (`ring_rope_push` on the nearest side; kind from 0x80047F20) |
| 220-243 | 511 | **ropes**: 221-229 a hand grabs a rope, 231-239 a foot, 220 / 230 release all hands / feet, 240-243 release one limb (`rope_release`), as World Tour |

## Posing (`model_animate`, 0x80012130; decompiled 2026-11-02, `src/nomercy/nm_model.cpp`)

The animation files have World Tour's format (`../../file_formats/animations.md`: a 4-byte header,
then 98-byte keyframes two frames apart; the same 19-joint skeleton, bind pose at 0x80047ED0, joint
order 0x80047EF8, parents 0x80047F0C). What the main segment does with them each frame, per slot:

- **The stream** (`struct AnimStream`, 0x80069F20 + 192 × slot): keyframes are DMA'd from the ROM one
  at a time (98 bytes) into two 128-byte heap buffers; `buf[cur_buf]` is the keyframe being posed, the
  other the next one in flight (`anim_stream_open`, `anim_stream_fetch`, `anim_stream_advance`). An
  animation change, or a restart at frame 0, reopens the stream, releases the ropes and the hand shape.
- **The frame:** even frames and the last one decode `buf[cur_buf]` (`model_decode_frame`); odd ones
  move the stream on and go halfway to the next keyframe (`model_decode_frame_interp`: positions of
  the parentless joints, every rotation the short way round, with a 180-degree flip of x and z when
  either differs by more than 120). At frame count - 2 the stream is moved on for the last frame.
- **The blend:** frames 0-4 of a new animation (not a restart, not the slot's first) lean toward the
  previous pose by 0.75 / 0.6 / 0.45 / 0.3 / 0.15 when the posing mode has bit 1 (`model_blend_prev`);
  the old pose is first re-centred on its pelvis unless mode bit 2.
- **The head look** (`head_look_update`, ovl_e) is written straight into joint 5's rotation.
- **The skin** (`model_build_skin`): as World Tour, plus a per-build part fit (scale 0x80047F54 and
  offsets 0x80047FB8 / 0x80047FCC / 0x80047FE0 by the slot's build byte 0x8006A268) for the joints in
  0x80047F40; the result is the render node's local matrix. The render tree (`struct ModelNode`,
  0x80091CC0, 28 nodes a slot) gets its world matrices from `node_tree_update`.
- **The root anchoring** (mode bit 0): on a switch the body is moved so that joint 1's world point
  stays where the old animation left it (x and z; y too with mode bit 3), by posing the root under the
  old translation first. This is why a position changes inside the posing.
- **The body:** the block's translation is (-pos.x, pos.y + a carry offset, pos.z), its yaw
  -(facing + 2048) × 360 / 4096 degrees; the body matrix is scale × (tilt(x, 0, z) × yaw) with the
  tilts only on a ladder (Wrestler +0x108 / +0x110). Each joint's world points (+0x54, +0x6C of its
  record) come out x-mirrored; `wrestler_joints_update` copies them into the wrestler record.
- **The events** fire for frame + 1 (`anim_frame_events`): 1-169 sounds, 170-175 specials
  (`anim_event_special`: a rumble / shake value or a texture swap), 176-195 hand shapes, 200-219 rope
  pushes (`ring_rope_push`, the side from joint 1's position and the body's facing), 220-243 limbs
  grabbing (221-229 hands, 231-239 feet: limb (k-1)/3 + 1 or (k-11)/3 + 5, rope 2 - (k-1) % 3) or
  releasing (220 / 230 all hands / feet, 240-243 one limb) the ropes.

## The follow-up variant sets (files 498-503)

`move_anim_offset` (0x800EC7D8) picks follow-up, chain and reversal moves: base move + **1-4**, from a
2-bit entry for the current move's animation code in set attrs +0x01 (four codes a byte, high bits
first). Each set is 534 bytes (2,136 codes); almost every entry is 0 (variant 1), with 63 to 96
non-zero entries per set.

## What this gives a port

- The **strike and grapple-attempt timing** is data: frames +8..+9 of the animation playing.
- **Where an animation leaves the wrestler** (place, pose, turn) is data, applied at its end.
- Sounds, hand poses and rope contacts are frame events a host can play back directly.

## In the viewer

`nomercy_models.py viewer` now reads file 703 (`anim_record`): the events bar shows the active window
(lit while the frame is inside it), the hold frame, where it ends and the frame events with No Mercy's
codes (sound, hands, special, ropes). The list filter matches labels such as `window`, `holds` and
`ends: apron` (686 animations have a window, 411 a hold frame). For example the running shoulder block
15256 is active on frames 10-14.

## Open

- ~~`place_from_zone`'s zone-spot kinds (3, 6, 7, 8, 15)~~: not kinds; `place_from_zone(w, frames, angle, distance)`
  slides the wrestler over `frames` frames (3-15 by state) to the point `distance` in front of its collision
  segment (`zone_info`: the plane distance along the segment's normal) and turns it to the segment's direction
  plus `angle` (0 or 2048, half a turn); count_timer = frames, angle_rel = the turn / frames (code 2026-11-02).
- 0x55 / 0x56's damage frames: `move_event` applies grapple damage at the end of the animation; check
  whether any event or byte moves it earlier for some moves.
- Codes 170-174 in full, and what the sound ids sound like (needs the audio bank).
- The alternate entry list (attrs +0x34 bit 1): decoded the same way, not yet checked in RAM. And names
  for the 548 move slots (the GameFAQs move lists could name them per wrestler).
- Add the record to the library's converter output (the viewer already reads it).
