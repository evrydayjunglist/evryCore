# Timerunning: seasons and the Pandaria introduction

This job adds configurable Timerunning seasons to `game`: a persistent season identity per character, separate seasonal copies of the world inside the same realm, and the restrictions that keep seasonal and ordinary play apart. The reconstructed Pandaria Remix opening on the Timeless Isle is parked on the branch `job/timerunning-pandaria-opening`, based on `game`, and is not merged.

Creation is not yet reachable from the stock client. Build 12.1.0.69497 rejects season 1 (Pandaria) in its native class checks while accepting season 2 (Legion). See the client compatibility section. On `game` nothing marks Pandaria content ready, so with an active Pandaria schedule the client shows the season banner and countdown with the Create button disabled, and seasonal login stays closed. Dungeons, raids, full Remix progression, rewards and conversion are not implemented. Legion is recognised for scheduling and client presentation only.

## Configuration

Edit the runtime `worldserver.conf` next to the server (a build refreshes only `worldserver.conf.dist`):

~~~ini
Timerunning.Season = "Pandaria"
Timerunning.StartTime = "2026-09-01 00:00:00"
Timerunning.DurationDays = 90
~~~

This example ends on 30 November 2026 at 00:00 UTC. `Season` accepts `None`, `Pandaria` or `Legion`, case-insensitive. One season is scheduled at a time; there is no automatic rotation. The start is a fixed UTC date in `YYYY-MM-DD HH:MM:SS` form, year 1970 or later; the duration is a whole number of days from 1 to 24855, bounded by the client's 32-bit countdown. The season is active from the start up to, but not including, the end. Restarts and daylight-saving changes do not move the countdown. An invalid reload keeps the previous schedule and logs why; invalid initial settings leave no season scheduled. `None` ignores the other two keys.

Enter `reload config` in the worldserver console, or `.reload config` in a GM character's chat, to apply a change. Sessions check once per second and resend the feature packets when the active season or its end changes.

Expiry or `None` saves and disconnects online Timerunners and refuses their next login. Their identity, items and progress stay intact; a valid Pandaria schedule lets them back in. Keep Pandaria active while testing, because conversion into an ordinary character is not implemented. Ordinary characters are never affected by these checks.

## Implemented on game

- Pandaria Timerunners start at level 10 on the Timeless Isle with no gold and the class kit from the installed Pandaria `CharacterLoadout` rows (purpose 16, item context 75). The level cap is 70, or the server's lower configured cap. Creation requires an Alliance or Horde race; neutral-race requests, the tutorial start and character templates are refused for Timerunners.
- Each season runs in its own `Map` object. Ordinary worlds keep instance 0, or instances 0 to 2 on maps split by faction; Pandaria uses 3, or 3 to 5; Legion would use 6 to 8. Creature health, combat, loot, corpses and respawn timers belong to the map object, so nothing is shared with ordinary players through visibility tricks.
- The `spawn_group_timerunning` table declares which seasons a spawn group belongs to without changing upstream spawn-group columns. Mask bits 1, 2 and 4 mean ordinary, Pandaria and Legion. A group with no row is ordinary-only. An invalid mask disables the group. The mask is enforced inside creature, gameobject and area-trigger loading, which every database spawn path uses: grid loading, respawns, explicit spawn-group spawning, pools, game events and GM-added spawns. Scripted summons are not database spawns and are not masked.

## Parked: the Pandaria opening

The branch `job/timerunning-pandaria-opening` holds the reconstructed Timeless Isle introduction, its world and hotfix updates, the startup validation that marks Pandaria content ready, and the installed-terrain test. It stays off `game` until a client can create season 1 characters and the reconstructed content is accepted for this branch. What it contains:

