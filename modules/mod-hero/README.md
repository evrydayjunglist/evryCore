# Hero native specializations

Hero has three modes on the normal specialisation page, playable from level one, and a hidden Initial specialisation that every new Hero starts on:

| ID | Mode | Order index | Talent group |
|---|---|---|---|
| 601 | Free Pick | 0 | 0 |
| 602 | Free Pick — Seasonal | 1 | 1 |
| 603 | Wildcard | 2 | 2 |
| 600 | Initial | 4 | 4 |

Status on 22 September 2026: activating a mode and switching between modes work in game, and the names were checked there. Relogging on each mode, the six switches on a Hero with borrowed spells, and the spellbook on each mode are not checked yet. The modes grant nothing yet: no spells, talents, currency or acquisition, and a Hero's learned spells are shared by all three.

- `data/sql/db-hotfixes/2026_09_21_00_mod_hero_three_specializations.sql` writes Hero's four specialisation rows, points the class default at 600, and publishes one hotfix push that also retires Initial's old id, 1479.
- `data/sql/db-hotfixes/2026_09_22_00_mod_hero_specialization_activation.sql` adds Hero's class bit to Blizzard's skill line ability row 35162, for the hidden activation spell 200749, and keeps every other field. The client only uses that spell for the Activate button once the row admits the player's class.
- `data/sql/db-characters/2026_09_21_01_mod_hero_specialization_actions.sql` copies each existing Hero's Initial action bars into the three modes' empty bar sets.
- `data/sql/db-characters/2026_09_22_01_mod_hero_specialization_ids_characters.sql` moves saved Heroes from 1479 to 600 and clears their empty per-mode loadouts, which the server rebuilds at login under the current mode names.
- `src/mod_hero.cpp` gives every Hero spell 200749 at creation and login, without replacing a copy learned by hand.
- Two changes in `src/server/game/Entities/Player/Player.cpp`, on `evry` only: a Hero keeps a chosen mode below level ten, which other classes still do not; and a switch removes only the outgoing specialisation's PvP talent spells, so it cannot take away another class's spell that a Hero learned. There is no new core hook.

The client half is in evryLoader: the specialisation page fixes, a guard in Blizzard's micro menu, and `/evryspec`. The full record is [HERO_SPECIALIZATIONS_69814.md](../../../evryLoader/docs/HERO_SPECIALIZATIONS_69814.md), and the owner procedure is [OWNER_ACTIVATION_TEST.md](../../../evryLoader/diagnostics/hero-specs/OWNER_ACTIVATION_TEST.md).

Future acquisition work has to tell spells a player learned by hand from spells a mode grants, and keep these ids.
