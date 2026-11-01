# WCW vs. nWo: World Tour — in-match state in RAM

Source: live RAM of the WCWRecompiled v0.2.0 build (N64Recomp static recompile of the US ROM),
read through IDA while a CPU vs CPU single match was paused at a debug break at clock 04:59.
Code references are N64 virtual addresses, read by disassembling the MIPS code that is resident
in RDRAM at that moment (main segment `0x80000400`–`0x800387F0` plus overlay `ovl_b` at
`0x80090000`, which is the overlay loaded during matches; ROM `0xA69570`).

Confidence tags: **confirmed** = backed by the code that reads or writes the field;
**observed** = only the snapshot values; **guess** = interpretation still to be tested.

## Reading RDRAM out of the recomp

- N64Recomp keeps RDRAM as one host allocation; in this session it was a 512 MB debugger
  segment at host `0x1AD3F700000` (changes per run). `tools/ida/n64mem.py` finds it with
  `find_rdram()` by matching the entry-point code at N64 `0x80000400`.
- Memory is stored as 32-bit words in host (little-endian) order, so a big-endian N64 byte at
  address `A` lives at host `RB + ((A & 0x7FFFFFFF) ^ 3)`. Halfwords use `^ 2`. Words and floats
  read directly. `n64mem.rblk()` un-swaps a block so it reads like real N64 memory.
- The bundled `aki_cheats` mod's GameShark codes were the starting leads: `800F0801 0064`
  (P1 spirit) and `800F16EF 0000` (clock seconds).

## Match clock — `0x800F16EC` (confirmed)

Three `s16` fields; the clock **counts up** (elapsed time), it is not a countdown.

| Addr | Type | Meaning | Snapshot |
|------|------|---------|----------|
| `0x800F16EC` | s16 | minutes | 4 |
| `0x800F16EE` | s16 | seconds (0–59) | 59 |
| `0x800F16F0` | s16 | sub-second ticks (0–29) | 19 |

- `func_8009DE70(clock*, 30)` advances it: ticks++ and when ticks reach 30 they reset and
  seconds++; seconds wrap at 60 into minutes++. 30 ticks per second implies the match logic
  ticks at 30 Hz (**guess** until the frame loop is traced).
- `func_8009DEE8(clock*, min, sec)` sets it (called with 0,0 when the match starts, at
  `0x800B0A10`).
- `func_800B28BC` is the per-tick clock update. It reads the time-limit setting at
  `0x8006205C` (s8; 0 = no limit, which was the case here). With no limit the clock resets to
  0:00 when minutes reach 60. With a limit it compares minutes against the table at
  `0x800E74FF + setting` = {5, 10, 30, 60} for settings 1–4 and calls `func_800B12FC(0x4000)`
  (time up) when they match. `func_800970FC(seconds, minutes)` draws the clock.
- Neighbouring globals that the match-flow code (`0x800B09E0`–`0x800B0DBC`) uses as
  counters: `0x800F16E2` (s8, match-flow phase), `0x800F16E3`, `0x800F16F2`/`0x800F16F3`,
  `0x800F16F4`, `0x800F16F6`, `0x800F16F8` (s16 counter, e.g. compared with 31),
  `0x800F16FC` (word). Not yet decoded.

## Wrestler array — `0x800F07F0`, 4 × `0x3A0` bytes (confirmed)

`wrestler[i] = 0x800F07F0 + i * 0x3A0` (the code computes `i*29*32`). Slots 2 and 3 are empty
in a singles match (`+0x02 = 0xFFFF`, spirit at the default 50).

### "Current wrestler" pointer globals

The update code works through globals instead of passing pointers:

| Addr | Meaning |
|------|---------|
| `0x800F1680` / `0x800F1670` | current wrestler (`wrestler[s8 @ 0x800F1698]`) |
| `0x800F1684` / `0x800F1674` | its opponent (`wrestler[s8 @ 0x800F16B0]`) |
| `0x800F1688` / `0x800F1678` | `wrestler[cur->+0x341]` (partner? **guess**) |
| `0x800F168C` / `0x800F167C` | `wrestler[cur->+0x342]` (**guess**) |

Set up by `func_800A7C80` before each wrestler is updated. At the break, current = P2,
opponent = P1.

### Per-wrestler state update

`func_800A7C80` ends with `jalr table[cur->state]`, where the table is at `0x800E723C`
(at least 64 entries). Each entry handles one action state; a handler typically switches on
`+0x12` (step within the state).

### Field map