- The default group and the existing Pandaria groups admitted to the Pandaria copy, and the new group 1286 holding the opening spawns, Pandaria-only. The admission query covers creature and gameobject groups; an area-trigger group on map 870 would keep the ordinary-only default.
- Sixteen opening creatures with GUIDs 11801064 to 11801079 and the Unstable Rift object with GUID 11801080. Eternus, Moratari, Horos, Momentus, Archaios and Eratus stand on Blizzard's own quest markers (`quest_poi_points`) for the quests they give, end or are objectives of. Erus, the Infinite Ravagers, the three Time Rifts and the rift object use map coordinates recorded on Wowhead and in the RestedXP guide on the reference shelf, converted with the Timeless Isle map bounds. Heights were taken from the installed terrain. Quest, creature, item, spell, currency and loadout identities are Blizzard data; facings, respawn times, the number of Ravagers and the arrival point in front of Eternus are reconstructed.
- The opening quests: the unstable rift, Archaios, Moratari, the Infinite Ravagers, the Bronze purchase from Horos, Momentus' cloak craft, the cloak-equip and thread objectives, the optional Chronostabilizer channel, Eratus, and Eternus' history dialogue. Normal quest, vendor, combat, loot and item-use paths perform the actions. One three-second Chronostabilizer pulse closes a nearby visible rift; cancelling before it gives no credit.
- The optional rift objective is credited the way Blizzard's data describes: investigating gameobject 423343 casts its own spell 439809, which gives the credit. The object's interaction condition (PlayerCondition 119615) is not in the client data, so `conditions` rows stand in for it: the rift can be investigated while It's About Time is in progress and until it has been investigated. Each player sees either that object or the untargetable rift creature 217666 at the same spot, never both.
- Moratari gives Knot My Problem, Goodbyes Are Hard When You Live Forever and Recalling the War, and Knot My Problem is offered together with Weave It To Me. Erus is a vendor only.
- Conversation windows for Eternus, Moratari, Horos, Erus and Momentus show Blizzard's own text records from the Remix. Which NPC shows which record, and the quest points where Moratari's text changes, are reconstructed from what each text says. Momentus' forge option carries Blizzard's gossip option id. The spoken lines for arriving, accepting It's About Time, Seeking Expert Advice, Weave It To Me or Knot My Problem, and fighting Archaios have no text record in the installed data; their wording is quoted from Wowhead and Warcraft Wiki.
- The Time Rift's display 118479 had no `creature_model_info` row, so the rifts could not be created. The row's size values are reconstructed.
- Nothing is cast at login. The earlier login cast of spell 436658 only applied two action-bar overrides for the Jade Forest abilities, which do not belong to the Timeless Isle.
- The final quest starts the existing Horde arrival scene, Into the Mists, or the Alliance one, The Mission. The existing scene scripts grant arrival and discovery credit. Eternus replaces the Alliance capital flight choice, and talking to Eternus again allows a retry after an interruption.
- The primary-stat thread uses the existing character currency and spell. Cloak effects read earned thread currencies only while the cloak is equipped. The currency-to-amount mapping is not verified against retail curves or a packet capture.
- A hotfix update for bag 216653. The starter kits need four of them; `Item.db2` has the basic record, but the installed `ItemSparse.db2` lacks the detailed row, so the update reconstructs a common, soulbound, 36-slot bag with the same identity. It is not a captured retail row. It carries `VerifiedBuild` zero, loads after the client-file data, and is pushed to every connecting client through the hotfix registration. Replace it when a verified row is available.
- Startup validation of the starting kits, quests, items, loot, criteria, currency, opening spawns, the rift object and its conditions, the Time Rift model record and spell bindings. Each missing prerequisite is logged and keeps creation closed; only a complete set marks Pandaria ready.
- A core fix found by the first playthrough: `Player::StoreNewItem` skips the appearance collection update while a character is still being created, because the session has no player attached yet and the update crashed the server. The Rogue, Death Knight, Monk and Demon Hunter starter kits hold spare one-handed weapons, which go into the bags when they cannot be equipped; those were the items that reached the crash. The collection picks them up at first login, as it already does for equipped items.
- Two core loot fixes found by the second playthrough. `Loot::hasItemFor` now counts an unlooted item that follows loot rules but carries database conditions. Its comment already said it covered conditional items, but only quest and free-for-all items were checked, so a corpse whose only loot was such an item never became lootable; the Thread of Power is one. And a consumable that takes effect when looted (`ITEM_SPELLTRIGGER_ON_LOOTED_FORCED`) and also carries a use effect without charges is no longer stored: taking it from a loot window, automatic storing and master loot all cast its looted spell on the looter instead. In the installed client data that is 55 items: the 36 Mists of Pandaria Remix threads, the 16 Legion Remix epoch mementos, Bronze Cluster, Infinite Knowledge and Fragmented Memento of Epoch Challenges. Before, such an item went into the bags, where its use effect could be cast again and again. Blizzard Watch describes threads as consumed automatically when looted; the tooltips say a memento is absorbed automatically and Bronze Cluster grants Bronze when looted. The server loads 339 items with this trigger, and the other 284 are stored as before. Some of those are opened or used from the bags, such as Delver's Starter Kit and the Mists "A Little Patience" supplies. Ninety-six are consumables with no use effect, so a stored copy cannot be used again. Retail consumes at least some of those when looted too (Potion of Mysterious Celerity says it is consumed immediately, and players describe Dragon Isles supply items such as Gorloc Crystals vanishing into supplies), but that is outside this work.
- A core change that belongs with the opening: `Unit::Kill` now gives kill credit before it builds corpse loot. Archaios' spool objective is sequenced after his kill objective, so while the loot was built first the spool was never allowed and the corpse could not be looted. Nothing else in the kill reward reads the loot. In the installed world database this is the only quest whose looted item is sequenced right after killing the same creature.

