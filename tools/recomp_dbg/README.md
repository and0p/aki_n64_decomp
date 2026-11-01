# aki_debug: a debug server for the World Tour recomp

A mod for the prebuilt `WCWRecompiled.exe` (in `_tools\WCWRecompiled`) that serves a small HTTP API
on `127.0.0.1:5664` for reverse engineering: pause, frame step, read and write RDRAM, memory
breakpoints, controller overrides and snapshots. Nothing in the recomp is rebuilt.

It has two halves:

- `mod/aki_debug.c`: MIPS mod code. It hooks `pads_read` (`0x8000544C`), which the frame loops call
  once per game frame (30 per second), before `match_tick` in a match. It calls the native library
  at the hook's entry and on its return.
- `native/aki_debug.cpp`: `aki_debug.dll`, loaded by the recomp's mod loader from next to the
  `.nrm`. It counts frames, decides when to pause, and runs the HTTP server.

A paused game sits **between two frames**: the previous frame's logic is done, the next frame's
input has not been read. The window keeps showing the last frame and audio keeps running.

`pads_read` is called by the match loop (`match_run`) and the menu loop (`match_run_loop`), so
frames are counted and pausing works in both. Each loop has its own stack pointer (`sp` in
`/status`: `0x8005C8D0` in a match).

## Revenge

The same mod builds for WCW/nWo Revenge (`build.ps1 -Game revenge`, output in
`_tools\dbg_build\revenge`, installed into `_tools\RevengeRecompiled\mods`). The game-specific
addresses are at the top of `native/aki_debug.cpp` and in `mod/aki_debug.c` (`AKI_GAME_REVENGE`):
pads_read `0x80003E78` (called only by the match loop, so frames count only in a match), the
scheduler `0x8003DE30` (add-client `0x80000580`), the graphics thread `0x800445B0`, `g_pads`
`0x8006E178`, and the thread structs and stacks from the `osCreateThread` calls. Three of the
stack sizes are assumed (0x2000, like the others). Built 2026-11-01 but **not yet run**: there
was no Revenge recomp build on the machine.

## Hotkeys

In the game window (not while the recomp's own menu is open):

| Key | Does |
|---|---|
| F2 | Pause / resume |
| F3 | Step one frame (pauses first if running) |
| F4 | Mark: pauses and logs a `mark` event, to point a client (or Claude) at this moment |
| F5 | Quick save into the in-memory slot `quick` (pausing only for the save if running) |
| F9 | Quick load from `quick` (likewise) |

F1 (RT64 inspector), F6 (free camera) and F11 (fullscreen) belong to the recomp and its bundled
mods; Esc opens the recomp's menu. Every hotkey action is listed under `events` in `/status` (the
last 32, each with `seq` and `frame`) and written to `WCWRecompiled.log` as `[aki_debug] ...`.
The quick slot is an ordinary snapshot: `load?slot=quick` works too, and so does F9 after a
client's `save?slot=quick`.

## How pausing works

The runtime (ultramodern) runs one N64 thread at a time and hands the CPU over only when a thread
blocks in an OS call. So a paused game thread must not just block in native code: the scheduler,
graphics and audio threads would stop with it, and the VI and RSP events queued meanwhile would be
dropped, which hangs the game on resume. Instead, the mod registers its own scheduler client (a
1-message queue at `0x80000200`, unused low RAM) and the paused loop waits there in `osRecvMesg`
for each retrace, then asks the DLL whether to stay paused. Commands are picked up within a
retrace (1/60 s).

While paused, the graphics thread waits for the game's next draw request and stops reading its
scheduler queue (`0x80040F68`, 16 messages). Once that fills, the scheduler blocks on its next
send and everything stalls, so on every paused retrace the DLL drops the oldest retrace messages
from the scheduler's client queues (keeping the newest two and every other message).

## Building

One-time toolchain, all in git-ignored `_tools\dbg_toolchain`:

- Zig 0.13 (`zig-windows-x86_64-0.13.0`), for `zig cc` (MIPS) and `zig ld.lld`.
- `N64Recomp` (commit `ffb39cd`, the one the recomp pins), with `RecompModTool` built into
  `N64Recomp\build` (CMake + Ninja + MSVC).
