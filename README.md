# evryCore (`evry`)

What we boot. This branch is `game` (retail parity, no modules) plus drop-in `modules/` and playerbots.

`master` in this repo fast-forwards from [TrinityCore](https://github.com/TrinityCore/TrinityCore) `master`. Permanent merge is one-way: `game` → `evry`. Never the other way. After work lands on `game`, merge `game` into `evry` to boot it.

* [What this branch is](#what-this-branch-is)
* [Jobs](#jobs)
* [Playerbots architecture](PLAYERBOTS_ARCHITECTURE.md)
* [Commander Mode plan](COMMANDER_MODE.md)
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

The selected playerbots product and technical direction is in
[Playerbots architecture](PLAYERBOTS_ARCHITECTURE.md). The direct-switch and RTS
implementation tracker is in [Commander Mode](COMMANDER_MODE.md).

## Jobs

When a job lands, add a short entry on the parent that owns it, with a completion percent. 100% means the listed feature is in and only bug fixes remain. Copy new `game` entries here after `game` is merged into `evry`.

### From `game`

- **combat-stats** (~75%) — Player base stamina, health, and STR/AGI/INT from ExpectedStat.db2, including the Midnight health and primary-stat squish at current-expansion levels. `GiveLevel` uses the same overrides as login. Creature MaxHealth uses floor so create health matches retail. `DealDamageMods` scales player damage into create-level creature HP and creature damage onto the level-matched DPS curve. `ScalingPlayerLevelDelta` is refreshed after NextLevelXP is known. Hunter, Shaman, Warlock, Monk, Druid, Death Knight, and Evoker still use an average secondary split. The Midnight squish is a level-80 sniff constant.
- **chromie-time** (~90%) — Chromie Time select, persist, gossip, and leave. Campaign breadcrumbs after select in `chromie_time_expansion_quest`. Outdoor scaling uses ConditionalContentTuning. Dungeon Finder hides and locks dungeons whose expansion bit is outside `ChromieTimeExpansionMask`. Select requires the Chromie Time interaction. Set and clear refresh phase so terrain swaps update. Silithus Wound shows unless the player is in a pre-BfA campaign. Chromie says the BroadcastText line on select; Shadowlands has no id yet. Start and re-enter from the present lock at level 68; already in a campaign may change timelines until the ContentTuning end level. Dorn refuse vs FAQ. Orgrimmar hourglass. Dark Portal Eastern Kingdoms ↔ Outland. Character wipe deletes `character_chromie_time` so a reused GUID does not inherit the old expansion. Player dumps still omit that table. Other terrain swaps, raid walk-in abort, and the level-80 return quest are still open.
- **treasure-picker** (~80%) — `CMSG_QUERY_TREASURE_PICKER` replies from world `treasure_picker` rows. Offer and grant filter by AllowableClass, weapon/armor skill when AllowableClass is -1, AllowableRace, and faction item flags. Turn-in grants the first eligible row, or the chosen item on a choice picker (`CMSG_QUEST_GIVER_CHOOSE_REWARD` accepts that picker item). Arathi RPE 90882–90887 and Demon Hunter Mardum unique pickers (40077, 38759, 38765, 40222, 38728) have contents. A quest that lists a picker id with no row still gets an empty reply so the quest frame opens. Most advertised pickers still have no world row. Gold, currency, and War Mode bonus fields are still unused.
- **warband-camps** (~90%) — Char-select camps persist on the Battle.net account. Enum sends them. A first visit creates Favorites with the default scene and character slots from WarbandScenePlacement. `CMSG_SETUP_WARBAND_GROUPS` at char select writes create, rename, reorder, scene, and members. Server-owned group ids. Unowned scenes are rejected. Deleted characters leave the camp. Camp setup matches living char-select guids.
- **warband-currency** (~85%) — Account-wide currencies persist on the Battle.net account and overlay onto every character of that account. Login merge keeps the larger of a leftover character row and the account pool, and does not call GetMap before the player has a map. Alt transfer UI, handlers, and log are gated by `FeatureSystem.AccountCurrencyTransfer.Enabled`. Transfer packet layout is still unverified on 12.1. Player dumps do not export the Battle.net account pool, so AccountWide currency does not round-trip through a character dump.
- **arathi-rpe** (~50%) — Catch Up on map 2927. Character-select RPE after a 60-day inactivity stub relocates to the Hammerfall pad. Adventure Guide `CMSG_ENCOUNTER_JOURNAL_START_ARATHI_RPE` casts 1260320 with no inactivity gate (level 20). Pad Thrall/Jaina, keep gnolls, farm openers 90882–90887, pad scenes 3692/3749, pumpkin and catapult spellclicks, and mount credit 239009 on that map. Later Catch Up after 90887, Leave Catch Up, hiding the old quest log, Stuck Ogre 253460, the Stromgarde siege, and the finale choice are still open.
- **archaeology** (~80%) — Survey, private finds, fragment loot, and client project-solve casts. Research* and QuestPOIPoint DB2 load with unsigned point parent IDs, sites persist with hidden find coordinates, and criteria types 3/4/6 plus modifiers 65/66 score. Site-to-branch is server policy (ResearchSite.db2 has no join). Four sites per seeded map (Eastern Kingdoms, Kalimdor, Outland, Northrend, Pandaria) when Archaeology is trained. Fragment crates, Chromie Time coupling, later continents, historical skill gates, and a sniffed find/keystone rate are still open.
- **area-spirit-heal** (~90%) — Waiting to Resurrect and Spirit Heal mana are allowed on every map. Spirit Heal 22012 resurrects ghosts who queued at that area spirit healer. Mardum (map 1481 / GhostZone 7705) has graveyard links and Spirit Healer 65183 at the named graveyards. Vault of the Wardens intro (1468) is not in this job.
- **avatar-overfiend** (~50%) — A spell proc that fires on a successful cast now honours its `spell_proc` family filter, which the core skipped for those events, so such a proc no longer fires on every spell. Avatar of Destruction summons its Overfiend when Soul Fire is cast instead of on every cast. Its other trigger, a chance to summon one when opening a Dimensional Rift, is still open.
- **battlepay** (~85%) — Classic Store glue and the free level-80 boost. Character-select packets use the 12.1.0 client layout with size checks, so the boost catalog no longer crashes the client. Fully deleting a character gives back a boost that was assigned to it but not yet applied, and unlinks a used boost from its GUID, so a new character that reuses the GUID is not boosted; login also releases boosts left on characters deleted before this fix. A boost is applied when it is assigned at character select, while the character is offline: ownership, specialization, the boost itself and room for the whole kit are checked first, then the level, specialization, destination, gold, new gear and used boost are saved together. Worn gear, bags and their contents are mailed and the backpack is kept; if the kit does not fit, nothing is used up. A boost assigned before this change is applied the same way when the character list loads, or given back to the account if it cannot be.
- **timerunning** (~35%) — Configurable seasons in worldserver.conf with reload, a persistent season per character, separate seasonal world copies in the same realm, and the party, trade, mail, bank and map restrictions that keep seasonal and ordinary play apart. Pandaria Timerunners start at level 10 on the Timeless Isle in the reconstructed Remix opening: quest NPCs on Blizzard's quest markers, the opening quests and rift object, Blizzard's conversation text, and a startup check that keeps creation closed until that content is complete. Creating a Mists of Pandaria Remix character currently requires a custom memory injection into the client: build 12.1.0.69497 rejects season 1 in two class checks, and a launch helper kept outside this repository (`Play evry.cmd`) changes one byte in each check in the running game's memory, never the file on disk. Time Rifts are invisible and Infinite Ravagers stand still. Most thread spells, the cloak's stat conversion, Archaios' summoning, Eratus' abilities and the retail hand-off to Pandaria are still open, as are full Remix progression, dungeons, raids, rewards and conversion. Legion is scheduling and client presentation only.
- **looted-consumables** (~50%) — Consumables whose effect fires when looted are applied on loot instead of stored when their only other effects are use effects without charges: Remix threads and mementos, Dragon Isles supply herbs, companion experience, Kaja'Cola drinks, mysterious potions, delve boons and blessings. Retail prints a loot line for at least the Dragon Isles herbs; this does not yet. Dragon Isles herbs grant no supplies without spell loot rows, companion experience uses the spell's raw amounts, and boons, most drinks and the blessings need spell scripts.

### On `evry` only

- **catalogshop** (~60%) — `mod-catalogshop` serves the in-game Shop locally from captured retail catalog responses. The stock client browses it over HTTPS on localhost (IPv4 and IPv6); it stays off until the module is enabled, so the classic Store runs without it. Free Buy completes a checkout for the level-80 boost and passes it to BattlePay through a signal file; other products are refused. Buy needs `us.checkout.battle.net` pinned to 127.0.0.1, port 443 needs an elevated worldserver, and the localhost certificate is made and trusted by hand. On `evry` a level-80 boost is applied to the offline character before success is reported: ownership, offline state, specialization, entitlement and bag space are checked first, then the level, specialization, destination, gold, new gear and used entitlement are saved together and replaced gear is mailed. Timerunners are refused.
- **modules** (~95%) — Drop-in folders at `modules/<name>/` (code, `conf/`, `data/sql/`). CMake `MODULES=static` (default) or `none`. Module `.conf.dist` copies to a runtime `modules/` directory next to the server. `mod-example` is the template.
- **playerbots** (~60%) — Bots are real characters, logged in through an in-process session with no network connection, and they only act by queueing the packets a real client would send. Today they quest on their own: talk to quest givers they can see within 40 yards, fight what attacks them, loot corpses, use quest objects and quest items, and walk to the nearest objective marker, finished-quest `?`, or takeable `!` in their zone. In a fight they auto-attack and press the damaging spells they know, one press per global cooldown. They walk the navmesh but step on the real ground: no climbing steeper than 35°, no long drops, no walking through walls or objects, and they walk around a small lip or make one checked normal jump over it. On death they release, run back and take their corpse, and use the spirit healer only when a player would. When bags are full of junk or gear is nearly broken they sell and repair at a vendor with their own gold. `playerbots.exe` builds with the module and, with `Playerbots.LoginMode = Coordinator`, decides when bots are online; it does not make any gameplay decisions yet. Settings are in `modules/mod-playerbots.conf` next to the server (`Playerbots.Enable`, `Count`, `Races`, `Classes`). Still open: a bot can loop in a cave when the navmesh only finds a partial route; heals, buffs, pets, class rotations, and backing out of minimum range; and the external brain for long-term activity, groups, guilds, professions, and economy (see [Playerbots architecture](PLAYERBOTS_ARCHITECTURE.md)).

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
