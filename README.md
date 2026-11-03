# AKI N64 Wrestling Game Annotated Decompilations (WIP)

This repository contains a WIP documentation and annotated decompilation of gameplay logic for N64 AKI wresting titles:



## Goals and Methodology

* Create deep documentation on the inner workings of match and other gameplay logic. (Menus and career modes might come later.)
* Create AI-assisted annotated decompilations (commented and documented source code) in simplified C-style C++17 and match them frame-by-frame with an emulator to achieve 100% parity. This code is currently not being byte-matched, meaning that I am not trying to create C code that, with the same compiler as the original dev team used (assuming it as available or can be reverse engineered) produces functions with the exact same bytecode. Instead, this code is being tested against input replays and save states for game state and RAM parity.
* Eventually (and possibly in another repo) create a modern reimplementation of the engine that is:
    * Portable to all modern platforms, including WASM for browsers.
    * Leveraging a data-driven Entity-Component-System architecture for simpler extensibility.
    * Multiplayer-enabled with rollback netcode.
    * Moddable via asset packs and scripts.
    * Low-overhead and minimal, but supports mods or forks with advanced graphics.
    * Enabled toggling of features and profiles to emulate gameplay with the different games. No Mercy should be frame-perfect, others may be slightly less so.
    * Derived from the annotated decompilation and seeking frame-perfect parity with unmodified No Mercy gameplay, and close parity with 
    * Is packaged with tools for the extraction and management of assets from legally-owned backups and community creators.

### Parity

#### Between games
Frame-perfect parity against (emulated) N64 gameplay is only pursued where possible. A character from World Tour in a match with added No Mercy features will obviously break parity.

I'm prioritizing No Mercy for 100% parity, and use settings (or profiles of multiple settings) to disable features and implement alternative logic all the way back down to World Tour. Parity in these feature sets are considered nice-to-have as an eventual goal.

#### RNG (Random Number Generator)
RNG in these games is based on increments happening randomly in an idle thread in a busy loop, so randomness is affected by spare CPU time. This makes for a hardware-based RNG that is virtually impossible to replicate meaningfully on other systems, so I do not attempt to. When checking for frame parity I NOP this increment in the emulator and enforce RNG parity manually.

#### Slowdowns
These games are multithreaded. The main logic thread is supposed to tick at 30hz, but with multiple CPU characters on screen it can sometimes slow down to around 28hz. This does not affect gameplay outcomes, it just slows the game down. It is not the goal of any recreations to emulate this.

Video and audio also run on their own threads, so syncronization on actual hardware is another variable that would be very difficult to emulate.

## AI Disclosure
AI is being used almost exclusively in this decompilation.

## Credits
