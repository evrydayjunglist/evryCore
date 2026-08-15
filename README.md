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
- **chromie-time** (~80%) — Chromie Time select, persist, gossip, and leave. Burning Crusade breadcrumb after select; remaining expansions mapped in `chromie_time_expansion_quest`. Outdoor scaling uses ConditionalContentTuning. Start and re-enter from the present lock at level 68; already in a campaign may change timelines until the ContentTuning end level. Dorn refuse vs FAQ. Orgrimmar hourglass. Dark Portal Eastern Kingdoms ↔ Outland.

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