Offsets are from the start of a wrestler. Snapshot column is P1 / P2.

| Off | Type | Meaning | Tag | Snapshot |
|-----|------|---------|-----|----------|
| `+0x000` | s8 | slot index (0–3) | confirmed | 0 / 1 |
| `+0x002` | s16 | **roster entry** (group << 8 | index + 1; 0x101 = Lex Luger), −1 when the slot is empty | confirmed (snapshot 9) | 0x101 / 0x101 |
| `+0x004` | s16 | frames left in the current move; the hit lands at 1 and the move ends at 0 (state 0x42 handler) | confirmed for attacking moves | 0x23 / 0x23 |
| `+0x006` | s16 | unknown | observed | 4 / 4 |
| `+0x008` | s16 | **current animation id**, set by `set_anim` `func_800A0E48(w, id, flag)`. The previous id is kept in `0x800F0650[slot]` | confirmed | 0x12C4 / 0x12C5 |
| `+0x00A` | s16 | **action state**, index into the handler table at `0x800E723C` | confirmed | 0x35 / 0x36 |
| `+0x00C` | s16 | **health**, clamped to 0..`+0x00E` (`wrestler_add_health`) | confirmed | 0x2B / 0x2B |
| `+0x00E` | s16 | **health_max**: each hit lowers it by damage / 4, never below 128 | confirmed | 0xC2 / 0xC8 |
| `+0x010` | s16 | **spirit meter**, 0–100 | confirmed | 55 / 78 |
| `+0x012` | s16 | step within the current state; handlers switch on it and bump it | confirmed | 1 / 1 |
| `+0x014` | s16 | per-state timer / counter (`state_timer`) | confirmed | 0x36 / 0x0D |
| `+0x016` | s16 | unknown | observed | -2 / 0x81 |
| `+0x018` | u32 | status flags (high word); bit `0x20` and `0x400` are tested and set by state code | confirmed as flags | 0x00000800 / 0x00000800 |
| `+0x01C` | u32 | status flags (low word); `& 0x1800` blocks spirit changes | confirmed as flags | 0x06000007 / 0x22200007 |
| `+0x020` | u16 | unknown | observed | 9 / 3 |
| `+0x032` | s16 | ring zone returned by `func_800A5D70(&pos)` (inside ring, apron, floor…) | guess | 1 / 1 |
| `+0x034` | s16 | facing quadrant (0–3), picks a value from `0x800E7040` = {0x800, -0x400, 0, 0x400} | confirmed as an index | 1 / 1 |
| `+0x03C` | f32 ×3 | **position x, y, z**; y is up, the ring mat is at y = 90 | confirmed (written as a vec3 at `0x800B06E0`) | (135.4, 90, 58.6) / (144.5, 90, -11.9) |
| `+0x048` | s32 | **facing angle**, 0x1000 = one full turn, 0 = facing +Z, positive turns toward +X (`atan2(dx, dz)`); signed, not wrapped | confirmed (snapshot 2: each faces the other within 1–2°) | 0x78C (≈170°) / 0x783 (≈169°) |
| `+0x05C` | 19 × vec4 f32 | **world-space joint positions** (x, y, z, 0), 16 bytes each, through `+0x18B` | observed | P1 standing (head y ≈ 210), P2 low (head y ≈ 169, knees ≈ 101) |
| `+0x1CC` | 19 × vec4 f32 | second joint set with the same layout, almost equal to the first (previous tick? collision copy?) | guess | |
| `+0x338` | u32 | written and read by the state code | observed | |
| `+0x341` / `+0x342` / `+0x344` | s8 | indexes of other wrestlers (partner / targets) used to fill `0x800F1688`/`0x800F168C` | confirmed as indexes | |
| `+0x350` | u16 | read in 10 places, written in 4 | observed | 0 / 0 |
| `+0x352`, `+0x353` | s8 | written by the state code | observed | (1, -1) / (-1, -1) |
| `+0x354` | ptr | pointer into `0x800F1Dxx` (P1 `0x800F1D60`, P2 `0x800F1D80`), probably a per-wrestler model or controller block | observed | |
| `+0x370` | s8 | move set of this wrestler's own current or last move (a wrestler slot). The receiver keeps its own value; its animation is looked up through the attacker | confirmed, see `moves.md` | 0 / 1 |
| `+0x372` | s16 | move slot (0–221) in that move set | confirmed | 208 / 14 (snapshot 1), 1 / 3 (snapshot 2) |
| `+0x378` | 36 bytes | unpacked parameter record of the current move | confirmed | |
| `+0x358` | struct | animation player, passed to `func_800097EC(player, anim_id)` by `set_anim`. `+0x35C`, `+0x360`, `+0x361` are part of it | confirmed | |

