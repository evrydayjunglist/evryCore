# mod-rts-spike

Throwaway harness for Commander Mode experiments. The current branch contains the completed Phase 0A camera, channel, ground-order, cloned-spell, and vehicle-seat evidence plus the Phase 0B direct-switch measurement harness. Source inspection and a live retail-client run reject the current stock-server path as playable possession: camera and active-mover state can follow the bot, while selection, ordinary spell casts, and the visible player UI remain on the session owner. Melee, bar persistence, death, other forced exits, and cleanup were not run because the playable-possession gate had already failed. Delete this whole folder once the retained evidence and any deliberately selected reusable seams have been separated from the throwaway harness.

It is a normal drop-in module (`modules/mod-rts-spike/`), so a static build compiles it in. The client half is the `RTSSpike` addon under `addon/`.

## Setup

1. Build worldserver (see the root AGENTS.md build block). This module links in automatically.
2. Copy `addon/RTSSpike/` into the client's `Interface/AddOns/`.
3. Log in on a GM account (the commands use the `debug` RBAC permission). `/rtsspike` in chat prints usage.

## Spike 1 — commentator free camera (the camera-ladder decider)

This is the one to run first. If it works, the hardest problem on the plan disappears.

1. In chat: `.rtsspike camflags on` (sets `PLAYER_FLAGS_COMMENTATOR2` + `PLAYER_FLAGS_COMMENTATOR_CAMERA` on you).
2. `/rtsspike cam` — dumps whether `C_Commentator` exists and what it exposes.
3. `.gps` to read your current coordinates, then `/rtsspike camset <x> <y> <z>` with a point a little above and behind you.
4. Record: does `C_Commentator.SetCameraPosition` exist, does it error, and does the camera actually detach and move?

Outcome: **works**. The two flags satisfy the client gate and `SetCameraPosition` moves the camera exactly, so no commentator opcode implementation is needed. The remaining camera question is whether a server viewpoint for world streaming composes with the commentator camera; the camera mechanism itself is settled.

## Spike 2 — addon command channel round-trip

1. `/rtsspike ping` — should print an `ack` reply within a moment. That alone proves the channel both ways.
2. `/rtsspike cmd server info` — runs a real server command over the channel and prints the `message` lines back.

Outcome to write down: ack received yes/no; whether `CONFIG_ADDON_CHANNEL` had to be touched (it defaults on).

## Spike 3 — order reticle (clicked coordinates from a stock client)

1. Pick an existing ground-targeted spell you can cast, or clone one via evryOps. Blizzard (`.blink`-style) AoE reticles work.
2. `.rtsspike reticle any` to log every ground cast, or `.rtsspike reticle spell <id>` to watch one.
3. Cast it at a spot on the ground. The server prints `dest map <m> at x y z` in chat and to `Server.log`.
4. Repeat while seated in a vehicle, to confirm the cast still delivers a destination when mounted on the future commander eye.

Outcome to write down: destination matches the clicked point yes/no; works while vehicle-seated yes/no.

## Phase 0B — direct switch to one managed bot

This is a single-pair GM probe, not the production direct-switch feature. It adds no
custom opcode, client patch, input proxy, unattended-body AI, or production actor
router. The controller and bot must be alive, stationary, visible, in the same group
and map instance, off transports and vehicles, out of teleport and existing control
states, and backed by live sessions. The playerbot controller is quiesced before the
native viewpoint → active mover → `SetClientControl` sequence begins.

The harness sends the bot's known spells, action buttons, cooldown history, and charge
state to the controller using their stock server packets. These packets do not carry a
player identity. The controller's action bars are therefore snapshotted before the
probe, all bar differences are logged, and the snapshot is restored on release. This
is measurement and crash-safe persistence is not claimed.

The bot has no general party-invite behavior yet. For this spike, invite it normally
and then make its managed session send the stock acceptance packet:

```text
/invite Botname
.rtsspike acceptinvite Botname
```

The helper accepts only the GM controller's pending invite to an online, same-instance
managed bot. It queues `CMSG_PARTY_INVITE_RESPONSE`; it does not create a group or call
`Group::AddMember` from the module.

