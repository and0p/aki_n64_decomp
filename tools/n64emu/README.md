# akiemu: World Tour in an emulator, with the debug API

`akiemu.py` runs the original World Tour ROM in the libretro **mupen64plus_next** core through a
small Python (ctypes) frontend, and serves the same HTTP API as the recomp's `aki_debug` mod
(`tools/recomp_dbg/README.md`), so `tools/recomp_dbg/akidbg.py` works with either.

Use it when you need **save states**. The emulator holds the whole machine, so a state restores
everything exactly and a replay from it is deterministic. The recomp can't do that: its other game
threads run as native code (see the recomp debug server's README). The addresses are the same as
in IDA and `_re_docs`, since this is the original MIPS code.

## Setup

- `_tools/libretro/mupen64plus_next_libretro.dll`: from
  `https://buildbot.libretro.com/nightly/windows/x86_64/latest/mupen64plus_next_libretro.dll.zip`
  (the build tested on 2026-10-31 reports `3.0-Vulkan 15bada8`). Save states only load into the
  same core build, so keep the DLL with the states.
- Python 3 (standard library only; the window uses tkinter). Nothing is Windows-only: on Linux or
  macOS, put the core's `.so` / `.dylib` build (same buildbot, `linux/x86_64` or `apple/osx/...`)
  in `_tools/libretro/` under the same name. Only Windows has been tested.
- The ROM, `_roms/WCW vs. nWo - World Tour (USA).z64` by default (`--rom`).

## Running

```
py tools\n64emu\akiemu.py                                    # window, boots to the title
py tools\n64emu\akiemu.py --quickmatch                       # walks the menus to a CPU-vs-CPU single match
py tools\n64emu\akiemu.py --state _tools\libretro\states\wt_singles_start.state
py tools\n64emu\akiemu.py --headless --fast --state <file> --paused
```

| Option | |
|---|---|
| `--game wcw\|revenge\|wm2000\|nomercy` | Which game (default `wcw`): picks the ROM in `_roms` and the game's frame-counter and idle-loop addresses. Only World Tour has `--quickmatch`. No Mercy (Rev 1) counts a game frame when libultra's next VI context gets a new framebuffer and patches its idle loop's `rng_main` call at 0x80000570 (2026-11-02). WrestleMania 2000 isn't analysed yet: `frame` counts VIs and `--no-idle-rng` is refused |
| `--headless` | No window |
| `--fast` | Run as fast as possible (about 5x real time headless) instead of 60 VIs a second |
| `--quickmatch` | Walk the menus to an Exhibition single match, CPU vs CPU (the recomp Quick Match mod's steps); `--venue 0..4` picks the mat |
| `--state FILE` / `--paused` | Load a state once booted / start paused |
| `--no-idle-rng` | NOP the idle loop's `rng_main` call (`0x80000584`), so `rng_main` only advances through game code, as in the recomp and the library; `/status` `idle_rng` says whether the call is still there. Use it for traces (`_re_docs/state_comparison.md`) |
| `--tick-hook` | No Mercy only: point match_frame's one call of `match_tick` (0x800D9DFC) at a small wrapper in unused expansion RAM (0x807FF000; the counter and a ring of COP0 Count stamps at 0x807FF100). A game frame then ends when a match tick has finished and the next has not started, so a pause always falls between two whole ticks (without it, 4-wrestler recordings catch the game mid-tick). `/status` gains `tick_hook`, `ticks` and `ticks_in_frame` (1, or more if several ticks finished inside one step), and `/ticks?since=N` lists each tick with the VI it was seen by and Count at its entry and exit (Count runs about 811,000 a VI in this core). Use it for 4-wrestler traces (2026-11-03) |
| `--port N` | HTTP port (default 5664, or `AKIDBG_PORT`); the recomp's mod uses 5664 too, so run one at a time or pick another |
| `--size N` | Window picture size, 1 to 4 (1 = 320x240) |

`_tools/libretro/states/wt_singles_start.state` is a single match a few seconds after the bell,
made with `--quickmatch`. Making another: drive the game (or `--quickmatch`) to the moment you
want, then `save?file=...` or F5.

The renderer is angrylion (software, no GPU needed), the RSP is HLE, and there's no Controller Pak
(the game asks once whether to play without saving; `--quickmatch` answers it).

## Revenge

`--game revenge` (added 2026-11-01) runs WCW/nWo Revenge: the framebuffer index is `0x8002EEC8`
(graphics thread `0x80002CC4`) and the idle loop's `rng_main` call is at `0x80000560`. Tested
headless: boots, counts 30 frames a second, steps and saves. The menus were driven with `/pad`
(Start, then Exhibition, Single Match, 1P vs CPU, A through the picks) to a match at Nitro;
`_tools/libretro/states/rv_1p_cpu_start.state` is that match a few seconds in. Revenge's
wrestlers are at `0x800F9D10`, 944 bytes each (anim +8, state +0xA, step +0xC).

## The window

`--size 1..4` sets the picture size (1 = 320x240, 2 = 640x480, ...).

| Key (P1) | N64 | | Key | Does |
|---|---|---|---|---|
| K | A | | F2 | Pause / resume |
| J | B | | F3 | Step one game frame |
| O | Z | | F4 | Mark: pause and log a `mark` event |
| E | Start | | F5 | Save state to the slot `quick` and `_tools/libretro/states/quick.state` |
| W A S D | D-pad | | F9 | Load `quick` (the slot, or the file after a restart) |
| Arrows | C up/down/left/right | | F7 (held) | Fast forward, 4x |
| U / I | L / R | | Ctrl+S | Save state to a file (dialog) |
| Q | Stick up | | Ctrl+O | Load state from a file (dialog) |
| | | | F8 | Input recorder: start / stop and save (No Mercy, see below) |
| | | | F6 | Cancel the recording (nothing is saved) |

The Save and Load buttons above the pads do the same as Ctrl+S / Ctrl+O; the dialogs open in
`_tools/libretro/states/` (git-ignored, as is `*.state`), and the game is held still while they're
up. Under the picture are clickable pads for P1 and P2: every N64 button and four stick directions,
held for as long as the mouse button is. Hotkey and dialog actions show up in `/status` `events`.
`/pad` overrides still win over the window's input.

To the right of the pads, **Auto input** presses one button (or stick direction) for one player
in a loop: held for *Hold* VIs, released for *Wait* VIs, while *Enabled* is ticked (1 VI = 1/60 s, a
World Tour game frame is 2 VIs). The loop counts emulated VIs, so it stops while paused and speeds
up with F7; switching it on starts with a press. Useful for mashing or walking through menus.

## Recording input (No Mercy)

Play a match in the window and get a save state plus a trace script that replays it frame-exactly
(added 2026-11-03). Run with `--game nomercy --no-idle-rng --tick-hook` (F8 refuses otherwise),
e.g. `py tools/n64emu/akiemu.py --game nomercy --no-idle-rng --tick-hook --size 2 --state <file>`.

- **F8** starts a recording at the end of the next match tick, holding a save state in memory. The
  line under the status shows `REC m:ss.s (N ticks)` while it runs (30 ticks a second).
- **F8** again stops it and pauses the game, and asks for a name: Save writes
  `_tools/libretro/states/<name>.state` and `tools/trace/scripts/<name>.json`; Discard (or Esc)
  throws it away. **F6** cancels a running recording at once. Loading a state (F9, Ctrl+O, `/load`)
  also discards it.
- What is recorded is what the game read, not the keys: the tick wrapper copies `g_pads`
  (0x8009EDA0, filled by `pads_read` just before `match_tick`) into a ring at 0x807FF308 on every
  tick, and the recorder logs it with a CRC of the whole RDRAM at the end of each frame.
- The script feeds the stick as `nx=`/`ny=`, the exact N64 values; akiemu inverts the core's
  stick mapping (`analog_for`: radial 15% deadzone, scaled to 80, rounded per axis). Every value
  from -80 to 80 on both axes is reachable.
- The game reads a pad one tick after it is fed (`pads_read` takes the result of the read it
  started on the previous pass), so script frame k carries the pads of recorded tick k + 2, and
  `load_pads` those of tick 2, fed on the step `record.py` takes after the load. The script is one
  tick shorter than the recording.
- Replay: `py tools/trace/record.py --port N --script tools/trace/scripts/<name>.json OUT.akt`
ecord.py --port N --script tools	race\scripts\<name>.json OUT.akt`
  on an akiemu started with the same two options. It compares the RDRAM CRC at every record with
  the recording's (`ram_crc`) and prints `RAM matches the recording at all N records` or the first
  record that differs. Checked 2026-11-03 headless: 300 ticks of random buttons and sticks for P1
  and P2, changed mid-frame, from `nm_1p_2p_normal_match_start.state`: all 299 records match, and
  the same script without `load_pads` differs from record 0 on. `nm_replay` reads the trace.
- Don't open the in-game pause menu while recording: no match tick runs there, so its input isn't
  recorded. A state saved hooked by an older akiemu gets the current wrapper on load.

`/record?do=start`, `/record?do=stop&name=NAME` (returns the files) and `/record?do=cancel` do the
same over the API; `/status` `recording` is the ticks so far (or null).

## The API

Everything in the recomp server's table, with these differences:

- **Frames.** `frame` counts game frames (30 a second): the emulator runs one VI (1/60 s) at a
  time, and a game frame is counted when the graphics thread flips to its next framebuffer
  (`0x8002D2B0`, which changes once per game frame in menus and matches). Pausing happens between
  VIs, so a paused game sits just after a flip, not exactly at `pads_read` as in the recomp.
  `vi` counts VIs; `/vi_step?n=` steps by VI.
- **Save states** (`/save`, `/load`, slots and files) restore everything, at any time. A file is
  the core's own state with a small header (frame and VI counts); a bare core state from RetroArch
  with the same core build loads too.
- **Pads.** `/pad` feeds the emulated controller through the core, so the game reads it through
  its own `pads_read`. Overrides last `frames` game frames, or until `/pad?clear=1`.
- `/screenshot` returns the last frame as a PNG (also `akidbg.py screenshot out.png`).
- `/pad` also takes `nx=`/`ny=`: the stick exactly as the game should read it (instead of `sx`/`sy`).
- `/crc`: CRC-32 of the whole RDRAM (what the input recorder logs). `/ticks` entries carry `pads`,
  (held, stick x, stick y) per port as each tick read them.
- `/log?n=50`: the core's log. `/regs` isn't available.

## Tested (2026-10-31)

In a CPU-vs-CPU single match, headless: 1 and 30 frame steps (one match clock tick per frame);
save, run 300 frames, load, three times over, with identical wrestler state, clock and
`rng_match` state after each replay; 100 rounds of random steps followed by a load and a step,
with no hang. The window was smoke-tested (it runs at real time); its keys haven't been tried.