## Protection of ordinary play

- The saved season belongs to the character and is independent of configuration. New and existing ordinary characters keep season zero. The season is written once by the character-creation transaction and never rewritten by a routine save. All four enumeration queries, offline name lookups, the character cache, the guild roster and player-dump imports keep the value. Soft deletion and restoration keep it; permanent deletion removes it with the character. Unknown and negative values are kept for diagnosis and refused at login.
- Creation checks the schedule when the request arrives, again after the asynchronous account checks, and once more inside `Player::Create`. Login checks the saved season right after account ownership and before any character state loads, with a second check inside `Player::LoadFromDB`. Both require the season to be marked ready, which only the parked opening does.
- A Pandaria Timerunner may enter only world map 870. Map creation and `Map::AddPlayerToMap` refuse other continents, dungeons, raids, battlegrounds, arenas and garrisons. A cross-season GM transfer such as `.appear` or `.summon` is refused when the player is added to the target map, which happens after the GM has already left the current map; the GM is then returned to homebind. Multi-map transports are refused inside the season copy. Outdoor PvP and battlefield managers are not created for season copies. Invalid or out-of-event homebind recovery returns to the opening's start point and keeps the season.
- Party invitations, acceptance, membership, trade and mail require matching known seasons; a missing identity refuses the operation. Auction-house access, guild banking and guild-paid repairs are refused for Timerunners. Personal banking stays available; account banking, account-currency transfers, the dungeon finder and battlegrounds are refused. Boost assignment, deferred login boosts, race or faction services and Arathi catch-up entry cannot move a Timerunner into ordinary progression. The separate boost implementation on `evry` needs the same eligibility guard.
- Not fenced today: the Black Market auction house, guild membership and calendar invitations. These are current picks, not oversights.
- Account collection unlocks keep the core's existing behavior. Nothing here claims complete Remix reward eligibility or protection against manual edits to character rows. Never clear `timerunningSeasonId` by hand as a substitute for conversion.

## Core seams and current picks

These changes reach into `src/server/game` and are recorded here as the picks in force. Each is open to discussion before the job merges.

1. `Map` carries a season id from its constructor, and `MapManager::CreateWorldMap` takes it. The world instance id is the season times three plus the team index, so ordinary ids 0 to 2 stay stable and Pandaria takes 3 to 5. `CreateMap` and `FindInstanceIdForPlayer` use the same helper.
2. `Map::CannotEnter` has a base implementation that compares the player's season with the map's. The instance and battleground overrides call it, and their `AddPlayerToMap` overrides gate on it before any side effect.
3. `CharacterCache::AddCharacterCacheEntry` takes the season with a default, so unchanged callers still compile, and the cache keeps it for offline checks such as party membership and boost targets.
4. A new world table, `spawn_group_timerunning`, holds season masks instead of a new column on `spawn_group_template`.
5. A process-wide `Timerunning::Manager` is loaded from `World::LoadConfigSettings`, read by the handlers on the world thread, and polled once per second per session from `WorldSession::Update` to resend feature packets and to disconnect Timerunners whose season closed.
6. The feature packets carry the configured season and countdown even while `TimerunningEnabled` is false, which happens when Legion is configured or Pandaria's content is not marked ready. The owner's client test on 14 September 2026 showed the banner and countdown with the Create button disabled in that state.
7. There is no conversion. Expiry or `None` locks seasonal login and preserves the data. `CMSG_CONVERT_TIMERUNNING_CHARACTER` stays unhandled until a real conversion exists; retail conversion migrates items, currencies, quests, bags and rewards rather than clearing a flag.
8. Legion is accepted by the schedule for presentation only. `IsPlayableSeason` admits Pandaria alone, and there is no Legion readiness setter.
9. Pandaria play is fenced to map 870 for this stage.
10. In `Player::Create` the faction and team are set before the map is chosen, so a split-by-faction map would pick the right seasonal copy; the upstream call later in the function still runs.
11. The opening's placements, dialogue and timing and the bag 216653 row are reconstructed rather than captured. The owner approved that reconstruction for the opening on 14 September 2026; it is parked rather than merged while the client cannot create Pandaria characters.

## Client compatibility