### Live checklist — not yet run

Run this only in a controlled test area. Source inspection shows that target, melee,
spell, and bar input will act on the original session owner on the current core even
while movement names the bot as active mover.

1. Invite the managed bot normally, run `.rtsspike acceptinvite Botname`, and confirm
   both players are in the same group and map. Stand the controller still and make sure
   neither player is charmed, falling, teleporting, flying, transported, or in a
   vehicle. The bot may be wandering; acquisition quiesces it and waits for it to stop.
2. Select the bot and run `.rtsspike switch`, or run `.rtsspike switch Botname`.
   `.rtsspike switchstatus` prints the reserved pair and phase.
3. Record camera and ordinary movement behavior. Then, with a harmless target and the
   original body positioned safely, record selection, one melee request, and one spell
   known by the bot. Compare every `mod-rts-spike: input` line in `Server.log`: movement
   is labelled `route=active-mover`; selection, melee, casts, and bars are labelled
   `route=session-owner`.
4. Record whether the bot's spells, bars, cooldowns, cast results, health, and power
   appear coherently. Move one action-bar button and record which character changes.
5. Run `.rtsspike release`. Verify the original mover, viewpoint, input, spell state,
   and snapshotted bars return without relogging.
6. Repeat separately for possessed-bot death, original-character death followed by
   explicit release, either logout, teleport/map change, group removal, failed acquire,
   and normal server shutdown. Do not mark any Commander Mode live checkbox until the
   matching case has actually passed.

First live setup attempt, 23 August 2026: party creation through the packet-driven
acceptance helper worked far enough to attempt `.rtsspike switch Opai`, but acquisition
was refused before mutation with `controller does not own its normal mover/viewpoint`.
The harness had incorrectly required `GetViewpoint()` to return the controller;
TrinityCore returns `nullptr` for the normal self-view and uses `FarsightObject` only
for a non-default viewpoint. The predicate now accepts that normal null value and emits
separate mover and viewpoint refusals. This attempt completed no control-transfer check.

Second live run, 23 August 2026: **the current stock path is a playable-possession
no-go.** Magey's camera moved to Opai and the client acknowledged Opai as active mover;
`CMSG_SET_ACTIVE_MOVER`, movement heartbeat, and fall-land packets all carried Opai's
GUID and matched the server's expected mover. The run did not establish ordinary
walk/turn control. Target selection still arrived on Magey's session owner, including
selecting Opai as though Magey were still the actor. Spell `2000001` and ordinary
Blizzard casts resolved with Magey as the actual caster. The server sent Opai's 115
known spells, 5 action buttons, spell history, and charge state to Magey's client, but
the visible controls remained Magey's. This proves that sending the actorless stock UI
packets does not retarget the player UI to Opai.

Explicit `.rtsspike release` succeeded without relogging: Magey's normal camera and
play returned, the owner UI snapshot reported no changed bar slots, and
`Playerbots.log` recorded Opai leaving the direct-control spike and resuming her
Builtin movement. Melee input, an actual action-bar edit and persistence check, both
death cases, and the other forced exits were not tested. The failed possession gate is
already sufficient for the Phase 0B stock-path no-go; do not describe those remaining
cases as passed.

### Source and build result — 23 August 2026

**No-go for playable direct switch on the current stock-server input path.** Existing
`Player::SetClientControl` and `Unit::SetMovedUnit` send stock control/viewpoint/active-
mover state, and movement validation resolves the packet GUID against
`GetUnitBeingMoved()`. In contrast, the stock handlers call `_player->SetSelection`,
`_player->Attack`, `_player->CanRequestSpellCast` / `RequestSpellCast`, and
`GetPlayer()->AddActionButton` / `RemoveActionButton`. Those verbs therefore remain on
the original character. Mover-only success would not satisfy possession.

