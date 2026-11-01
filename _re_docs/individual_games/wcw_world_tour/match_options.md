# World Tour: match modes and options

Andrew's notes from the game's menus (2026-10-30). Memory locations have not been looked for
yet unless noted.

- **Ring-out:** 10 seconds, 20 seconds, lumberjack, or off.
- **Quick match** mode.
- **Tag team format:** a chooser that sets whether tagging is required.
- **Finishes:** pins, submissions and TKOs can each be turned off (`g_match_rules` bits 0x20, 0x10, 0x01; see `match_end.md`).
- **Tag team elimination:** the match ends when one teammate is eliminated, or only when
  both are.

## Already seen in code

- The ring-out option looks like `g_countout_setting` (`0x8006205D`, used by `countout_tick`).
  `g_countout_lengths` (`0x800E7450`) reads 0, 10, 20, 0. So the value is probably 0 off,
  1 ten count, 2 twenty count (the paused match), 3 lumberjack. That match of the fourth
  entry to lumberjack is a guess; lumberjack would then have no count of its own.
- The neighbouring words `0x80062058` and `0x8006205A` are tested as bit fields by the
  count-out and grapple code (`0x80062058 & 0x200` / `& 0x600`, `0x8006205A & 4`). They are
  probably other match options, perhaps the tag format and the disabled finishes. Not checked.
- The match time limit is `0x8006205C` (see `match_state.md`).

## Seen in RAM

| Address | Luger singles (snapshots 5-8) | Tag team (snapshot 9) | Notes |
|---------|-------------------------------|-----------------------|-------|
| `0x80062058` (u16) | 0x012F | 0x02C0 | match type bits; `& 0x200` makes `countout_tick` count only LEGAL wrestlers, `& 0x600` lets L/R start rear grapple moves |
| `0x8006205A` (u16) | 0x0033 | 0x0033 | `& 4` turns the count-out off |
| `0x8006205C` (u8) | 0 | 0 | time limit (0 = none) |
| `0x8006205D` (u8) | 2 | 0 | ring-out setting (2 = 20 count, 0 = off) |
| `0x80062060` (4 × u16) | 1, 1, 1, 1 | 1, 1, 1, 1 | CPU difficulty per slot, 0–2 (`ai.md`) |
