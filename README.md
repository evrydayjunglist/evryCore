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

- **combat-stats** (~75%) — Player base stamina, health, and STR/AGI/INT from ExpectedStat.db2, including the Midnight health and primary-stat squish at current-expansion levels. `GiveLevel` uses the same overrides as login. Creature MaxHealth uses floor so create health matches retail. `DealDamageMods` scales player damage into create-level creature HP and creature damage onto the level-matched DPS curve. `ScalingPlayerLevelDelta` is refreshed after NextLevelXP is known.
- **chromie-time** (~90%) — Chromie Time select, persist, gossip, and leave. Campaign breadcrumbs after select in `chromie_time_expansion_quest`. Outdoor scaling uses ConditionalContentTuning. Dungeon Finder hides and locks dungeons whose expansion bit is outside `ChromieTimeExpansionMask`. Select requires the Chromie Time interaction. Set and clear refresh phase so terrain swaps update. Silithus Wound shows unless the player is in a pre-BfA campaign. Chromie says the BroadcastText line on select; Shadowlands has no id yet. Start and re-enter from the present lock at level 68; already in a campaign may change timelines until the ContentTuning end level. Dorn refuse vs FAQ. Orgrimmar hourglass. Dark Portal Eastern Kingdoms ↔ Outland. Character wipe deletes `character_chromie_time` so a reused GUID does not inherit the old expansion. Player dumps still omit that table. Other terrain swaps, raid walk-in abort, and the level-80 return quest are still open.
- **treasure-picker** (~80%) — `CMSG_QUERY_TREASURE_PICKER` replies from world `treasure_picker` rows. Offer and grant filter by AllowableClass, weapon/armor skill when AllowableClass is -1, AllowableRace, and faction item flags. Turn-in grants the first eligible row, or the chosen item on a choice picker (`CMSG_QUEST_GIVER_CHOOSE_REWARD` accepts that picker item). Arathi RPE 90882–90887 and Demon Hunter Mardum unique pickers (40077, 38759, 38765, 40222, 38728) have contents. A quest that lists a picker id with no row still gets an empty reply so the quest frame opens. Most advertised pickers still have no world row. Gold, currency, and War Mode bonus fields are still unused.
- **warband-camps** (~90%) — Char-select camps persist on the Battle.net account. Enum sends them. A first visit creates Favorites with the default scene and character slots from WarbandScenePlacement. `CMSG_SETUP_WARBAND_GROUPS` at char select writes create, rename, reorder, scene, and members. Server-owned group ids. Unowned scenes are rejected. Deleted characters leave the camp.
- **warband-currency** (~85%) — Account-wide currencies persist on the Battle.net account and overlay onto every character of that account. Alt transfer UI, handlers, and log are gated by `FeatureSystem.AccountCurrencyTransfer.Enabled`. Transfer packet layout is still unverified on 12.1. Player dumps do not export the Battle.net account pool, so AccountWide currency does not round-trip through a character dump.

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