The stock client 12.1.0.69497 enforces two native class checks during character creation. Both accept no season, season 0, or season 2 with any class except Evoker, and both reject every class for season 1. That is why a Pandaria creation attempt shows a blank race and class screen: the default selection finds no eligible class and the interface code fails on the missing race data.

Static analysis of the matching unpacked client established that those checks run only under four glue interface functions: `GetAvailableClasses`, `GetClassDataByID`, `ResetCharCustomize` and `SetSelectedRace`. The create request copies the chosen season unchanged, and nothing on the submit path consults the checks. The candidate route is therefore an interface adapter that presents season 0 to those four calls and restores season 1 before `CreateCharacter`, delivered as replacement interface files. It has not been tested. The fallback is Legion Remix as the first playable season, which the client accepts natively but for which no server content exists yet.

Keep Pandaria's real id everywhere. Do not advertise season 2 while storing season 1. Do not patch `Wow.exe` on disk from the unpacked dump, and do not attach a debugger to the running client to change it in memory; that route was tried and left the client unable to shut down normally.

## Database updates

On `game`:

- `sql/updates/characters/master/2026_09_13_00_characters.sql` adds `characters.timerunningSeasonId`.
- `sql/updates/world/master/2026_09_13_01_world.sql` creates `spawn_group_timerunning`.

Parked with the opening:

- `sql/updates/world/master/2026_09_13_02_world.sql` admits the Pandaria groups and adds the opening spawns, quest relations, vendor and loot rows.
- `sql/updates/world/master/2026_09_13_03_world.sql` moves the spawns onto the quest markers, adds the rift object and its conditions, corrects the quest givers and Knot My Problem's prerequisite, adds the Time Rift model record, and adds the conversation windows and spoken lines. It keeps the job's 13 September date because upstream already has later dates; a new date could collide with an upstream file of the same name.
- `sql/updates/hotfixes/master/2026_09_13_00_hotfixes.sql` adds the reconstructed `item_sparse` row for bag 216653.

These files were renamed on 15 September 2026 without changing their content. The updater recognises an applied update by its hash and renames its record.

## Tests

- `tests/game/Timerunning.cpp` covers schedule parsing and boundaries, invalid reloads, saved identity, readiness, world-copy ids, spawn masks, map admission and party checks. Run `tests.exe "[Timerunning]"`.
- `tests/game/TimerunningInstalledData.cpp` is parked with the opening. Set `TIMERUNNING_DATA_DIR` to the extracted server data directory and run `tests.exe "Pandaria introduction placements match installed terrain"` on that branch. It checks the start point, the sixteen opening creatures and the rift object from the latest placement update against the installed terrain.
- The database migration and world start-up harnesses live outside the repository with the other validation records. They need MySQL 8.4 binaries and read the live server settings, so they are not part of CTest.

## Remaining feature work

The full event still needs dungeon and raid ownership with lockout separation, complete combat and leveling scaling, all thread tiers and gem abilities, retail cloak curves, event-wide Bronze, loot and vendors, specialization weapon-box rewards, achievement and collection eligibility, and restart-safe conversion. The optional rift quest closes rifts directly; retail anomaly combat and timing are not reproduced. Beyond the opening, the world copy uses the existing Pandaria database with its existing gaps. Legion gameplay is not implemented.

Conversion needs authoritative ownership and offline-state checks, replacement equipment and bags, migration of event items, abilities, currencies and quests, preservation of eligible collections and legacy rewards, relocation to the faction capital, and an atomic identity and cache update. Repeated requests must be harmless.

## References

The reviewed reference is [agatho/TrinityCore at 7289b16aad17e31959382f876c8b265641f8df30](https://github.com/agatho/TrinityCore/tree/7289b16aad17e31959382f876c8b265641f8df30), in particular commits ccfe175cedee576c1ad39d5a9c769677648304cd and 7289b16aad17e31959382f876c8b265641f8df30. Its unrelated changes were not imported. It accepted unvalidated client season numbers, did not restore the season during login loading, and cleared the season flag in place of an event-specific conversion. It contained no opening placements or scripts.

[Blizzard's launch description](https://news.blizzard.com/en-us/article/24092672/world-of-warcraft-remix-mists-of-pandaria-now-live) supports the level-10 start, the level-70 cap and the Timeless Isle introduction. [Blizzard's end-of-event description](https://news.blizzard.com/en-us/article/24123573/world-of-warcraft-remix-mists-of-pandaria-ends-soon) describes what conversion has to migrate. Season ids and the active-versus-enabled packet behavior were checked against the retail interface source at commit 6e96727fd523c80f2cd43dc1c43946b0336f1217.
