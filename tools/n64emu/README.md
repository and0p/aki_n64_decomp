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
- Python 3 (standard library only; the window uses tkinter).
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
| `--game wcw\|revenge` | Which game (default `wcw`): picks the ROM in `_roms` and the game's frame-counter and idle-loop addresses. Revenge has no `--quickmatch` |
| `--headless` | No window |
| `--fast` | Run as fast as possible (about 5x real time headless) instead of 60 VIs a second |
| `--quickmatch` | Walk the menus to an Exhibition single match, CPU vs CPU (the recomp Quick Match mod's steps); `--venue 0..4` picks the mat |
| `--state FILE` / `--paused` | Load a state once booted / start paused |
| `--no-idle-rng` | NOP the idle loop's `rng_main` call (`0x80000584`), so `rng_main` only advances through game code, as in the recomp and the library; `/status` `idle_rng` says whether the call is still there. Use it for traces (`_re_docs/state_comparison.md`) |
| `--port N` | HTTP port (default 5664, or `AKIDBG_PORT`); the recomp's mod uses 5664 too, so run one at a time or pick another |
| `--scale N` | Window zoom |

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

| Key | N64 | | Key | Does |
|---|---|---|---|---|
| X | A | | F2 | Pause / resume |
| C | B | | F3 | Step one game frame |
| Z | Z | | F4 | Mark: pause and log a `mark` event |
| Enter | Start | | F5 | Save state to the slot `quick` and `_tools/libretro/states/quick.state` |
| Arrows | D-pad | | F9 | Load `quick` (the slot, or the file after a restart) |
| A / S | L / R | | | |
| I J K L | C up/left/down/right | | | |

The stick isn't on the keyboard (`/pad` sets it). Hotkey actions show up in `/status` `events`.

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
- `/log?n=50`: the core's log. `/regs` isn't available.

## Tested (2026-10-31)

In a CPU-vs-CPU single match, headless: 1 and 30 frame steps (one match clock tick per frame);
save, run 300 frames, load, three times over, with identical wrestler state, clock and
`rng_match` state after each replay; 100 rounds of random steps followed by a load and a step,
with no hang. The window was smoke-tested (it runs at real time); its keys haven't been tried.
