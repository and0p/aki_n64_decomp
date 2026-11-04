# AKI N64 Wrestling Game Annotated Decompilations (WIP)

This repository contains high-level documentation and virtually (*see [Parity](#parity)*) frame-perfect annotated decompilations of AKI's series of N64 wrestling games, starting with No Mercy.

These documents and decompilations should enable easy creation of modern ports.

Decompilation is (currently) focused on match gameplay, not menus, create-a-wrestler, or story modes.

## Immediate Goals

* Create deep documentation on the inner workings of match and other gameplay logic. Menus and career modes might come later.
* Create AI-assisted annotated decompilations (commented and documented source code) in simplified C-style C++17 and match them frame-by-frame with an emulator to achieve 100% parity. This code is currently not being byte-matched, meaning that I am not trying to create C code that, with the same compiler as the original dev team used (assuming it as available or can be reverse engineered) produces functions with the exact same bytecode. Instead, this code is being tested against input replays and save states for game state and RAM parity for all functiosn and branches.

## Progress

As of writing, No Mercy has been roughly 65% decompiled with annotations and parity tested against emulation.

## How this differs from other recomp / decomp projects

* [jessetbh](https://github.com/jessetbh) has made excellent recomps of all the AKI games, which use *"[N64Recomp](https://github.com/N64Recomp/N64Recomp) to **statically recompile** the [games] into a native PC port, running on the [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) with [RT64](https://github.com/rt64/rt64) as the rendering engine."* These are strict ports of game bytecode, running logic natively with virtual devices emulating video and other hardware. It allows for some modding and applying ROM hacks.
* Some decomps (such as [fnvdecomp](https://fnvdecomp.com/)) strive for byte-identical output. This means that each function in the decompilation should compile down to the exact sequence of bytes as the original function. I'm not doing that here, but am instead stepping through emulated versions of these games to enforce exact parity in game state. In other words, my decomp should have a CPU vs CPU match play the exact same way indefinitely (assuming RNG is managed -- see [Parity](#parity)). This decomp should be easier to mod extensively, but still difficult.
* Eventually, a port using modern game architecture can be built much easier using this annotated decomp than by comparing the original byte code. In a way this repo contains the first very large step in supporting that type of project.

## Contributing
This approach to decompilation is WIP. Once No Mercy is 100% I'll restructure this project and solidify the workflow so that the other games are open to AI decomp. At that point others willing to donate compute will be helpful.

As of writing the annotated decomp is going well but slowly, and a $100/month Claude subscription is being churned through pretty quickly -- the weekly limit is hit in about 3 days. If you want me to work through this (and the port) a little faster throw me some money on [kofi](https://ko-fi.com/apdecomp).

I'm also open to architectural suggestions on the upcoming port, as seen below, and feature requests.

## Future port
The goal is to eventually create a modern reimplementation of the engine (in a separate repo) that is:
* Portable to all modern platforms, including WASM for browsers.
* Leveraging a data-driven Entity-Component-System architecture for simpler extensibility.
* Multiplayer-enabled with rollback netcode.
* Moddable via asset packs and scripts.
* Low-overhead and minimal, but supports mods or forks with advanced graphics.
* Enabled toggling of features and profiles to emulate gameplay with the different games. No Mercy should be frame-perfect, others may be slightly less so.
* Derived from the annotated decompilation and seeking frame-perfect parity with unmodified No Mercy gameplay (parity with previous games in the series TBD)
* Is packaged with tools for the extraction and management of assets from legally-owned backups and community creators.

I am open to suggestions and requests on this. I'm looking at [bevy](https://github.com/bevyengine/bevy) as a minimalistic Rust engine.

## Parity

### RNG (Random Number Generator)
RNG in these games are based on increments happening randomly in an idle thread in a busy loop, so randomness is affected by spare CPU time. This makes for a hardware-based RNG that is virtually impossible to replicate meaningfully on other systems, so I do not attempt to. When checking for frame parity between the C++ code and the emulator I NOP this increment across the board and enforce RNG parity.

### Slowdowns and Framerate
These games are multithreaded. The main logic thread is supposed to tick at 30hz, but with multiple CPU characters on screen it can sometimes slow down to around 28hz. This does not affect gameplay outcomes, it just slows the game down. It is not the goal of these decomps to replicate this.

Video and audio also run on their own threads, so syncronization on actual hardware is another variable that would be very difficult to emulate. An actual N64 might show a more variable frame-rate where the main thread fails to update gameplay state and hand it off in time.

### NTSC vs PAL
This repo is dealing with the NTSC versions of these games, which should include all US and Japanese releases, covering all games. If the PAL versions (the EU release of No Mercy, for example) ticks at 25hz that will not be supported here.

## AI Disclosure
AI is being used almost exclusively in this decompilation.

AI has not been used to write this document at all.

## Credits
* [jessetbh](https://github.com/jessetbh) for his original recomps, which were used for early interactive debugging.
* [N64Recomp](https://github.com/N64Recomp/N64Recomp) for some inspiration and some tooling.
* AKI for making really fun games.