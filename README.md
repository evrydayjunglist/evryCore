# evryCore (`evry`)

What we boot. This branch is `game` (retail parity, no modules) plus drop-in `modules/` and playerbots.

`master` in this repo fast-forwards from [TrinityCore](https://github.com/TrinityCore/TrinityCore) `master`. Permanent merge is one-way: `game` → `evry`. Never the other way. After work lands on `game`, merge `game` into `evry` to boot it.

* [What this branch is](#what-this-branch-is)
* [Jobs](#jobs)
* [Requirements](#requirements)
* [Install](#install)
* [Reporting issues](#reporting-issues)
* [Submitting fixes](#submitting-fixes)
* [Copyright](#copyright)
* [Authors &amp; Contributors](#authors--contributors)
* [Links](#links)

## What this branch is

TrinityCore is an MMORPG framework in C++, derived from MaNGOS, with a long history of optimizing the codebase and in-game mechanics. evryCore stays on that `master` line (current retail client). `game` holds retail-parity core. `evry` is that tree plus modules.

Community involvement on upstream TrinityCore is unchanged: ideas and code for TrinityCore itself go to [TrinityCore.org](https://www.trinitycore.org) and [TrinityCore pull requests](https://github.com/TrinityCore/TrinityCore/pulls).

This repository: [github.com/evrydayjunglist/evryCore](https://github.com/evrydayjunglist/evryCore).

## Jobs

When a job lands, add a short entry on the parent that owns it. Copy new `game` entries here after `game` is merged into `evry`.

### From `game`

- **combat-stats** — Player base stamina, health, and STR/AGI/INT from ExpectedStat.db2, including the Midnight health and primary-stat squish at current-expansion levels. `GiveLevel` uses the same overrides as login. Creature MaxHealth uses floor so create health matches retail. `DealDamageMods` scales player damage into create-level creature HP and creature damage onto the level-matched DPS curve. `ScalingPlayerLevelDelta` is refreshed after NextLevelXP is known.
- **chromie-time** — Chromie Time select, persist, gossip, and leave. Burning Crusade breadcrumb after select; remaining expansions mapped in `chromie_time_expansion_quest`. Outdoor scaling uses ConditionalContentTuning. Start and re-enter from the present lock at level 68; already in a campaign may change timelines until the ContentTuning end level. Dorn refuse vs FAQ. Orgrimmar hourglass. Dark Portal Eastern Kingdoms ↔ Outland.

### On `evry` only

- **modules** — Drop-in folders at `modules/<name>/` (code, `conf/`, `data/sql/`). CMake `MODULES=static` (default) or `none`. Module `.conf.dist` copies to a runtime `modules/` directory next to the server. `mod-example` is the template.
- **playerbots** — In-process bots as real `Player` rows and an empty-socket `WorldSession`, brought in through `World::AddSession`. They only act by queueing real client packets. They walk the navmesh to stand beside questgivers, then nearby turn-in, incomplete kill objectives (select and melee swing in melee range), then nearby accept. Runtime keys live in `modules/mod-playerbots.conf` next to the server.

## Requirements

Software requirements are in the [TrinityCore wiki](https://trinitycore.info/en/install/requirements) for Windows, Linux, and macOS. They apply here.

## Install

Installation is in the [TrinityCore wiki](https://trinitycore.info/en/home) for Windows, Linux, and macOS. Use this repository instead of TrinityCore/TrinityCore, and build from `evry` to boot with modules and playerbots.

## Reporting issues

Work that is unique to this fork: [evryCore issues](https://github.com/evrydayjunglist/evryCore/issues).

Upstream TrinityCore bugs that are not our patches: [TrinityCore issue tracker](https://github.com/TrinityCore/TrinityCore/labels/Branch-master). Read the [issue tracker guide](https://community.trinitycore.org/topic/37-the-trinitycore-issuetracker-and-you/) before filing there.

## Submitting fixes

Fixes meant for TrinityCore: pull request to [TrinityCore](https://github.com/TrinityCore/TrinityCore). In this repo, put upstreamable work on `pr/<name>` from `master`.

Fixes meant for evryCore: `game` work on `game`; modules and playerbots on `evry`. Do not merge `evry` into `game`.

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
