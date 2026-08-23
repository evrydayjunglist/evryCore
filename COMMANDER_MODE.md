# Commander Mode

Living implementation plan and work tracker for three related control modes on the
retail client: ordinary manual play, direct switch/possession of a party bot, and an
RTS free view that commands the player's original character alongside party bots.

Last updated: 23 August 2026

Source plan: `Commander Mode.pdf`, revised 22 August 2026

This file is both the plan and the tracker. Keep the root `README.md` to a short job
summary, and keep experimental evidence in `modules/mod-rts-spike/README.md` until
the spike module is deleted.

## Status

| Phase | State | Exit condition |
| --- | --- | --- |
| 0A. Commander client feasibility | **Complete** | Camera, command channel, ground order, cloned spell, and vehicle-seat tests pass |
| 0B. Direct-switch feasibility | **Ready; decisions pending** | Stock-client control transfer, actor routing, UI restoration, and safe release are measured |
| 1A. Shared commandable-player movement | Waiting on Phase 0B and owner decisions | The player body and managed bots use one move/hold/release contract; one- and five-bot tests pass |
| 1B. Explicit attack and loot adapters | Waiting on Phase 1A | Manual attack and loot reach player-like packet paths for the player body and managed bots |
| 2. `mod-rts` translator | Waiting on Phases 1A and 1B | Guarded group orders reach shared adapters with no action or movement engine in `mod-rts` |
| 3. Commander addon | Waiting on Phase 2; camera completion also waits on Phase 4 | Selection, camera mode, reticle orders, state display, and clean exit work together |
| 4. Streaming viewpoint | Spike pending; does not block Phase 1A | A real active viewpoint streams the camera area without fighting the commentator camera |
| S1. Direct switch product | Waiting on Phase 0B and Phase 1A | The client can play an authorized party bot while the original character is safely AI-driven, then restore both |
| 5. evryOps support | Later | Configuration, read-only pins, and addon distribution are available without web orders |

Checkboxes mean:

- `[x]` — implemented and verified with the evidence named here.
- `[ ]` — not complete.
- **Blocked** — the dependency is named on the item.
- **Skipped** — a measured earlier result made the work unnecessary.

Only check an item after its build and acceptance test pass. Add the date and concise
evidence when a live-client result settles an unknown.

Keep the work units separate: Phase 0B is a control-transfer spike, Phase 1A is the
shared movement-state and playerbots-adapter job, Phase 1B adds explicit attack and
loot adapters, Phase 2 is a `mod-rts` job, Phase 3 is the RTS addon job, Phase 4 starts
as its own viewpoint spike, S1 is the production direct-switch job, and Phase 5 belongs
in the evryOps repository. The order-spell data cleanup is also a separate content job;
do not bury it in a C++ commit.

## Goal and boundaries

Commander Mode is a control layer over the existing retail client and player-like
movement. Selection remains client-side. Managed bots continue to use the same
`PlayerbotWalker` used by autonomy, so there is one movement engine rather than two.
The player's original character is also a commandable RTS subject; its unattended
driver must be proven safe before Phase 1A chooses an adapter, because human client
input and automated packets must never compete for the same `Player`.

Hard boundaries:

- The RTS path uses the stock `TrinityCore` addon-command channel and no custom
  opcodes.
- Do not patch, inject into, or reverse-engineer `Wow.exe` for Commander Mode. Direct
  switch has a separate unresolved client-boundary decision; do not add a modified
  client or custom protocol unless the owner explicitly selects it after Phase 0B.
- Do not use a vehicle seat for the selected camera design.
- Do not issue orders from evryOps; its bot pins remain read-only.
- Keep commanded action player-like. Managed bots use the existing walker and real
  client packets. Do not add `MotionMaster`, module teleports, direct attack calls, or
  direct spell preparation as Commander shortcuts.
- A commander may address only their own original character and eligible bots in their
  own group and on the same map.
- Use the caster's map for a ground order. Client-sent destinations contain XYZ but
  retain `MAPID_INVALID`.

## Confirmed design

### Control modes

```text
Normal play
  client -> original character
  autonomy -> party bots

Direct switch
  client -> selected party bot
  selected unattended policy -> original character
  autonomy -> remaining party bots

RTS free view
  client -> commentator camera and RTS UI
  RTS orders -> original character and selected party bots
```

