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

When a job lands, add a short entry on the parent that owns it, with a completion percent. 100% means the listed feature is in and only bug fixes remain. Copy new `game` entries here after `game` is merged into `evry`.

### From `game`

- **combat-stats** (~75%) — Player base stamina, health, and STR/AGI/INT from ExpectedStat.db2, including the Midnight health and primary-stat squish at current-expansion levels. `GiveLevel` uses the same overrides as login. Creature MaxHealth uses floor so create health matches retail. `DealDamageMods` scales player damage into create-level creature HP and creature damage onto the level-matched DPS curve. `ScalingPlayerLevelDelta` is refreshed after NextLevelXP is known.
- **chromie-time** (~80%) — Chromie Time select, persist, gossip, and leave. Burning Crusade breadcrumb after select; remaining expansions mapped in `chromie_time_expansion_quest`. Outdoor scaling uses ConditionalContentTuning. Start and re-enter from the present lock at level 68; already in a campaign may change timelines until the ContentTuning end level. Dorn refuse vs FAQ. Orgrimmar hourglass. Dark Portal Eastern Kingdoms ↔ Outland.

### On `evry` only

- **modules** (~95%) — Drop-in folders at `modules/<name>/` (code, `conf/`, `data/sql/`). CMake `MODULES=static` (default) or `none`. Module `.conf.dist` copies to a runtime `modules/` directory next to the server. `mod-example` is the template.
- **playerbots** (~85%) — In-process bots as real `Player` rows and an empty-socket `WorldSession`, brought in through `World::AddSession`. They only act by queueing real client packets. Immediate world first: talk within 40 yards (same NPC turns in before accepting), fight what is hitting them, loot a corpse at their feet (coin and every item a player can click, then close), use a quest object in range, and when idle a quest mob they can see (150 yards). Then walk to the nearest map yellow (incomplete objective or finished-quest `?`). They commit to a walk and do not recast the map every tick. They click talk, use, loot, and vendor from their feet once a player could; they do not finish a stand-beside pin into a lip they can already click over. A 40-yard `?`/`!` still stops a long walk; it does not pull them off a unit or object they are already in range to use, and a walk to talk is not stolen by another nearby immediate. Each walk heartbeat plants on the ground. Walking up steeper than 35° stops. If a local look around her feet finds legal ground beside a small steep patch, she walks that and continues to the same destination. If there is no legal ring, she picks other work once. A short step down (curb, stair, or house slab) is allowed; a longer drop is a cliff. A failed walk does not start another yellow whose first grounded step is the same illegal drop. Talk and turn-in lookup honor unreachable guids. If they are already in melee they keep swinging from their feet after a close-in step is refused; they do not stop auto-attack or mark that attacker unreachable. In a live fight they press damaging spells they actually know from the spellbook (`CMSG_CAST_SPELL`) on GCD at that spell’s range, and still auto-attack in melee. A bolt does not require punch range. Heals, buffs, stances, interrupts, and other utility wait. A walk stops for a 40-yard `?`/`!`, for something hitting them, or for loot at their feet. Missing spawns do not freeze the bot. Incomplete gameobject, kill, and item work still uses that objective’s quest POI when the map is why they walk. Item objectives that come from a gameobject (`gameobject_questitem`) use that spawned object: stand beside it, `CMSG_GAME_OBJ_USE`, then take chest loot and `CMSG_LOOT_RELEASE` on the GO. Creature drops stay melee then corpse loot. An incomplete monster objective on a quest whose StartItem they still hold is not a kill: walk that objective’s marker, stand beside a spawned creature the item’s on-use spell would accept (the same database spell conditions a client uses), and queue `CMSG_USE_ITEM` on that unit. They do not keep using it on a unit that no longer meets those conditions. Immediate use is interact range only, not a 150-yard idle fight. On death they wait, queue `CMSG_REPOP_REQUEST`, ACK the graveyard teleport (`CMSG_MOVE_TELEPORT_ACK`, or empty `CMSG_WORLD_PORT_RESPONSE` on a worldport), ghost-walk to the corpse, and queue `CMSG_RECLAIM_CORPSE` within 39 yards after the server delay (stand aside if the circle is hot). Teleport ACK is ordinary client housekeeping, not a death-only path. Spirit healer only if a player would: walk fails or sticks, the body is camped, she already has sickness, or she has been a ghost a long time. After she is alive she sits until health is full, unless combat stands her up. When bags have 0 or 1 free slot and grey junk to sell, or an equipped piece is broken or under 20% durability, they walk to the nearest vendor on this map (prefer one that can repair when repair is why they came), stand beside them, open the shop, sell junk, and repair all if that NPC can and they need it, paying with their own gold. Combat, loot at their feet, and a 40-yard talk they can finish still win; a long map yellow may stop for the vendor. Runtime keys live in `modules/mod-playerbots.conf` next to the server. `Playerbots.Races` and `Playerbots.Classes` are comma-separated English names and only apply when a new bot character is created. Empty means no filter.

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
