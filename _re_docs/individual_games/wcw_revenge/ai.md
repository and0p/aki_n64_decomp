# WCW/nWo Revenge: CPU AI

Part of the WCW/nWo Revenge notes (index: `README.md`). Written as differences from World Tour
(`../wcw_world_tour/`); anything not mentioned is not checked yet, not known to be the same.

## CPU AI

Read from `ai_think` (`0x800A9924`) and the mode table (`0x800E3660`), compared with World
Tour's `ai.md`. Names in `symbols/revenge/ai.toml`.

- **Same design:** the AI still presses buttons into a per-slot block, picks a mode, and runs
  the mode's handler each frame. Mode changes reset the step, remember the opponent's state and
  clear the low flag bits, as in World Tour.
- **AI block: 96 bytes per slot at `0x800F9B88`** (World Tour 84 at `0x800F1710`). Same at the
  start (mode +2, step +4, held +6, pressed +8, stick +0xC, skill +0x12, own situation +0x14);
  then moved: opponent situation +0x22 (WT +0x1C), opponent's state at mode start +0x2E (WT
  +0x24), distance and angle +0x38, corner distances +0x48 (WT +0x40), flags +0x5C (WT +0x50).
  Revenge also stores both wrestlers' ring quarter (+0x1C, +0x2A) and ring zone (+0x1E, +0x2C).
- **Modes 0–45 are World Tour's modes 0–45, in the same order**: aligning the two tables by
  the buttons each mode presses and the constants it uses gives index = index for all 46, with
  near-identical button sets (grapple, strike, block, run, the timed counters, tag…). Revenge
  adds **16 modes, 46–61** (World Tour's 46 and 47, the whip-runner hand-off and the dive to the
  floor, became part of the reflexes and mode 46).
- **Reflexes grew**: the situation classifier has more classes (up to 23). Besides World Tour's
  reflexes (grappled 16, pinned/down 18, whipped 17, dazed 37), Revenge sends classes 12 and 22
  to 37, getting up (15) to 38, grappling as attacker (16) to 14 and the whip runner (19) to 8
  directly, and two new situations (20, 21) to the new modes 50 and 51.
- New in `ai_think`: when the opponent is in state 0x29 (World Tour's TKO stagger) the CPU
  taunts 30 % of the time (mode 34) or backs off (39); and a guard resets the AI to mode 0 if a
  wrestler stays 150 frames with flags 0x280 set.

### The new modes (first reading)

| Mode | What it seems to do |
|------|---------------------|
| 46 | hold A while running, ending on states 4 / 99 (World Tour's run-and-dive, adapted) |
| 47 | run toward a corner and climb the turnbuckle (state 0x0A), holding C-down |
| 48 | like 47 from the apron: climbing in or up (states 0x2B, 0x86, 0x89–0x8A) |
| 50, 51 | in the new states 0x5D / 0x5E: wait a per-wrestler delay, then A or B |
| 52 | hold Down while below a height |
| 53 | steer on the floor by x and z separately |
| 54, 55 | wait for state 0x30, then C-right and B |
| 56 | circle the opponent (like mode 27) |
| 57–60 | wait for state 0x30, then the ground hold (mode 13) |
| 61 | close to the opponent: a skill roll gives the timed block (mode 32) |

The difficulty table (World Tour's skill 50 / 250 / 900 at `0x800E7560`) is not found yet in
Revenge.

## Other findings (2026-11-01)

- The reflex press factor (`ai_reflex_mash_rate`, `0x800B3AD8`) is shared by the whip, mash and
  dazed reflexes: 0.45 / 0.55 / 0.65 by perceived health (129+, 65–128, below 65).
- **Favourite moves**: `ai_pick_favourite` (`0x800AC208`) picks one of two move slots from the
  wrestler header (+0x24 / +0x26) with a skill chance, the second more when health is under 65.
- **CPU level**: `ai_state_init` (`0x800A97A0`) copies a per-slot level from `0x8007E2A8`
  (0–2; 3 and up become 1) into the AI block +0x10 (lead for the difficulty question).
- In 16,000 frames of CPU vs CPU singles and 90 seconds of CPU tag, modes 0–39, 43 and 44–45
  ran; none of the new modes 46–61.