### Snapshot 2 (clock 05:24 tick 0, HUD shows 05:23)

Andrew paused during a standing move after a grapple, P1 attacking.

| | P1 | P2 |
|---|---|---|
| `+0x04` / `+0x06` | 0x19 / 0x23 | 0x28 / 0x23 |
| anim `+0x08` | 0x0DA8 | 0x0DA9 |
| state `+0x0A` | 0x42 | 0x43 |
| spirit `+0x10` | 82 | 77 |
| step `+0x12` | 0 | 0 |
| pos | (144.6, 90, 80.4) | (123.7, 90, 152.6) |
| facing `+0x48` | -0xA9 (≈ -15°) | 0x757 (≈ 165°) |

- Both snapshots give the attacker an even animation id and even state, and the receiver
  `id + 1` for both. The animation pair is now explained: both come from the attacker's move
  set, part 0 and part 1 of the same move (see `moves.md`). Snapshot 1 is move 208 and
  snapshot 2 is move 1, both from move set 0.
- `+0x04` is the number of frames left in the current move. The state 0x42 handler applies the
  hit when it reaches 1 and ends the move at 0.
- The two facings are 180° apart and each points at the other wrestler, which pins down the
  angle convention above.
- `+0x06` in snapshot 2 (0x23) equals `+0x04` in snapshot 1, so `+0x06` may be the previous
  value of `+0x04` (**guess**).
- The HUD clock lags RAM by one second when ticks = 0: the clock was drawn before the tick
  that rolled seconds over.

### Snapshot 3 (clock 05:33 tick 25): one wrestler getting up

| | P1 | P2 |
|---|---|---|
| state | 0x3B `state_3B_stance`, step 4 | 0x3C `state_3C_get_up`, step 1 |
| anim / anim_frame | 3048 / 10 | 3045 / 28 |
| pose (+0x038) | 1 | 7 |
| spirit | 92 | 85 |
| head joint y | 241 (upright) | 193 (crouched) |
| last move | slot 154 (ground attack, face-up B) | slot 3 |

The screenshot shows one wrestler crouched halfway up and the other standing in a fighting
stance behind him. That matches the head heights and pins down state 0x3C (get up, lying
pose class 7) and state 0x3B (back into stance, pose class 1). Pose classes are dispatched by
`state_27_recover`.

### Snapshot 4 (clock 05:41 tick 7): one wrestler lying flat

P2 lies on the mat (head joint y 105, the mat is at 90) but is still in **state 0x43
`state_43_receive_move`**, at frame 75 of receive animation 3497, with `move_frames_left` 0.
The animation's pose class (`anim.pose_class`) is 0x1B, a lying class. So lying flat after a
move is the tail of the receive animation, not a state of its own. When it ends,
`state_27_recover` sends the wrestler to 0x3C get-up or 0x3B stance. Meanwhile P1 is back in
`state_01_stand` (animation 2919).

States 0x29 and 0x2A (pose classes 3 and 2) were identified later from the code: 0x29 is dazed on the feet (3087, or 3089 on the apron) and 0x2A a short stagger (3093, 3094); see `movement.md`.

### Snapshot 5 (clock 05:59 tick 7): pinfall, first count called

| | P1 (pinned) | P2 (pinning) |
|---|---|---|
| state | 0x32 `state_32_pin_defender`, step 1 | 0x31 `state_31_pin_attacker`, step 1 |
| anim / anim_frame / frames left | 3114 / 17 / 13 | 3113 / 17 / 13 |
| spirit | 100 | 91 |
| flags_lo | 0x02000007 | 0x26200007 (GRAPPLE_LEAD set) |
| linked_idx | -1, -1 | 0 (P1), -1 |
| recover_count (+0x016) | -13 | -1 |

- The pinner is the grapple leader (flags_lo 0x4000000), with the pinned wrestler in
  `linked_idx[0]`. That confirms `wrestlers_update_all` runs the pair together, leader first.
- The referee's count was not found in either wrestler. A RAM snapshot of this moment is saved
  (scratchpad `snap_pin1.pkl`, covering main bss and the ovl_b bss). A second pause at
  count 2 can be diffed against it to find the counter.

### Snapshot 6 (clock 06:56 tick 17): running attack at a wrestler on the apron

