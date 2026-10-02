# evryCore (`game`)

Retail-parity World of Warcraft server. This branch is a fork of [TrinityCore](https://github.com/TrinityCore/TrinityCore) `master` with no modules.

`master` in this repo fast-forwards from upstream TrinityCore `master`. Permanent merge is one-way: `game` → `evry`. Never the other way. The boot tree is `evry`.

* [What this branch is](#what-this-branch-is)
* [Jobs on `game`](#jobs-on-game)
* [Requirements](#requirements)
* [Install](#install)
* [Reporting issues](#reporting-issues)
* [Submitting fixes](#submitting-fixes)
* [Copyright](#copyright)
* [Authors &amp; Contributors](#authors--contributors)
* [Links](#links)

## What this branch is

TrinityCore is an MMORPG framework in C++, derived from MaNGOS, with a long history of optimizing the codebase and in-game mechanics. evryCore stays on that `master` line (current retail client) and adds retail-parity work here on `game`.

Community involvement on upstream TrinityCore is unchanged: ideas and code for TrinityCore itself go to [TrinityCore.org](https://www.trinitycore.org) and [TrinityCore pull requests](https://github.com/TrinityCore/TrinityCore/pulls).

This repository: [github.com/evrydayjunglist/evryCore](https://github.com/evrydayjunglist/evryCore).

## Jobs on `game`

When a job lands on `game`, add a short entry here. The percent is feature completion: 100% means the listed work is in and only bug fixes remain.

Every entry here also belongs on the `evry` README under "From `game`", with the same text and percent. `evry`'s README is a different file, so merging `game` into `evry` can drop an entry there or add one twice without a conflict. After each such merge, compare the two lists and make sure each `game` entry appears exactly once on `evry`.

- **pandaren-faction-choice** (~80%) — A neutral pandaren with "A New Fate" in progress can choose the Horde or the Alliance. The server takes the client's choice, changes the race and faction, teaches the new race's languages and capital flight paths, moves reputation onto the new race's base, answers the client, and casts the retail faction-choice spell that gives the quest credit. While "A New Fate" is in her quest log, the Spirit of Master Shang Xi offers "I'm ready to decide.", which opens the choice window, and he offers "A New Fate" again to a player who closed it. Still open: the balloon ride and the teleport to Orgrimmar or Stormwind that follow the choice are not scripted.
- **retail-spell-resistance** (~100%) — Retired resistance stats no longer remove retail spell damage. Armor, explicit school immunities, absorbs and scripted damage reductions remain separate. Regression tests cover legacy creature resistance values, explicit damage accounting and immunity.
- **combat-stats** (~75%) — Player base stamina, health, and STR/AGI/INT from ExpectedStat.db2, including the Midnight health and primary-stat squish at current-expansion levels. `GiveLevel` uses the same overrides as login. Creature MaxHealth uses floor so create health matches retail. `DealDamageMods` scales player damage into create-level creature HP and creature damage onto the level-matched DPS curve. `ScalingPlayerLevelDelta` is refreshed after NextLevelXP is known. Hunter, Shaman, Warlock, Monk, Druid, Death Knight, and Evoker still use an average secondary split. The Midnight squish is a level-80 sniff constant. Level-ups, pet level-ups and a stats reset replace the create stats instead of adding the new level's on top of the old ones, and Feral Spirit wolves keep their own create stamina so all of the shaman's stamina share becomes health.
- **chromie-time** (~90%) — Chromie Time select, persist, gossip, and leave. Campaign breadcrumbs after select in `chromie_time_expansion_quest`. Outdoor scaling uses ConditionalContentTuning. Dungeon Finder hides and locks dungeons whose expansion bit is outside `ChromieTimeExpansionMask`. Select requires the Chromie Time interaction. Set and clear refresh phase so terrain swaps update. Silithus Wound shows unless the player is in a pre-BfA campaign. Chromie says the BroadcastText line on select; Shadowlands has no id yet. Start and re-enter from the present lock at level 68; already in a campaign may change timelines until the ContentTuning end level. Dorn refuse vs FAQ. Orgrimmar hourglass. Dark Portal Eastern Kingdoms ↔ Outland. Character wipe deletes `character_chromie_time` so a reused GUID does not inherit the old expansion. Player dumps still omit that table. Reaching the end level from a dungeon kill delays the capital teleport until after loot, so the group looter is no longer pulled out mid-kill. Other terrain swaps, raid walk-in abort, and the level-80 return quest are still open.
- **treasure-picker** (~80%) — `CMSG_QUERY_TREASURE_PICKER` replies from world `treasure_picker` rows. Offer and grant filter by AllowableClass, weapon/armor skill when AllowableClass is -1, AllowableRace, and faction item flags. Turn-in grants the first eligible row, or the chosen item on a choice picker (`CMSG_QUEST_GIVER_CHOOSE_REWARD` accepts that picker item). Arathi RPE 90882–90887 and Demon Hunter Mardum unique pickers (40077, 38759, 38765, 40222, 38728) have contents. A quest that lists a picker id with no row still gets an empty reply so the quest frame opens. Most advertised pickers still have no world row. Gold, currency, and War Mode bonus fields are still unused.
- **warband-camps** (~90%) — Char-select camps persist on the Battle.net account. Enum sends them. A first visit creates Favorites with the default scene and character slots from WarbandScenePlacement. `CMSG_SETUP_WARBAND_GROUPS` at char select writes create, rename, reorder, scene, and members. Server-owned group ids. Unowned scenes are rejected. Deleted characters leave the camp. Camp setup matches living char-select guids.
- **warband-currency** (~85%) — Account-wide currencies persist on the Battle.net account and overlay onto every character of that account. Login merge keeps the larger of a leftover character row and the account pool, and does not call GetMap before the player has a map. Alt transfer UI, handlers, and log are gated by `FeatureSystem.AccountCurrencyTransfer.Enabled`. Transfer packet layout is still unverified on 12.1. Player dumps do not export the Battle.net account pool, so AccountWide currency does not round-trip through a character dump.
- **arathi-rpe** (~50%) — Catch Up on map 2927. Character-select RPE after a 60-day inactivity stub relocates to the Hammerfall pad. Adventure Guide `CMSG_ENCOUNTER_JOURNAL_START_ARATHI_RPE` casts 1260320 with no inactivity gate (level 20). Pad Thrall/Jaina, keep gnolls, farm openers 90882–90887, pad scenes 3692/3749, pumpkin and catapult spellclicks, and mount credit 239009 on that map. Later Catch Up after 90887, Leave Catch Up, hiding the old quest log, Stuck Ogre 253460, the Stromgarde siege, and the finale choice are still open.
- **archaeology** (~80%) — Survey, private finds, fragment loot, and client project-solve casts. Research* and QuestPOIPoint DB2 load with unsigned point parent IDs, sites persist with hidden find coordinates, and criteria types 3/4/6 plus modifiers 65/66 score. Site-to-branch is server policy (ResearchSite.db2 has no join). Four sites per seeded map (Eastern Kingdoms, Kalimdor, Outland, Northrend, Pandaria) when Archaeology is trained. Fragment crates, Chromie Time coupling, later continents, historical skill gates, and a sniffed find/keystone rate are still open.
- **area-spirit-heal** (~90%) — Waiting to Resurrect and Spirit Heal mana are allowed on every map. Spirit Heal 22012 resurrects ghosts who queued at that area spirit healer. Mardum (map 1481 / GhostZone 7705) has graveyard links and Spirit Healer 65183 at the named graveyards. Vault of the Wardens intro (1468) is not in this job.
- **avatar-overfiend** (~50%) — A spell proc that fires on a successful cast now honours its `spell_proc` family filter, which the core skipped for those events, so such a proc no longer fires on every spell. Avatar of Destruction summons its Overfiend when Soul Fire is cast instead of on every cast. Its other trigger, a chance to summon one when opening a Dimensional Rift, is still open.
- **class-abilities** (~70%) — Imported DK, Evoker and Demon Hunter ability work, shared summon/support/leech/school modifiers, Fel Rush movement, Priest/Rogue repairs and class-proc crash guards. Review fixes cover ally buff ownership, delayed damage, independent Immolation Auras, duplicate procs and zero-Fury form expiry. Current gameplay acceptance, some empowered Evoker healing, inferred talent tuning and other unfinished source-track abilities remain open. [Scope, evidence and owner checks](CLASS_ABILITIES.md).
- **battlepay** (~85%) — Classic Store glue and the free level-80 boost. Character-select packets use the 12.1.0 client layout with size checks, so the boost catalog no longer crashes the client. Fully deleting a character gives back a boost that was assigned to it but not yet applied, and unlinks a used boost from its GUID, so a new character that reuses the GUID is not boosted; login also releases boosts left on characters deleted before this fix. A boost is applied when it is assigned at character select, while the character is offline: ownership, specialization, the boost itself and room for the whole kit are checked first, then the level, specialization, destination, gold, new gear and used boost are saved together. Worn gear, bags and their contents are mailed and the backpack is kept; if the kit does not fit, nothing is used up. A boost assigned before this change is applied the same way when the character list loads, or given back to the account if it cannot be.
- **timerunning** (~35%) — Configurable seasons in worldserver.conf with reload, a persistent season per character, separate seasonal world copies in the same realm, and the party, trade, mail, bank and map restrictions that keep seasonal and ordinary play apart. Pandaria Timerunners start at level 10 on the Timeless Isle in the reconstructed Remix opening: quest NPCs on Blizzard's quest markers, the opening quests and rift object, Blizzard's conversation text, and a startup check that keeps creation closed until that content is complete. Creating a Mists of Pandaria Remix character currently requires a custom memory injection into the client: build 12.1.0.69497 rejects season 1 in two class checks, and a launch helper kept outside this repository (`Play evry.cmd`) changes one byte in each check in the running game's memory, never the file on disk. Time Rifts are invisible and Infinite Ravagers stand still. Most thread spells, the cloak's stat conversion, Archaios' summoning, Eratus' abilities and the retail hand-off to Pandaria are still open, as are full Remix progression, dungeons, raids, rewards and conversion. Legion is scheduling and client presentation only.
- **looted-consumables** (~50%) — Consumables whose effect fires when looted are applied on loot instead of stored when their only other effects are use effects without charges: Remix threads and mementos, Dragon Isles supply herbs, companion experience, Kaja'Cola drinks, mysterious potions, delve boons and blessings. Retail prints a loot line for at least the Dragon Isles herbs; this does not yet. Dragon Isles herbs grant no supplies without spell loot rows, companion experience uses the spell's raw amounts, and boons, most drinks and the blessings need spell scripts.
- **housing** (~65%) — Player housing for the Warband, built from agatho's housing branch, the owner's own 12.0.7 retail captures and the older local housing track. A house belongs to the Battle.net account: one per district (Founder's Point and Razorwind Shores), every character of the account enters and decorates it as owner, and the character who bought it is shown as its owner. Start Tutorial, the free first house at a plot's cornerstone, entering and leaving through the door, Teleport Home, the house exterior built piece by piece as retail sends it, exterior fixture editing and moving the house, Warband decor storage with placing, removing and redeeming owed decor, relinquishing (the house is packed and comes back on the next purchase), eviction and owner deletion (packed, refund paid), charters and guild neighborhoods, and the house level and budget table. Housing requests are only accepted from where a player could make them. Razorwind Shores' town is spawned from the owner's captures; Founder's Point uses 12.0.1 captures by others. A blueprint imported by another character on the same Battle.net account rebuilds the loaded interior. Endeavors cannot advance yet (five criteria modifiers are unidentified), houses stay at level 1 until the General Contractor is captured, only house styles unlocked by default can be used, and the Alliance "My First Home" and spawn phases are still open.
- **pet-battles** (~50%) — Turn-based battles against wild pets with capture, and pet duels between players, built from agatho's pet battle branch with the 12.1 client layouts. The client's battle spot is checked and nobody is teleported, every pick is checked, and leaving the battle by logout, map change or teleport counts as a loss. Tamer battles have no gossip yet, joining the PvP queue is refused, trap upgrades are missing, and no battle has been played in the client yet.
- **echo-isles** (~90%) — Durotar quest Young and Vicious (24626). The Bloodtalon Lasso works only within 15 yards of a wild Swiftclaw and not while already riding; it hides that raptor for a minute, seats the player on a Swiftclaw mount and gives the capture credit. Riding into the raptor pens (area trigger 5675) gives the return credit and removes the mount. A ride left before the pens is cleaned up so the raptor can be lassoed again; retail behaviour for leaving early was not captured, so that part is a guess. The wild Swiftclaw follows a fixed path round the island that avoids obstacles. No in-client test of the quest is recorded.
- **character-save-fixes** (~100%) — Two character-save bugs. Every mail flag is now saved, not only the lowest eight bits, so a mail a GM sent with `.send items` keeps its can't-be-returned flag after it is read and is deleted, not returned, when it expires. A stored aura location that changes or is removed now deletes its old row by character and spell, so it no longer comes back at the next login or makes later saves fail on a duplicate key.
- **skyriding** (~55%) — Skyriding instead of forced Steady Flight, unlocked as on retail. Once any character of a Battle.net account has turned in one of the Skyriding unlock quests (the Dragon Isles "Skyriding" quests or Khaz Algar's "Secure the Beach"), every character of that account at level 10 or more gets the Skyriding trait tree; a new account has no Skyriding. The tree grants Skyriding Basics at 10, Second Wind at 20 and Aerial Halt at 30, as since patch 11.2.7. Lift Off, Skyward Ascent, Surge Forward and Whirling Surge push the mount with the retail movement impulse, Lift Off also casts Launch Boost, and the six shared charges show on the ability buttons. Switch Flight Style, learned with Skyriding, toggles between Skyriding and Steady Flight, and the choice is kept across logins. Logging in already mounted keeps the abilities usable. Taken from the archived Skyriding track and agatho's skyriding branch, checked against 12.1.0 data. Whirling Surge follows the game data: only an account with An Azure Ally (the Highland Drake) gets it. None of the unlock quests, and not the Highland Drake quest, has a quest giver in our world data yet, so for now only a GM can unlock Skyriding. Still open: those quest givers, charge recharge that speeds up with flying speed, the Lightning Rush choice, the Dismount button, and any test in the 12.1.0 client.

## Requirements

Software requirements are in the [TrinityCore wiki](https://trinitycore.info/en/install/requirements) for Windows, Linux, and macOS. They apply here.

## Install

Installation is in the [TrinityCore wiki](https://trinitycore.info/en/home) for Windows, Linux, and macOS. Use this repository instead of TrinityCore/TrinityCore, and build from `game` when you want retail parity with no modules.

## Reporting issues

Work that is unique to this fork: [evryCore issues](https://github.com/evrydayjunglist/evryCore/issues).

Upstream TrinityCore bugs that are not our patches: [TrinityCore issue tracker](https://github.com/TrinityCore/TrinityCore/labels/Branch-master). Read the [issue tracker guide](https://community.trinitycore.org/topic/37-the-trinitycore-issuetracker-and-you/) before filing there.

## Submitting fixes

Fixes meant for TrinityCore: pull request to [TrinityCore](https://github.com/TrinityCore/TrinityCore). In this repo, put upstreamable work on `pr/<name>` from `master`.

Fixes meant for evryCore `game`: pull request or commit on this repository's `game` branch.

SQL-only upstream fixes: TrinityCore ticket, on an existing bug report when one exists.

## Copyright

License: GPL 2.0

Read file [COPYING](COPYING).

## Authors &amp; Contributors

Read file [AUTHORS](AUTHORS).

## Links

* [evryCore](https://github.com/evrydayjunglist/evryCore)
* [TrinityCore](https://github.com/TrinityCore/TrinityCore)
* [TrinityCore website](https://www.trinitycore.org)
* [TrinityCore wiki](https://www.trinitycore.info)
* [TrinityCore forums](https://talk.trinitycore.org/)
* [TrinityCore Discord](https://discord.trinitycore.org/)
