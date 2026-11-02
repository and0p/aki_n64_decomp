# WCW/nWo Revenge: controls and unlocks

Notes from Andrew (2026-11-01), from the GameFAQs guides (six unlockable wrestlers plus the
THQ Man swap; one more is said to need a GameShark). Not yet checked against the code;
`input_*` handlers are named in `symbols/revenge/` where matched to World Tour.

## Basic controls

| Input | Does |
|-------|------|
| C-Left | Flip an opponent over on the mat |
| C-Right | Change focus (target) |
| C-Up | Climb the turnbuckle; climb back up onto the apron; from the apron climb into the ring; tag a partner; grab a weapon from the crowd |
| C-Down | Run |
| A | Grapple |
| B | Punch or kick, depending on the distance to the opponent |
| L | Pin an opponent on the mat; counter moves |
| R | Block attacks; counter moves |
| D-pad | Move |
| Analog stick | Taunt |

Differences from World Tour worth checking in code: C-Left flips a downed opponent (World Tour
had no flip button), and the dazed turn-around moved to L (states 0x83 / 0x84, `states.md`).

## Unlocks and secrets

| Unlock | How |
|--------|-----|
| Curt Hennig | Win the U.S. Heavyweight title (also opens the World Heavyweight title championship mode) |
| Billy Kidman | Win the Cruiserweight title (also opens the TV title championship mode) |
| Meng and Barbarian | Win the Tag Team titles |
| Roddy Piper | Win the U.S. title, then the World Heavyweight title |
| Kanyon (Mortis) | Win the Cruiserweight title, then the TV title |
| THQ Man | In the EWF division, highlight AKI Man and press C-Down to swap to THQ Man (and back) |
| Play as a manager | Controllers 3 and 4 press Z; both picked wrestlers must have managers |
| Stay down | When knocked down, hold the analog stick in a corner: the wrestler stays down until it is released or the opponent picks them up |

The hidden wrestlers are in the select table at `0x8003A1EC` (record pointers in select order:
Hennig after the nWo White list, Kidman after Raven's Flock, Piper before DDP, Meng after
Glacier, Kanyon after Van Hammer, Barbarian and Wrath after Larry Zbysko; then alternate entries
63–79 for the second costumes, managers Onoo and Jimmy Hart). The "stay down" trick is
probably the analog-stick taunt input read while down; worth finding in `state_2C_down`.
