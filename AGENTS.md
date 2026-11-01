## Goal

-Derive gameplay state, functionality, graphics, and audio for matches in the AKI series of wrestling games for N64 into a C/C++ library (or set of libraries) that are agnostic to graphical implementation and easily portable to other languages and environments such as webassembly in the browser.
- Other facets of the games (intros, menus, "story" or "career" modes, customization ("create-a-wrestler"), etc) are not relevent as they will be reimplemented and recontextualized in more modern ways.
- N64 rendering logic should be recontextualized for modern shader-based rendering pipelines. Quirks and glitches in the N64 rendering pipeline should be understood but will ultimately be reimplemented.

## Context

AKI released a series of 3D wrestling video games for the N64. The relevent titles, in order:

- WCW vs. nWo: World Tour (1997)
- Virtual Pro Wrestling 64 (1997, Japan only)
- WCW/nWo Revenge (1998)
- WWF WrestleMania 2000 (1999)
- WWF No Mercy (2000)

Within the `_recomps/` folder are git submodules for existing recomps for all five games. They were seemingly created in matching order of release, and share some context and references to earlier projects to avoid listing redundant information. These recomps use advanced graphical libraries to replicate how the N64 rendered, which we do not need to replicate.

Within `_roms/` are big endian (z64) ROM files for these games, which I legally backed up from my own cartridges. One exception is Virtual Pro Wrestling 64 as I have no need to own a Japanese N64 game, but my understanding it is very nearly identical to WCW vs nWo: World Tour.

## Decisions

- Start with WCW vs. nWo: World Tour. VPW64 should follow closely, since it is nearly identical.
- Language: C++17 core exposed through a plain C API, built for native and for WebAssembly (Emscripten). The core avoids exceptions, RTTI, threads, and any platform or GPU calls.
- Build system: CMake with CMakePresets and Ninja, plus Emscripten's toolchain (`emcmake`) for the WebAssembly build; tests run under CTest.
- License: GPL-3.0, so code and findings can be borrowed from the GPL-3.0 recomps in `_recomps/`.
- Fidelity: game logic, AI and animation are frame-exact with the original and run at the original frame rate. Higher display frame rates come from interpolating poses in the host. Known inconsistencies may be logged and fixed later; these parameters may change as research continues.
- Assets: a separate converter tool turns a ROM into a folder of assets (wrestler metadata, textures, rings, arenas, moves, animations, etc.) that the library reads. The library never reads ROMs directly, and no ROM-derived data is committed. The exact split is decided as each game is analysed.
- One shared engine for all games. Where a newer game uses a newer format, the asset format is versioned (or the old format is ported) so data from every game can be used; the approach is chosen once analysis is complete.
- CPU opponent AI is in scope.
- Reverse-engineering notes live in `_re_docs/` (see its README for layout).
