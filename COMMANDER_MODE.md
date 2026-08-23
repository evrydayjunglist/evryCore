# Commander Mode

Living implementation plan and work tracker for the RTS control layer: a top-down
camera, party-bot selection, and click-to-move orders on the retail client.

Last updated: 23 August 2026

Source plan: `Commander Mode.pdf`, revised 22 August 2026

This file is both the plan and the tracker. Keep the root `README.md` to a short job
summary, and keep experimental evidence in `modules/mod-rts-spike/README.md` until
the spike module is deleted.

## Status

| Phase | State | Exit condition |
| --- | --- | --- |
| 0. Client feasibility | **Complete** | Camera, command channel, ground order, cloned spell, and vehicle-seat tests pass |
| 1. Playerbot command API | **Ready** | One bot arrives and holds; five bots survive offset orders and a mid-walk redirect |
| 2. `mod-rts` translator | Waiting on Phase 1 | Guarded group orders reach the existing walker with no movement logic in `mod-rts` |
| 3. Commander addon | Waiting on Phase 2; camera completion also waits on Phase 4 | Selection, camera mode, reticle orders, state display, and clean exit work together |
| 4. Streaming viewpoint | Spike pending; does not block Phase 1 | A real active viewpoint streams the camera area without fighting the commentator camera |
| 5. evryOps support | Later | Configuration, read-only pins, and addon distribution are available without web orders |

Checkboxes mean:

- `[x]` — implemented and verified with the evidence named here.
- `[ ]` — not complete.
- **Blocked** — the dependency is named on the item.
- **Skipped** — a measured earlier result made the work unnecessary.

Only check an item after its build and acceptance test pass. Add the date and concise
evidence when a live-client result settles an unknown.

Keep the work units separate: Phase 1 is a playerbots job, Phase 2 is a `mod-rts`
job, Phase 3 is an addon job, Phase 4 starts as its own viewpoint spike, and Phase 5
belongs in the evryOps repository. The order-spell data cleanup is also a separate
content job; do not bury it in a C++ commit.

## Goal and boundaries

Commander Mode is a control layer over the existing retail client and existing
playerbot movement. Selection remains client-side. Orders are translated into the
same `PlayerbotWalker` used by autonomy, so there is one movement engine rather than
two competing ways to move a bot.

Hard boundaries:

- Use the stock `TrinityCore` addon-command channel; do not add custom opcodes.
- Do not patch, inject into, or reverse-engineer `Wow.exe` for Commander Mode.
- Do not use a vehicle seat for the selected camera design.
- Do not issue orders from evryOps; its bot pins remain read-only.
- Keep bot action player-like: the existing walker emits real client movement packets.
  Do not add `MotionMaster`, module teleports, direct attack calls, or direct spell
  preparation as Commander shortcuts.
- A commander may control only bots in their own group and on the same map.
- Use the caster's map for a ground order. Client-sent destinations contain XYZ but
  retain `MAPID_INVALID`.

## Confirmed design

### Order path

```text
Addon selection
  -> stock addon-command channel
  -> short-lived, validated subject context in mod-rts

RTS Order cast
  -> AllSpellScript::OnSpellCast
  -> clicked XYZ plus the caster's map

subject context + destination
  -> shared mod-rts move service and scope guards
  -> formation offsets
  -> mod-playerbots narrow command API and commanded latch
  -> existing PlayerbotWalker
  -> real CMSG_MOVE_* packets
```

- The addon owns the persistent UI selection and sends subject GUIDs over the command
  channel. An empty subject list means all controllable bots in the commander's group.
- The addon does not know the in-world clicked XYZ. `mod-rts` must join a short-lived
  subject context to the destination received by the server spell hook, then consume
  or expire that context. Direct chat or SOAP `rts move` commands call the same move
  service with explicit XYZ.
- Replies use the existing addon whisper-to-self path: acknowledgement, success,
  failure, and message lines.
- Bot positions and state may return over that path at about 2 Hz.
- `mod-rts` owns validation and translation, not pathfinding or movement.

### Camera

- The selected camera is the stock commentator camera. With
  `PLAYER_FLAGS_COMMENTATOR2` and `PLAYER_FLAGS_COMMENTATOR_CAMERA`,
  `C_Commentator.SetCameraPosition` moves it exactly and without the normal zoom or
  pitch clamp.
- The camera alone does not change server visibility. World streaming still follows
  the character through `m_seer`.