The UI probe can send actor data through stock packets, but those packets have no actor
identity and incoming bar edits remain owner-bound. After this result, the owner selected
a capability-negotiated custom client/protocol boundary comparable in responsibility to
SuperUI. Its exact client form and production protocol remain separate work. Client
patching, injection, reverse engineering, custom opcodes, a purpose-built client, and
companion tooling are all available options subject to the safety proof.

The RelWithDebInfo `worldserver` and `tests` targets build successfully. The focused
`[rts-spike]` tests pass 20 assertions in two cases. These are source/build results, not
live retail-client evidence.

## Spike 5 — walker at RTS scale

Not scripted here — it needs the shared commandable-player movement API that does not exist yet. This is Phase 1A's final acceptance test, not work to run before the API. When Phase 1A lands, order five grouped bots to one point with explicit test offsets and redirect them mid-walk.

## Answers (fill in, then delete the module)

- **Spike 1 commentator camera: works, with one question still open.** `C_Commentator`
  exists in full on Midnight. Setting both player flags reloads the client and detaches
  the camera; `C_Commentator.SetCameraPosition` then moves it to a given point and the
  camera flies free with the character no longer visible. So the camera ladder lands on
  rung 1 and no client patching is needed for the camera. The client's own API
  documentation gates every camera function behind `RequiresActiveCommentator` with
  failure mode `ReturnNothing`, and that gate is open with the flags alone, which also
  opens `SetCamera` (position, yaw, pitch, roll and field of view in one call).
  `SetCameraPosition` is exact and unclamped, in the same world coordinates `.gps`
  reports, so nothing needs converting and the client's zoom ceiling does not apply.
  The world does not stream around the camera. With the camera 300 yards from the
  body, creatures near the body still render and creatures near the camera do not.
  That is `m_seer`: the core has no commentator handler, so the server still streams
  entities around the character and the client can only draw what it has been told
  exists. The camera alone is not enough, and the fix is SuperUI's: a world object at
  the camera position, made active, set as the viewpoint through
  `Player::SetViewpoint`, with the addon telling the server where the camera is.
  Note before building that: `SetViewpoint` writes `ActivePlayerData::FarsightObject`,
  which the client reads to move its own camera the way Eye of Kilrogg does. Whether
  that fights the commentator camera or composes with it is untested.
- **Spike 2 addon channel: works.** `/rtsspike ping` returns `ack` and
  `/rtsspike cmd server info` returns `ack`, the message lines, then `ok`. It behaves
  identically with the commentator flags on and off, so the control channel survives
  commander mode. `AddonChannel` did not have to be touched; the default of true held.
  Replies arrive as a whisper to self from the realm-qualified name (`Orcy-Trinity`), so
  anything that filters on sender must expect the realm suffix. Addon chat is throttled
  at 100 messages per second before a mute, and GM accounts skip the check, so the
  planned two-per-second state pump has plenty of room.