- Direct switch and RTS free view are sibling modes built on a shared controller and
  commandable-`Player` lifecycle. Direct switch is not an RTS camera feature.
- Switching to a bot must make that bot the client's real input actor, with its own
  movement, targeting, spells, cooldowns, and action bars. Switching back restores the
  original character without relogging.
- While the client controls a bot, the original character remains in the world under
  an explicitly selected unattended policy. In RTS mode, the original character is a
  selectable commandable unit alongside party bots.
- Only one controller may drive a `Player` at a time. Normal input, direct possession,
  autonomous playerbot work, unattended-body work, and RTS orders must have explicit,
  non-overlapping transitions.

### RTS order path

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
  -> shared commandable-Player service and commanded latch
       -> managed-bot adapter -> existing PlayerbotWalker
       -> unattended-body adapter -> design proven by Phase 0B
  -> player-like CMSG_MOVE_* packets
```

- The addon owns the persistent UI selection and sends subject GUIDs over the command
  channel. Subjects may include the commander's original character and eligible bots
  in the group. The exact empty-selection behavior is an open product decision.
- The addon does not know the in-world clicked XYZ. `mod-rts` must join a short-lived
  subject context to the destination received by the server spell hook, then consume
  or expire that context. Direct chat or SOAP `rts move` commands call the same move
  service with explicit XYZ.
- Replies use the existing addon whisper-to-self path: acknowledgement, success,
  failure, and message lines.
- Commandable-unit positions and state may return over that path at about 2 Hz.
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

## Open product decisions

- [ ] Decide the direct-switch client boundary.
  - Start by testing the stock retail client and existing packets. SuperUI's full
    switch uses custom client packets to proxy a bot's UI and inputs; evryCore cannot
    assume that design is available.
  - If the stock client cannot provide the required experience, stop and decide
    explicitly whether a modified client is acceptable. Do not drift into client
    patching as an implementation detail.
- [ ] Decide the unattended original-character policy during direct switch.
  - Choices include hold, follow/assist/defend, the full existing playerbot brain, or
    the shared RTS command service. Begin with the least autonomous safe behavior that
    matches how the owner wants to play.
- [ ] Decide what entering RTS mode does to eligible units.
  - Either the original character and all party bots immediately enter commanded hold,
    or each remains in its current controller until its first accepted order.
- [ ] Decide whether the original character may be individually released while RTS
  free view remains active.
  - Releasing a bot can restore bot autonomy. It cannot restore ordinary human input to
    the original character while the same client remains the RTS camera/controller.
    Recommended behavior: disallow individual release of the original body; exiting
    RTS restores human control, while a separate unattended-policy command may change
    how the body acts during free view.
- [ ] Confirm the initial RTS combat and loot policy.
  - Current direction: commanded units do not autonomously acquire targets, retaliate,
    chase, or loot. Attack and loot are explicit commander orders. Passive, defensive,
    assist, aggressive, and auto-loot-in-reach policies may be added later.
- [ ] Decide commander arbitration when more than one human is in the group.
  - Entering free view must not silently create hidden per-bot locks. Choose one active
    commander per group, group-leader control, or another visible rule.
- [ ] Decide the post-resurrection RTS state.
  - Current direction: death suspends the order, normal player-like corpse recovery
    runs automatically, and the unit returns alive in hold awaiting a new command.
- [ ] Confirm direct-switch behavior when either character dies.
  - Recommended behavior: a possessed bot's death force-releases direct possession and
    restores the original character as the client's actor, even if that character is
    currently dead or a ghost. The normal death UI and controls then apply.
  - The unattended original character's death does not force-release possession. Keep
    controlling the bot, surface the original character's death, clear its active
    unattended task, and let the selected player-like recovery policy run. An explicit
    switch back before recovery restores control to the original character in its
    current dead or ghost state rather than hiding or repairing that state.

`release spirit` and `release command/control` are separate operations. Releasing the
spirit begins ghost and corpse recovery; it does not inherently restore quest autonomy
or end the RTS association.

SuperUI is useful evidence for those control-state transitions: it force-releases when
the possessed bot dies, but not when the unattended original character dies, and it
clears the dead AI unit's current task. Its recovery mechanics are not selected here.
Do not copy direct `BuildPlayerRepop`, ghost teleport, or `ResurrectPlayer()` shortcuts;
evryCore keeps its existing queued release-spirit, ghost-walk, and reclaim packets.

The earlier suggested 15-minute stale-command timeout is withdrawn. No arbitrary
elapsed-time release is selected. A legitimate hold must be able to last indefinitely;
cleanup should follow explicit exit or a concrete invalidation such as logout, group
removal, map incompatibility, or destruction. Add a lease only if testing demonstrates
a real abandoned-controller case that those events cannot clean up.

## Plan corrections from the PDF

The source PDF put the five-bot walker spike before the movement API while also
identifying that API as the work needed to run it. The test is therefore tracked as
Phase 1A's final acceptance gate. This preserves the load test: Phase 2 movement work
may not begin until it passes.

The PDF's conditional Phase 4 vehicle eye is also superseded. The commentator camera
won the ladder, but live testing found that streaming still follows the body. Phase 4
now tracks commentator-camera composition with a real server viewpoint instead of a
vehicle.

The original plan treated only party bots as RTS subjects and did not include direct
switch. The product vision now has three control modes: normal play, direct switch to a
party bot while the original character is AI-driven, and RTS free view where that
original character and party bots are all commandable. A narrow direct-switch
feasibility spike therefore precedes the shared Phase 1A backend, while production
switch remains a sibling track after that shared foundation.

Branch sequence:

1. Merge the completed Phase 0A `job/rts-spike` work into `evry`.
2. Create a fresh disposable Phase 0B switch-spike branch from updated `evry`.
3. When Phase 0B is measured, remove any throwaway-only switch code and merge its
   evidence plus only deliberately accepted reusable seams into `evry`.
4. Create the Phase 1A movement branch from that refreshed `evry`; after it lands,
   create a separate Phase 1B attack/loot branch.
5. Branch S1 only after its prerequisites have landed on `evry`.

Do not turn `job/rts-spike` or the Phase 0B branch into a stacked umbrella branch.

## Phase 0A — Commander client feasibility

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

Exit result: the original Commander client feasibility gate is complete. Phase 0B may
begin.

## Phase 0B — direct-switch feasibility

Location: a throwaway extension to `modules/mod-rts-spike/` on its own job branch

Scope: prove or reject the stock-client control-transfer path; do not build the
production switch feature here

- [ ] Add a GM-only switch command for one alive, same-map, visible, grouped managed
  bot and a separate release command.
- [ ] Validate controller, subject, group, map, transport, teleport, death, and existing
  control state before changing anything.
- [ ] Exercise the native ordering required by client control: viewpoint, active mover,
  then `SetClientControl`.
- [ ] Prove that normal client movement controls the bot and that releasing restores
  the original character's mover, camera, and input without relogging.
- [ ] Route and verify selection, melee, and one ordinary known spell against the bot
  actor. Movement success alone is not proof of playable possession.
- [ ] Test whether the stock client can display the bot's known spells, action bars,
  cooldowns, cast results, and basic character state, then restore the owner's UI.
- [ ] Determine whether action-bar edits can be saved to the bot rather than the owner.
- [ ] Keep the original character safe and stationary for this spike. Do not attach the
  full playerbot brain until the unattended-body policy is chosen.
- [ ] Kill the possessed bot and verify direct possession is force-released to the
  original character with the correct mover, viewpoint, input, and death-state UI.
- [ ] Kill the unattended original character and verify possession continues, its death
  is visible to the controller, and an explicit switch back restores the real dead or
  ghost state without fabricating a resurrection.
- [ ] Force a clean return to the original character on explicit release, bot death,
  either logout, teleport or map change, group removal, failed switch, and module or
  server shutdown where applicable.
- [ ] Record exactly which parts work with the stock retail client and which, if any,
  require an addon, core seam, or modified protocol.
- [ ] Rebuild RelWithDebInfo `worldserver`.

Acceptance:

- [ ] Switch into one party bot, move it with ordinary controls, select a target, swing,
  and cast one of the bot's known spells.
- [ ] Display the bot's usable controls without permanently overwriting the owner's UI.
- [ ] Switch back and verify the original character's mover, viewpoint, bars, spells,
  and input are restored.
- [ ] Every forced exit returns control safely; no actor remains double-controlled or
  abandoned.
- [ ] Original-character death alone does not eject the controller from a living
  possessed bot; switching back while the original is dead remains safe and playable.
- [ ] Write a stock-client go/no-go result before Phase 1A chooses the unattended-player
  adapter or S1 production work begins.

## Phase 1A — shared commandable-player movement and RTS latch

Location: `modules/mod-playerbots/` plus the smallest separately agreed adapter for the
unattended original character

Scope: a subject-neutral command-state contract, a managed-playerbot adapter, and a
proven path for the player's original character; no product UI or order translator

### Decisions before implementation

- [ ] Resolve the applicable open product decisions above using the Phase 0B result.
- [ ] Confirm whether RTS entry immediately holds every eligible unit or command state
  begins on the first accepted order.
- [ ] Confirm manual combat and loot for the first commanded version.
  - Recommended starting behavior: no autonomous target acquisition, retaliation,
    chase, or loot while commanded. Phase 1B adds explicit attack and loot after the
    movement API is stable.
- [ ] Choose the visible multi-human commander rule and concrete invalidation events.
  - Do not add an arbitrary elapsed-time timeout. Arrival and hold remain commanded
    indefinitely until explicit release or a real lifecycle invalidation.
- [x] Keep stop/hold distinct from release.
  - `stop` cancels movement at the current location and enters hold. `hold` retains the
    controller and suppresses ordinary autonomy. `release` removes command control and
    restores the applicable normal controller.
- [ ] Confirm death recovery and the post-resurrection state.
  - Death suspends RTS movement while the existing packet-driven release-spirit,
    ghost-walk, reclaim, and recovery flow runs. Clear the active move, attack, or loot
    payload at death; retain RTS association and return alive in hold rather than
    resume a stale order.

### Implementation

- [ ] Define a narrow public commandable-`Player` contract without exposing
  `PlayerbotRecord` or making `mod-rts` depend on playerbot internals.
- [ ] Add a managed-playerbot lookup and adapter that reuses `PlayerbotWalker` rather
  than creating another movement system.
- [ ] Add the separately approved unattended-original-character adapter. It must not
  queue automated movement while the retail client still owns that character's input.
- [ ] Add owner-aware move, stop/hold, and explicit release operations using a narrow
  request type carrying controller and subject identity.
- [ ] Store controller, requested destination, moving/holding/recovering state, and the
  concrete lifecycle data needed for safe cleanup. Do not add an arbitrary timeout.
- [ ] Clear incompatible autonomous work when command control begins.
- [ ] While commanded, continue required session housekeeping, death handling, and the
  selected packet walker, but suppress quest, vendor, idle-combat, target acquisition,
  chasing, and loot unless a later explicit order or selected stance permits them.
- [ ] Clear the active command and suspend command movement across death while normal
  release-spirit and corpse recovery runs, then apply the chosen post-resurrection
  state without releasing RTS control accidentally.
- [ ] Stop movement without restoring autonomy for stop/hold. Release safely on
  explicit release and the chosen logout, group, map, controller, and subject events.
- [ ] Log command start, redirect, arrival, hold, release, refusal, recovery, and
  invalidation without a per-tick flood.
- [ ] Add focused automated coverage for subject lookup, controller authorization,
  latch transitions, redirect, stop/hold, release, invalidation, and death suspension.
- [ ] Extend `mod-rts-spike` with a test-only command that calls the new API for up to
  five grouped bots and assigns five simple, explicit offsets. This is only the scale
  harness; product formation remains Phase 2 work.
- [ ] Extend the harness to include the unattended original character as a commandable
  subject once Phase 0B has proved the required control handoff.
- [ ] Rebuild RelWithDebInfo `worldserver`.

### Acceptance gate

- [ ] Order one bot to a valid point; it arrives and stays instead of resuming quest
  work.
- [ ] Redirect that bot mid-walk; it takes the new walk without competing autonomy.
- [ ] In the RTS test state, order the original character to move, redirect, stop, and
  hold without simultaneous human movement packets.
- [ ] Use the throwaway spike harness to order five grouped bots to five formation
  offsets, then redirect all five mid-walk.
- [ ] Add the original character to the scale test and confirm every subject arrives
  without packet floods, walker corruption, stacking, or autonomy replacing the order.
- [ ] Confirm the selected manual policy does not retaliate, chase, or loot without an
  explicit order.
- [ ] Kill a commanded bot and confirm automatic spirit/corpse recovery does not require
  ghost commands or silently restore quest autonomy.
- [ ] Confirm explicit release restores bot autonomy. Confirm that exiting RTS, rather
  than individually releasing the original body, restores normal player control.
- [ ] Kill the unattended original character and confirm release-spirit, ghost walk,
  corpse recovery, and return-to-hold preserve the RTS association without requiring
  commander control of the ghost.

Do not start Phase 1B until this gate passes.

## Phase 1B — explicit attack and loot adapters

Location: `modules/mod-playerbots/` plus the Phase 1A unattended-character adapter

Scope: a separate backend job that adds explicit selected-subject actions; no command
parser, addon UI, autonomous combat stance, or automatic nearby loot

### Decisions before implementation

- [ ] Decide what an explicit attack does after its target dies or becomes invalid.
  - Recommended behavior: stop combat and return the subject to commanded hold. Do not
    acquire another target automatically.
- [ ] Decide whether explicit loot means take every normally clickable item and coin or
  open the loot window for later item-level orders.
  - Recommended starting behavior: use the existing playerbot take-all packet flow for
    that explicitly named corpse or object, then return to hold.

### Implementation

- [ ] Add subject-neutral, owner-aware attack and loot operations to the shared command
  service. `mod-rts` must translate requests, not implement action behavior.
- [ ] Add a managed-playerbot attack adapter that reuses the existing selection,
  facing, walk-to-range, swing, and known-spell packet paths.
- [ ] Add a managed-playerbot loot adapter that reuses normal eligibility, interact
  range, walk, `CMSG_LOOT_UNIT` or gameobject use, take, and release packet paths.
- [ ] Add equivalent unattended-original-character adapters through the safe controller
  seam selected in Phase 1A; never send simultaneous human and automated input.
- [ ] Keep the ordered target explicit. Do not select nearby enemies, assist the group,
  chase another target, or loot an unrelated corpse.
- [ ] On completion, refusal, or target loss, return to commanded hold without restoring
  quest, vendor, combat, or loot autonomy.
- [ ] Log accepted action, refusal, target loss, completion, and return-to-hold without
  per-tick flood.
- [ ] Add focused authorization, target-validation, state-transition, packet-request,
  and no-retarget/no-unrelated-loot coverage.
- [ ] Rebuild RelWithDebInfo `worldserver`.

### Acceptance gate

- [ ] Order one managed bot and the unattended original character to attack the same
  valid target separately; only the selected subject acts.
- [ ] After the target dies, each commanded subject holds and does not acquire another
  enemy.
- [ ] Explicitly order one selected subject to loot that target; no unselected subject
  and no unrelated corpse is looted.
- [ ] Confirm every action uses existing player-like packet paths without AntiDOS flood,
  direct server attack calls, direct spell preparation, or loot shortcuts.

Do not mark Phase 2 complete until this gate passes. The movement-only translator may
begin after Phase 1A if Phase 1B remains a separate in-progress job.

## Phase 2 — `mod-rts`, the order translator

Location: new `modules/mod-rts/`

Scope: additive translator over the shared commandable-player service; no movement
engine and no new core call site without a separately discussed seam

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
- [ ] Accept an addon-provided subject GUID list containing the original character and
  eligible group bots. Implement the empty-list behavior chosen in Phase 1A.
- [ ] Implement the MVP commands:
  - [ ] `rts mode on`
  - [ ] `rts mode off`
  - [ ] `rts move <x> <y> <z>`
  - [ ] `rts stop`
  - [ ] `rts release`
  - [ ] `rts state`
- [ ] Add explicit selected-unit commands after move/stop/release are reliable:
  - [ ] `rts attack <target-guid>`
  - [ ] `rts loot <target-guid>`
- [ ] Keep attack and loot player-like: use the existing selection, swing, cast, walk,
  interact-range, and loot packet paths rather than direct server actions.
- [ ] Validate command ownership, group membership, map, destination, rate, and RBAC
  before calling the shared command service.
- [ ] Apply `rts release` only to subjects that can return to another controller while
  free view remains active. `rts mode off` exits the RTS session and restores ordinary
  human control to the original character.
- [ ] Route the spell hook and direct chat/SOAP move command through the same validated
  move service.
- [ ] Take the ground-order map from the commander, never the destination object.
- [ ] Add deterministic formation offsets so multiple bots do not stack.
- [ ] Return batched commandable-unit position and command state at about 2 Hz.
- [ ] Add `rts hold` and `rts follow <name>` after move/stop/state pass.
- [ ] Add a `.conf.dist` only for genuine runtime policy; keep player-model numbers as
  code constants unless a knob is justified.
- [ ] Add automated command parsing, scope, rate-limit, and formation tests.
- [ ] Rebuild RelWithDebInfo `worldserver`.

Acceptance:

- [ ] Chat, addon channel, and SOAP reach the same command tree.
- [ ] A commander cannot address another human character, a non-group bot, or a subject
  on another map.
- [ ] A valid group move reaches only the shared service; managed bots still move only
  through the existing `PlayerbotWalker` adapter.
- [ ] The original character and selected bots accept explicit attack and loot orders
  without restoring unrelated autonomous combat or loot.
- [ ] A stale or missing subject context cannot turn a ground cast into an order.
- [ ] Repeated state replies and redirects remain below channel and AntiDOS limits.

## Phase 3 — Commander addon

Location: retail Lua addon distributed as a zip

- [ ] Replace the scratch UI with selection frames for the original character and
  controllable group bots.
- [ ] Add click selection, Shift-add/remove, select-all, and control groups.
- [ ] Add clean enter and exit for the commentator camera preset.
- [ ] Bind **RTS Order** to a key for reticle and `[@cursor]` ground orders.
- [ ] Send the selected subject GUIDs through the `rts` command tree and arm the
  server context before casting **RTS Order**. The cast destination comes from the
  server spell hook, not from Lua.
- [ ] Display position, commanded state, movement state, and failures from `rts state`.
- [ ] Add explicit selected-unit attack and loot controls after movement controls pass.
- [ ] Add world-map orders by sending normalized `UiMap` coordinates; convert them
  with the server's retail `UiMapAssignment` math and obtain Z from server terrain.
- [ ] Add waypoint queues only after direct move, stop, and redirect are reliable.
- [ ] Package a clean downloadable zip with setup and keybinding documentation.

Acceptance:

- [ ] Select the original character and one bot independently and move, redirect, and
  stop each; release the bot without leaving free view.
- [ ] Select five bots and preserve formation through a redirect.
- [ ] Issue explicit attack and loot orders to selected units; unselected and passive
  units do not join or loot automatically.
- [ ] Enter and leave camera mode without losing command-channel replies.
- [ ] Exit RTS mode and restore normal client control to the original character; do not
  offer an individual release action that would compete with the active free camera.
- [ ] Reloading the UI clears or reconstructs selection without leaving stale commands.

## Phase 4 — streaming viewpoint

Location: `mod-rts` plus the existing viewpoint APIs

Status: independent spike may run before Phase 3 is complete

Gate: it does not block Phase 1A or the non-camera parts of Phase 2, but it must pass
before Phase 3 camera integration is implemented or Phase 3 is marked complete.

- [ ] Build a throwaway composition test using a real same-map world object as the
  active viewpoint while the commentator camera is detached.
- [ ] Verify whether `Player::SetViewpoint` moves or fights the client camera through
  `ActivePlayerData::FarsightObject`.
- [ ] If they compose, design the smallest module-owned viewpoint lifecycle: create,
  make active, move at a bounded rate, and clean up on exit, logout, teleport, map
  change, or module shutdown. On death, suspend/remove the viewpoint as required and
  recreate it after recovery without implicitly ending the RTS association.
- [ ] Keep the commander's original body vulnerable under ordinary game rules and
  drive it only through the chosen commandable-player adapter. Do not park it by
  assumption, teleport it, make it invulnerable, or leave the retail client competing
  for its movement.
- [ ] Reject invalid maps and coordinates; never use the viewpoint as a teleport.
- [ ] Verify creatures and terrain stream around the camera position while the
  commentator camera remains controllable.
- [ ] Kill the unattended original character, complete automatic spirit/corpse recovery,
  and verify the viewpoint and RTS association return in the chosen hold state.
- [ ] If they conflict, stop and choose a new camera/streaming design before product
  implementation. Do not silently fall back to a vehicle or injected client.
- [ ] Rebuild RelWithDebInfo `worldserver` for any C++ version of the spike or product
  viewpoint.

Exit result: camera control and server visibility follow the commander view together,
and every exit path restores the normal viewpoint.

## S1 — production direct switch/possession

Location: the smallest module and explicitly discussed core seams selected by Phase 0B

Scope: a sibling control mode outside RTS free view; it shares controller identity and
commandable-player lifecycle with Phase 1A but has its own input and UI routing

Gate: Phase 0B has a go result, the client boundary is explicitly selected, and the
unattended original-character policy is decided. Do not infer production feasibility
from mover-only success.

- [ ] Add an authorized same-map switch request for an eligible grouped managed bot.
- [ ] Store one controller-to-subject pairing and reject double control, implicit
  takeover, transport/teleport conflicts, and bot-to-bot switch without a clean release.
- [ ] Suspend the possessed bot's autonomous controller, stop incompatible movement,
  and close incompatible interaction state before granting client input.
- [ ] Transfer viewpoint, active mover, and client control in the order proven by the
  spike, then confirm the controlled bot through normal active-mover handling.
- [ ] Route movement, selection, melee, spell, item, loot, aura-cancel, and channel
  inputs to the controlled bot using the approved client boundary.
- [ ] Present the bot's known spells, action bars, cooldowns, cast results, equipment,
  bags, money, and other agreed player-state UI. Decide whether bar edits are read-only
  or persist to the bot; never save them onto the owner's character accidentally.
- [ ] Attach the selected unattended controller to the original character. Do not
  silently grant it full quest, combat, or loot autonomy beyond the chosen policy.
- [ ] When the unattended original character dies, clear its active task, report the
  death to the controller, keep direct possession active, and run only the selected
  player-like corpse-recovery policy.
- [ ] Allow explicit release back to a dead or ghost original character and restore its
  real death state and normal death controls; do not fabricate life to make release
  convenient.
- [ ] Distinguish release back to the original character from transition into RTS free
  view. Both paths must restore the correct mover, viewpoint, controls, and AI owners.
- [ ] Force a safe release on possessed-bot death, controller logout, bot logout,
  group removal, teleport or map change, invalid state, and shutdown.
- [ ] Ignore or reject late packets for the formerly controlled bot after forced release.
- [ ] Add focused state-transition, authorization, actor-routing, UI-restore, and forced
  cleanup coverage.
- [ ] Rebuild RelWithDebInfo `worldserver`.

Acceptance:

- [ ] Switch from the original character to one party bot and play that bot with normal
  movement, targeting, melee, spells, items, and the agreed UI state.
- [ ] The original character follows the chosen unattended policy and never receives
  simultaneous human and automated input.
- [ ] Kill the unattended original character while the possessed bot remains alive;
  possession continues, the death is visible, and its active task does not resume.
- [ ] Switch back while the original character is dead or a ghost and verify its normal
  death UI, mover, viewpoint, and recovery controls are usable.
- [ ] Kill the possessed bot and verify forced release restores the original character
  as the client actor without losing either character's real death state.
- [ ] Switch back without relogging; both characters restore their correct controller,
  bars, spell state, position, and autonomy.
- [ ] Transition between direct switch and RTS free view only through explicit release
  states; no actor remains abandoned or controlled twice.
- [ ] Every forced-exit case restores a playable original character.

## Phase 5 — evryOps touchpoints

Location: `D:\WOWEmulation\Emulators\Tools\evryOps`

- [ ] Expose `mod-rts.conf` through the existing Conf page if the module gains config.
- [ ] Add read-only live bot pins to the existing Map page.
- [ ] Host the packaged addon zip when client-asset distribution is built.
- **Skipped:** VehicleSeat overlay editor; the vehicle camera rung did not win.
- **Never:** movement buttons, task assignment, or any other web control plane.

## Deferred vocabulary and polish

- `rts follow <name>` and `rts hold` follow the move/stop MVP.
- Passive, self-defense, assist, and aggressive are future Commander combat policies.
  Do not let the existing playerbot combat brain become an accidental default stance.
- Optional auto-loot-in-reach follows reliable explicit `rts loot`; it is not the
  commanded default.
- World-map orders follow in-world reticle orders.
- Waypoint queues follow reliable direct redirects.
- Heals, buffs, class stance logic, interrupts, pets, and class rotations remain
  separate playerbot jobs, not Commander prerequisites. Commander combat posture is a
  control policy, not class stance logic.
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