- The remaining camera work is a real active server viewpoint at the camera position.
  `Player::SetViewpoint` also writes `ActivePlayerData::FarsightObject`, so its
  composition with the commentator camera must be tested before product code is
  chosen.
- Vehicle camera, injected clamp lifting, stock-CVar fallback work, and a launcher
  project are not selected.

### Ground order

- Spell `2000001`, **RTS Order**, is the keeper input spell.
- It is learned normally, known, usable, long-range, ignores line of sight, shows the
  normal ground reticle, and works with `[@cursor]`.
- It is still diagnostic data, not the finished product input: the live overlay has
  `CastingTimeIndex` 5, a 12-second recovery, a 1.5-second start recovery, a 2.5%
  mana cost, and an area-trigger effect. Before Phase 2 consumes it, make it instant,
  inert, free, and without a global cooldown or cooldown, then repeat the cast tests.
- The client performs the ground raycast. The server reads the destination in
  `AllSpellScript::OnSpellCast` before `CheckCast`, so the destination is an input
  event rather than proof that the spell effect later succeeded.
- A new spell ID needs its same-ID client-only `Spell.db2` root as well as its name
  and server SpellInfo components. The repaired evryOps clone carries that root in
  the same hotfix push. No `SkillLineAbility` row is needed.

## Plan corrections from the PDF

The source PDF put the five-bot walker spike before Phase 1 while also identifying
Phase 1 as the work that creates the order API needed to run it. The test is therefore
tracked as Phase 1's final acceptance gate. This preserves the load test: Phase 2 may
not begin until it passes.

The PDF's conditional Phase 4 vehicle eye is also superseded. The commentator camera
won the ladder, but live testing found that streaming still follows the body. Phase 4
now tracks commentator-camera composition with a real server viewpoint instead of a
vehicle.

## Phase 0 — client feasibility

Throwaway harness: `modules/mod-rts-spike/` and its `RTSSpike` addon.

- [x] Commentator flags unlock `C_Commentator` camera control.
  - Evidence: exact, unclamped camera movement in the same coordinates as `.gps`.
- [x] Measure world streaming with the camera away from the body.
  - Evidence: objects near the body remain visible; objects near a camera about 300
    yards away are not streamed.
- [x] Round-trip a command over the stock `TrinityCore` addon prefix.
  - Evidence: `/rtsspike ping` returns `ack`; `/rtsspike cmd server info` returns
    acknowledgement, message lines, and `ok`, with commentator flags both on and off.
- [x] Deliver a ground destination from the stock client.
  - Evidence: the server receives clicked XYZ through `AllSpellScript::OnSpellCast`.
- [x] Deliver a ground destination at commander-camera range.
  - Evidence: with the camera about 300 yards from the body, clicks land near the
    camera after the spell receives long range and ignore-line-of-sight properties.
- [x] Make a fresh cloned spell learnable and castable.
  - Evidence: spell `2000001` passed learned, known, usable, reticle, and `[@cursor]`
    checks without a `SkillLineAbility` row.
- [x] Deliver a destination while vehicle-seated.
  - Evidence, 23 August 2026: with commentator flags off, Wintergrasp Demolisher
    entry `28094` accepted the player in seats 2 and 3. Each `/cast RTS Order`
    delivered a separate destination for spell `2000001` while the player remained
    seated. The logged map value was the expected `MAPID_INVALID`.
- **Skipped:** launcher hello-world. No Commander client patching was selected.

Exit result: the client feasibility gate is complete. Phase 1 may begin.

## Phase 1 — playerbot command API and commanded latch

Location: `modules/mod-playerbots/`

Scope: one self-contained change to the existing subsystem

### Decisions before implementation

- [ ] Decide combat behavior while commanded.
  - Recommended starting behavior: a bot still defends itself when attacked, then
    loots its allowed kill at its feet, and returns to the outstanding command. This
    matches the existing attacker-and-loot rules without letting autonomous quest
    selection take over.
- [ ] Decide command ownership and timeout semantics.
  - The latch needs the commanding player's identity, explicit release behavior, and
    a bounded stale-command timeout. Arrival must hold position long enough for the
    arrive-and-stay test rather than immediately restoring quest autonomy.
- [ ] Decide stop, hold, and release semantics.
  - A move needs to remain commanded on arrival. There must also be one explicit way
    to stop and hold position and one explicit way to release the bot back to
    autonomy; do not let command names acquire those meanings accidentally.

### Implementation

- [ ] Add a narrow public lookup from `Player*` to its managed bot without exposing
  `PlayerbotRecord`.