- `MMRecompModTemplate` (`include\modding.h`) and `AkiMods` (`common\mod.ld`).
- The recomp's `WCWSyms` submodule (`_recomps\WCWvsNWOWorldTourRecomp\WCWSyms\dump.toml`).

Then:

```powershell
.\tools\recomp_dbg\build.ps1          # builds and copies both files into _tools\WCWRecompiled\mods
```

The DLL is locked while the game runs: `py tools\recomp_dbg\akidbg.py quit` first. New mods are
switched on when installed. Start a match with `tools\single_match.ps1` (the bell rings around
frame 1800). `AKIDBG_PORT` changes the port, and `AKIDBG_PAUSE_AT=<frame>` pauses on reaching
that frame; both are read from the environment `single_match.ps1` is started from.

## The API

Every endpoint is a GET (or a POST for `/write` with a raw body) and answers JSON, except
`/read?binary=1`. Numbers can be decimal or `0x` hex. Addresses can be virtual (`0x80xxxxxx`) or
physical, within the game's 8 MB; memory is shown big-endian, as on the N64.

| Endpoint | What it does |
|---|---|
| `/status` | `frame`, `paused`, `at_hook`, `reason`, `sp`, breakpoints, snapshots, hotkey `events` |
| `/pause?timeout=ms` | Stops at the next frame hook and waits until it's there |
| `/resume` | Runs freely |
| `/step?n=1&timeout=ms` | Runs n frames, then answers once paused again |
| `/run?frame=F&timeout=ms` | Runs until frame F |
| `/read?addr=&len=` | `{"hex": ...}`; add `binary=1` for raw bytes |
| `/write?addr=&hex=` | Writes bytes (or POST the raw bytes) |
| `/break?addr=&size=1/2/4&eq=V` | Pauses at the first frame start where the value is V (also `ne=V`, `change=1`); `/break?clear=1` |
| `/pad?port=0..3&held=&sx=&sy=&frames=` | Overrides a controller after the game reads it (held, pressed, stick); `/pad?clear=1` |
| `/save?slot=&file=` | Snapshot while paused, in memory and/or to a file (8 MB) |
| `/load?slot=` or `/load?file=` | Restores a snapshot while paused |
| `/drop?slot=` | Forgets a snapshot |
| `/regs` | The MIPS registers in the paused loop |
| `/quit` | Ends the game process at once (nothing is saved) |

`frame` counts hook calls since the game started. While paused, `frame` is the frame about to run.
Breakpoints are checked once per frame, at the hook, so they catch values between frames, not
the write itself.

## The CLI

```
py tools\recomp_dbg\akidbg.py status
py tools\recomp_dbg\akidbg.py pause
py tools\recomp_dbg\akidbg.py u16 0x800F16EC
py tools\recomp_dbg\akidbg.py read 0x800F07F0 0x40
py tools\recomp_dbg\akidbg.py step 30
py tools\recomp_dbg\akidbg.py break 0x800F16F0 2 change
py tools\recomp_dbg\akidbg.py save start
py tools\recomp_dbg\akidbg.py resume
py tools\recomp_dbg\akidbg.py quit
```

From Python, `akidbg.Debugger()` has the same calls (`pause`, `step`, `run_to`, `u16`, `read`,
`write`, `break_eq`, `pad`, `save`, `load`, ...).

## Snapshots: use the emulator for loads

**Loading a snapshot in the recomp is unreliable; use `tools/n64emu` (the ROM in an emulator, same
API) when you need save states.** Saving, and reading a saved snapshot's frame, are fine.

A recomp runs the game's MIPS code as native code. Only the game thread is parked at the hook;
the other N64 threads (scheduler, graphics, audio, sound, idle) keep running natively, and where
each one is lives partly in native code a snapshot can't hold. A snapshot is the game's 8 MB of
RDRAM plus the game thread's MIPS registers, and restoring all of it overwrites data those threads
are in the middle of using. A load therefore:

- keeps each N64 thread's OSThread struct, the other threads' MIPS stacks, and the wait lists of
  the queues they wait on, as they are live;
- is refused unless every thread waits on the same queue as at save time ("step a frame and try
  again");
- and, like a save, only runs once the graphics thread is idle between frames and the scheduler
  is waiting for an interrupt (never, in menus that are loading).

With all that, a stress test in a match (random steps, then a load, 80 times over) still crashed
after about a minute on 2026-10-31, so treat recomp loads as experimental.