| | P1 (running in the ring) | P2 (on the outer apron) |
|---|---|---|
| state | 0x07 `state_07_running_attack`, step 1 | 0x56 `state_56_apron_recover`, step 1 |
| move slot | 125 (FAQ: Run+B) | 120 |
| anim / frame / left | 4528 / 5 / 22 | 3042 / 64 / 14 |
| animation location / pose class | 0 / 0 | 3 / 3 |
| ring_zone | 3 | 3 |
| pos | (-176, 90, 154) | (-174, 90, 115) |

- Confirms state 0x07 as the running attack and FAQ slot 125 as Run+B.
- The apron animation has location class 3 (y stays 90) and pose class 3. Pose class 3 sends
  `state_27_recover` to 0x29. (Later corrected: 0x29 is the dazed state, which has an apron version; see `movement.md`.)
- Both wrestlers have ring_zone 3 near the same side of the ring, so zone 3 is probably the edge
  of the ring (in the ring next to the ropes, or on the apron).

### Snapshot 7 (clock 07:03 tick 16): one wrestler down outside, one on the apron

Read with `n64mem.rblk` and compared field by field with snapshot 5 (the pinfall, saved as
`snap_pin1.pkl`). Lex Luger vs Lex Luger again.

| | P1 (lying on the floor outside) | P2 (on the apron) |
|---|---|---|
| state / step | 0x28 down, step 3 | 0x55 `state_55_ropes_b`, step 5 |
| anim / frame / left | 3228 / 27 / 3 | 3336 / 2 / 36 |
| pose class | 17 (lying) | 0 |
| pos | (−395, **0**, 60): the floor is y = 0 | (−265, **90**, 114): apron at mat height |
| health / health_max | 58 / 189 | 0 / 177 |
| spirit | 46 | 96 |
| region_condition | 31, 4, 50, 48, 49 | 23, 0, 50, 50, 50 |
| grapple_ctx | 0x20 | 0x20 |
| down_count (+0x036) | 1 | 0 |
| last move slot | 105 (move set 0) | 185 (move set 1) |

What the two snapshots settle:

- **`+0x348` / `+0x34C` are copies of another wrestler's `flags_hi` / `flags_lo`**, taken by
  `wrestler_update` before the UPDATED bit is set. Snapshot 9 shows whose: the tag partner's
  (`+0x344`, 0 in singles, so in singles both point at P1).
- **`+0x036` is a knockdown counter** (`down_count`): 1 for the wrestler lying outside, 0 for
  the standing one; the code adds 1 in state 0x28 and clears it on standing up, and recovery
  loses 60 per count.
- **`+0x03A` holds the last received move's `end_value`**, −1 while pinned (P1 in snapshot
  5 = −1, the pinner 1089).
- `+0x04C`, `+0x18C–1CB` and `+0x2FC–32B` are zero in every snapshot (unused here).
- Health 0 does not end anything: P2 has been at 0 since the pinfall and is still fighting.
- `GrappleCtx` 0x20 is set on both wrestlers while one stands on the apron and the other
  is outside, which supports reading 0x20 (now DEF_ON_APRON) as an apron context.
- `run_flags` (0x6 / 0xC here, 0x9 / 0xC at the pin), `+0x028–02B` and `state51_flags` change
  between snapshots but two samples are not enough to decode them.

### Snapshot 8 (clock 07:06 tick 16, 3 s after snapshot 7): count-out running

Diffing the overlay bss against snapshot 7 (exactly 90 ticks earlier) and reading
`countout_tick` (`0x800B2680`) gives the whole count-out:

| | Snapshot 7 | Snapshot 8 |
|---|---|---|
| `g_countout_left` (`0x800F16C4`) | 20 | 19 (screen shows 20 − 19 = 1) |
| `g_countout_clock` (`0x800F16E4`, min/sec/tick) | 0:00 / 27 | 0:01 / 27 |

- **It is global**: one clock and one counter for the whole match. It runs while *any*
  active wrestler is on the floor (`flags_lo & LOC_FLOOR`, ignoring wrestlers with `& 0x100`),
  and resets only when *nobody* is outside. A second wrestler leaving does not restart it.
- Length comes from the match option `g_countout_setting` (`0x8006205D`, 2 here) through
  `g_countout_lengths` = {off, 10, 20}. This match uses the 20 count.
- The count speeds down near the end: a number every 2 s while 6+ are left, every 3 s for
  5–3, every 4 s for the last two. At 0 the match ends with result `0x10000`.