- [ ] Add `OrderMoveTo(ObjectGuid commanderGuid, Player* bot, Position const&)`, or a
  narrow request type carrying the same owner identity.
- [ ] Add `OrderStop(ObjectGuid commanderGuid, Player* bot)` for the chosen stop/hold
  behavior.
- [ ] Add an owner-matched explicit release operation that restores autonomy.
- [ ] Add command owner, requested destination, hold/move state, and timeout data to
  `PlayerbotRecord`, so a permitted self-defense fight can resume the same command.
- [ ] Clear incompatible autonomous targets when a command begins.
- [ ] While commanded, continue ordinary session housekeeping, death handling, and
  `Walker.Update`, but prevent quest, vendor, idle-combat, and unrelated loot
  selection from replacing the order. If commanded self-defense is enabled, keep the
  existing allowed corpse-loot flow for that kill before resuming the stored order.
- [ ] Stop movement without releasing autonomy when the chosen hold operation is
  used. Release safely only on explicit release, commander logout, bot logout, map
  mismatch, death as decided, and timeout.
- [ ] Log command start, redirect, arrival, release, refusal, and timeout without a
  per-tick flood.
- [ ] Add focused automated coverage for lookup, latch transitions, authorization
  data, timeout, and stop/redirect behavior where the current test harness permits.
- [ ] Extend `mod-rts-spike` with a test-only command that calls the new API for up to
  five grouped bots and assigns five simple, explicit offsets. This is only the scale
  harness; product formation remains Phase 2 work.
- [ ] Rebuild RelWithDebInfo `worldserver`.

### Acceptance gate

- [ ] Order one bot to a valid point; it arrives and stays instead of resuming quest
  work.
- [ ] Redirect that bot mid-walk; it takes the new walk without competing autonomy.
- [ ] Use the throwaway spike harness to order five grouped bots to five formation
  offsets, then redirect all five mid-walk.
- [ ] Confirm all five arrive without packet floods, walker corruption, stacking, or
  autonomy replacing the order.
- [ ] Confirm explicit release restores normal autonomous work.

Do not start Phase 2 until this gate passes.

## Phase 2 — `mod-rts`, the order translator

Location: new `modules/mod-rts/`

Scope: additive module; no movement engine and no new core call site

- [ ] Register a real `CommandScript` with an `rts` command tree and scoped auth RBAC
  rows. Use raw `AllCommandScript` interception only if the command/RBAC path proves
  unsuitable.
- [ ] Complete the separate spell-data prerequisite: turn spell `2000001` from a
  diagnostic clone into an inert input spell that is instant, has no gameplay area
  trigger, cost, global cooldown, or cooldown. Preserve its destination target, long
  range, ignore-line-of-sight property, normal reticle, and client-only `Spell.db2`
  root, then repeat known/usable/reticle/cursor/vehicle checks.
- [ ] Decide and test how the addon arms subjects for a ground cast. Recommended
  starting design: keep one short-lived, one-shot subject context per commander,
  reject stale or missing context, and consume it when spell `2000001` supplies a
  destination. The server must not treat this as permanent UI selection.
- [ ] Add an `AllSpellScript::OnSpellCast` handler for spell `2000001` that reads the
  client destination before `CheckCast`, pairs it with the validated subject context,
  takes the map from the caster, and calls a shared move service.
- [ ] Accept an addon-provided subject GUID list; empty means controllable group bots.
- [ ] Implement the MVP commands:
  - [ ] `rts move <x> <y> <z>`
  - [ ] `rts stop`
  - [ ] `rts state`
- [ ] Add the distinct hold or release command required by the Phase 1 semantics.
- [ ] Validate command ownership, group membership, map, destination, rate, and RBAC
  before calling the playerbot API.
- [ ] Route the spell hook and direct chat/SOAP move command through the same validated
  move service.
- [ ] Take the ground-order map from the commander, never the destination object.
- [ ] Add deterministic formation offsets so multiple bots do not stack.
- [ ] Return batched bot position and command state at about 2 Hz.
- [ ] Add `rts hold` and `rts follow <name>` after move/stop/state pass.
- [ ] Add a `.conf.dist` only for genuine runtime policy; keep player-model numbers as
  code constants unless a knob is justified.
- [ ] Add automated command parsing, scope, rate-limit, and formation tests.
- [ ] Rebuild RelWithDebInfo `worldserver`.

Acceptance:

- [ ] Chat, addon channel, and SOAP reach the same command tree.
- [ ] A commander cannot address a non-group bot or a bot on another map.
- [ ] A valid group move reaches only the existing `PlayerbotWalker` command API.
- [ ] A stale or missing subject context cannot turn a ground cast into an order.
- [ ] Repeated state replies and redirects remain below channel and AntiDOS limits.

## Phase 3 — Commander addon

Location: retail Lua addon distributed as a zip

- [ ] Replace the scratch UI with selection frames for controllable group bots.
- [ ] Add click selection, Shift-add/remove, select-all, and control groups.
- [ ] Add clean enter and exit for the commentator camera preset.
- [ ] Bind **RTS Order** to a key for reticle and `[@cursor]` ground orders.
- [ ] Send the selected subject GUIDs through the `rts` command tree and arm the
  server context before casting **RTS Order**. The cast destination comes from the
  server spell hook, not from Lua.
- [ ] Display position, commanded state, movement state, and failures from `rts state`.
- [ ] Add world-map orders by sending normalized `UiMap` coordinates; convert them
  with the server's retail `UiMapAssignment` math and obtain Z from server terrain.
- [ ] Add waypoint queues only after direct move, stop, and redirect are reliable.
- [ ] Package a clean downloadable zip with setup and keybinding documentation.

Acceptance:

- [ ] Select one bot and move, redirect, stop, and release it.
- [ ] Select five bots and preserve formation through a redirect.
- [ ] Enter and leave camera mode without losing command-channel replies.
- [ ] Reloading the UI clears or reconstructs selection without leaving stale commands.

## Phase 4 — streaming viewpoint

Location: `mod-rts` plus the existing viewpoint APIs

Status: independent spike may run before Phase 3 is complete

Gate: it does not block Phase 1 or the non-camera parts of Phase 2, but it must pass
before Phase 3 camera integration is implemented or Phase 3 is marked complete.

- [ ] Build a throwaway composition test using a real same-map world object as the
  active viewpoint while the commentator camera is detached.
- [ ] Verify whether `Player::SetViewpoint` moves or fights the client camera through
  `ActivePlayerData::FarsightObject`.
- [ ] If they compose, design the smallest module-owned viewpoint lifecycle: create,
  make active, move at a bounded rate, and clean up on exit, logout, teleport, map
  change, death, or module shutdown.
- [ ] Keep the commander's body parked and vulnerable under ordinary game rules.
- [ ] Reject invalid maps and coordinates; never use the viewpoint as a teleport.
- [ ] Verify creatures and terrain stream around the camera position while the
  commentator camera remains controllable.
- [ ] If they conflict, stop and choose a new camera/streaming design before product
  implementation. Do not silently fall back to a vehicle or injected client.
- [ ] Rebuild RelWithDebInfo `worldserver` for any C++ version of the spike or product
  viewpoint.

Exit result: camera control and server visibility follow the commander view together,
and every exit path restores the normal viewpoint.

## Phase 5 — evryOps touchpoints

Location: `D:\WOWEmulation\Emulators\Tools\evryOps`

- [ ] Expose `mod-rts.conf` through the existing Conf page if the module gains config.
- [ ] Add read-only live bot pins to the existing Map page.
- [ ] Host the packaged addon zip when client-asset distribution is built.
- **Skipped:** VehicleSeat overlay editor; the vehicle camera rung did not win.
- **Never:** movement buttons, task assignment, or any other web control plane.

## Deferred vocabulary and polish

- `rts follow <name>` and `rts hold` follow the move/stop MVP.
- World-map orders follow in-world reticle orders.
- Waypoint queues follow reliable direct redirects.
- Heals, buffs, stance logic, interrupts, pets, and class rotations remain separate
  playerbot jobs, not Commander prerequisites.
- Shared bot knowledge, continent convenience travel, and guide-scripted brains remain
  out of scope.

## Evidence log

| Date | Result |
| --- | --- |
| 22 Aug 2026 | Commentator flags enabled exact, unclamped `C_Commentator` camera movement. |
| 22 Aug 2026 | Addon command channel returned replies with commentator flags on and off. |
| 22 Aug 2026 | Commander-range ground clicks followed the detached camera after range and line-of-sight constraints were removed from the order spell. |
| 23 Aug 2026 | Fresh clone `2000001` passed learn, known, usable, reticle, and cursor-cast checks. |
| 23 Aug 2026 | Vehicle seats 2 and 3 each delivered an RTS Order destination with commentator flags off. |
| 23 Aug 2026 | Live overlay inspection confirmed spell `2000001` still needs product cleanup: cast time, mana cost, global cooldown, cooldown, and area-trigger effect remain. |