- **Spike 3 order reticle: coordinates arrive, but the client decides whether you may
  click at all.** A ground cast delivers the clicked point to the server, read from
  `AllSpellScript::OnSpellCast`. Two things to know before building on it. The
  destination's map is `MAPID_INVALID`, because a client-sent destination is filled by
  `Position::Relocate`, which copies x, y, z and orientation and never the map; take
  the map off the caster instead. And the run used stock Blizzard, whose range and line
  of sight are enforced by the client before it sends anything: at commander-camera
  distance the cursor greys out, clicking answers "out of range", and no packet reaches
  the server. That wording is the useful part. The client found ground under the
  commander camera and turned the cast down on distance alone, so its terrain raycast
  follows the commentator camera and only the spell's range is in the way. Line of
  sight is enforced separately: a click behind a building while in range answers
  "Target is not in line of sight", a different refusal from the range one, and it is
  measured from the character rather than the camera. Both have to be answered. The
  server hook running before `CheckCast` does not
  help, because there is nothing to hook. So the order spell has to be a hotfixed clone with a long
  `SpellRange` row through `SpellMisc.RangeIndex` and
  `SPELL_ATTR2_IGNORE_LINE_OF_SIGHT` in `SpellMisc.Attributes`. Pick an instant spell:
  Blizzard's periodic component re-casts at the stored destination every tick, so one
  click logged many lines.

  **Rerun with those two changed, and it works.** Blizzard's own `SpellMisc` (ID
  164352) was overlaid to `RangeIndex` 13 and `Attributes3` 4, and with the camera 300
  yards from the body, clicks under the commander camera reached the server at
  -521.698 -3956.561 41.273 and -522.517 -3963.887 45.798 — both within about twelve
  yards of the camera and over three hundred from the character, which was still at
  -525.066 -4268.14. So the client's terrain raycast follows the commentator camera,
  and ground ordering at commander range needs no client patching at all, only a spell
  whose range and line of sight allow the click. That the server accepted a three
  hundred yard ground cast is itself the proof the overlay was live: row 5 is 0-40
  yards for both the hostile and the friendly pair, and `Spell::prepare` refuses in
  `CheckCast` before `_cast` ever reaches the logging hook.

  The Blizzard cast itself was invisible from up there, which is the same streaming
  limit Spike 1 found: the effect spawned within the body's seer range, so the client
  was never told about it.

  The log now records the caster's position and the distance to the click as well as
  the click itself. The clicked point alone cannot tell a click at a distant camera
  from a click next to the character, and reading one without the other is how this
  result got doubted after the fact.

  The clone failure had a separate, measured cause. The first clones copied
  `SpellName` and the SpellInfo component records but omitted the client-only
  `Spell.db2` root. Their name resolved, but the client reported them unknown and
  unusable and sent no cast. Adding that root through `hotfix_blob` made the
  diagnostic clone cast. The repaired evryOps clone now copies the effective root,
  name, and components in one push. Fresh spell 2000001 produced the normal learned
  message, returned known and usable, showed the reticle with `/cast RTS Order`, and
  cast at the clicked point with `/cast [@cursor] RTS Order`. The temporary
  `SkillLineAbility` rows for 1312659 and 2000000 did not change their behavior; the
  working clone has no such relation.

  Spell 2000001 is the keeper feasibility spell, not finished product data. Live
  overlay inspection still shows `CastingTimeIndex` 5, a 12-second recovery, a
  1.5-second start recovery, a 2.5% mana cost, and an area-trigger effect. Before the
  order translator consumes it, make it instant and inert with no cost, global
  cooldown, or cooldown, then repeat the cast checks.

  The vehicle follow-up also passed on 23 August 2026, with the commentator flags
  off to isolate the seat test. Wintergrasp Demolisher entry 28094 kept the player
  seated in seats 2 and 3, and `/cast RTS Order` delivered a separate spell 2000001
  destination from each seat. The logged map value was the expected `MAPID_INVALID`;
  the order implementation takes the map from the caster. A combined
  commentator-plus-vehicle run is unnecessary because the commentator camera won
  the ladder and Commander Mode does not use a vehicle for its view.

  Cleanup snapshot `2026-08-23_00-29-30` and hotfix push 110669 removed both
  diagnostic clones and those `SkillLineAbility` rows. The realm-wide Blizzard
  `SpellMisc` override (ID 164352) was deleted, and the same push marks the native
  record valid so a reconnect refreshes its original data. Spell 2000001 and all of
  its cloned records remain.

  Follow-up snapshot `2026-08-23_00-53-35` removed only the obsolete Valid Spell
  record from diagnostic push 110667. Once its payload was deleted, leaving that
  record made `LoadHotfixData` report an unknown store at every boot. The newer
  removal record and spell 2000001's Valid record remain, and the next boot was
  clean.
- **Phase 0B direct switch: live no-go confirmed.** The stock client accepted Opai's
  viewpoint and active-mover identity, while selection and spell casts remained Magey's
  and the stock UI packets did not replace Magey's visible controls. Explicit release
  restored Magey and resumed Opai's Builtin controller. Untested melee, bar-persistence,
  death, and lifecycle cases remain recorded as untested rather than passed.
- Spike 5 walker at scale: blocked on Phase 1A and retained as that phase's final acceptance test

The living implementation plan and tracker is [COMMANDER_MODE.md](../../COMMANDER_MODE.md).