- It is paused while another count runs (`0x800F16F3`, the wrestler `sub_800B2238` is
  counting, is −1 here).
- `g_update_first` (`0x800F16E3`) went 1 → 3 over the 90 ticks, as expected for a value
  that cycles 0–3 every frame.

### Snapshot 9 (clock 01:14 tick 9): tag team, everyone outside

Chris Benoit (P1, human) and Rey Misterio Jr. (CPU) against Scott Hall (P2, human) and Kevin
Nash (CPU). The screenshot shows Benoit standing on the floor holding a bat, Rey covering
Hall on the floor, and Nash on the apron with SPECIAL lit. Ring-out is off for this match.

| Slot | Wrestler | +0x002 | state | flags_hi | flags_lo | teammate (+0x344) |
|------|----------|--------|-------|----------|----------|-------------------|
| 0 | Benoit (P1) | 0x10B | 0x01 stand | 0x6400 | 0x12000012 | 1 |
| 1 | Rey (CPU) | 0x10A | 0x37 cover | 0x0004 | 0x06008011 | 0 |
| 2 | Hall (P2) | 0x007 | 0x38 covered | 0x0024 | 0x62808012 | 3 |
| 3 | Nash (CPU) | 0x006 | 0x58 | 0x0000 | 0x22003005 | 2 |

What it settles:

- **`+0x002` is the roster entry**, not an "in use" marker: 0x10B and 0x10A are group 1
  indexes 10 and 9 (Benoit, Rey), 0x007 and 0x006 group 0 indexes 6 and 5 (Hall, Nash). The
  Luger matches showed 0x101 for both slots because it was Luger vs Luger.
- **`flags_lo & 0x1` = CPU**: clear on the two human players, set on both CPU partners.
- **`flags_lo & 0x2` = LEGAL**: set on Benoit and Hall, the two men legally in the match, clear
  on both partners. Both wrestlers have it in singles.
- **`flags_lo & 0x20000000` = TEAM_B**: set on Hall and Nash only. In the Luger matches only
  P2 had it.
- **`flags_lo & 0x1000` = SPECIAL** on Nash (spirit 100), matching the SPECIAL! on screen.
- **`flags_hi & 0x2000` = holding a weapon**, with `0x4000` as well for this one (a bat):
  Benoit's idle animation is 3253, the weapon idle that `wrestler_base_anim` picks for
  `0x2000 | 0x4000`.
- **`+0x344` is the tag partner** (0 ↔ 1, 2 ↔ 3), and `+0x348/+0x34C` hold a copy of the
  partner's flags.
- Rey and Hall (the cover pair) both have `flags_lo & 0x8000`, and Hall also has `0x800000`
  and `0x40000000`. Later found: `0x8000` = `FACE_DOWN` (set from the lying pose class), `0x800000` = `BACK_HIT`
  (facing away when run into, see `movement.md`) and `0x40000000` = **`BLEEDING`**: so Hall was
  bleeding at that pause. Bleeding slows health recovery (every 90 ticks by header
  byte 2 instead of every 60 by byte 1), gives extra spirit on hits (header byte 3), and
  paints blood into the head, neck and chest textures (`blood_apply`).

Match option words at `0x80062058`: 0x02C0 here vs 0x012F in the Luger singles match, so
this word holds the match type. `0x8006205A` is 0x0033 in both, the ring-out byte
`0x8006205D` is 0 (off) here vs 2. See `match_options.md`.

### What snapshot 1 shows

P1 and P2 have consecutive animation ids (0x12C4 / 0x12C5) and consecutive states
(0x35 / 0x36), both at step 1, facing nearly the same direction, P1 about 70 units from P2.
That fits a grapple move in progress with P1 as the attacker and P2 receiving it.
Andrew's screenshot of the same break agrees: one wrestler is seated on the mat with legs out
(P2's joints: head y ≈ 169, knees near the mat) while the other holds him from behind, both
facing the same way. The HUD spirit bars (left about half full, right about three-quarters)
match 55 / 78, and the clock reads 04:59.
It is still a **guess** that states 0x35/0x36 and animations 0x12C4/0x12C5 are a paired
attacker/receiver set; one more snapshot during a different move would settle it.

## Open questions / next steps

All answered in later docs (roster id: `roster_entry` +0x02; per-region condition +0x33C;
moves: `moves.md`; 30 Hz and update order: `frame_loop.md`; states: `symbols/wcw/states.toml`).
Open questions now live in `../../QUESTIONS.md`.
