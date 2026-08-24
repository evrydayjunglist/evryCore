# Commander Mode

Living implementation plan and work tracker for three related control modes on the
retail client: ordinary manual play, direct switch/possession of a party bot, and an
RTS free view that commands the player's original character alongside party bots.

Last updated: 24 August 2026

Source plan: `Commander Mode.pdf`, revised 22 August 2026

This file is both the plan and the tracker.
[Playerbots architecture](PLAYERBOTS_ARCHITECTURE.md) is the authoritative wider design
for the reserve, Builtin, and External playerbot brains and the C# executable stack.
Keep the root `README.md` to a short job summary, and keep experimental evidence in
`modules/mod-rts-spike/README.md` until the spike module is deleted.

## Status

| Phase | State | Exit condition |
| --- | --- | --- |
| 0A. Commander client feasibility | **Complete** | Camera, command channel, ground order, cloned spell, and vehicle-seat tests pass |
| 0B. Direct-switch feasibility | **Complete — live no-go confirmed, custom boundary selected, throwaway switch code removed** | Stock viewpoint and active mover work, but selection, casts, and visible player UI remain on the session owner; production possession will use a capability-negotiated custom client/protocol boundary |
| 1A. Shared commandable-player movement | **Backend implemented — stock-client input custody blocks acceptance** | The player body and managed bots use one move/hold/release contract; the original cannot also move from ordinary client input; remaining one- and five-bot lifecycle tests pass |
| 1B. Explicit attack and loot adapters | Waiting on client input custody and Phase 1A | Manual attack and loot reach player-like packet paths for the player body and managed bots |
| 2. `mod-rts` translator | Waiting on Phases 1A and 1B | Guarded group orders reach shared adapters with no action or movement engine in `mod-rts` |
| 3. Commander addon | Waiting on Phase 2; camera completion also waits on Phase 4 | Selection, camera mode, reticle orders, state display, and clean exit work together |
| 4. Streaming viewpoint | Spike pending; does not block Phase 1A | A real active viewpoint streams the camera area without fighting the commentator camera |
| S1. Direct switch product | Commander client foundation is next; possession behavior follows Phase 1A | The client can play an authorized party bot while the original character is safely AI-driven, then restore both |
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
in the evryOps repository. The order-spell data cleanup is also a separate content job
with its own content commit.

## Goal and boundaries

Commander Mode is a control layer over the existing retail client and player-like
movement. Selection remains client-side. Managed bots continue to use the same
`PlayerbotWalker` used by autonomy, so there is one movement engine rather than two.
The player's original character is also a commandable RTS subject; its unattended
driver must be proven safe before Phase 1A chooses an adapter. Controller arbitration
keeps human client input and automated packets mutually exclusive for each `Player`.

Commander is one controller family over the embodied runtime defined in
`PLAYERBOTS_ARCHITECTURE.md`. Without `playerbots.exe`, bots may remain RTS-ready in
reserve and execute bounded local orders or stances. With the executable, External
autonomy may choose their long-horizon lives. RTS and direct control preempt that
strategic controller, while real-time orders remain in the worldserver control path.

Safety and selected design boundaries:

- The current RTS harness uses the proven stock `TrinityCore` addon-command channel.
  The next job is a narrow, version-pinned Commander client boundary for RTS input
  custody as well as later direct possession. It must negotiate capability, park direct
  body input while RTS owns the original, and restore ordinary input on every exit.
- Client patching, injection, reverse engineering, a purpose-built client, and companion
  tooling are available options for the dedicated client job. Choose from measured
  evidence and keep the resulting path safe, supportable, and explicit.
- The selected camera design uses commentator mode; the vehicle-seat experiment remains
  evidence rather than the current camera implementation.
- Runtime orders originate in the game-side Commander client; evryOps bot pins remain a
  read-only operator view under the current architecture.
- Commanded action remains player-valid: preserve costs, geometry, lifecycle, and server
  authority, using the existing walker and packet-equivalent action paths. Shortcuts that
  bypass those invariants fail the project's non-cheaty, non-hacky safety line.
- Keep strategic planning off the real-time Commander path. `playerbots.exe` may receive
  controller changes and later replan, but `mod-rts` calls the shared worldserver skill
  runtime directly.
- A commander may address only their own original character and eligible bots in their
  own group and on the same map.
- The original character remains the same real `Player`, account character, group
  member, and lifecycle subject while RTS owns it. It is not converted into a managed
  bot. The shared commandable runtime drives it as an RTS subject, and WASD must not
  concurrently drive the body.
- Use the caster's map for a ground order. Client-sent destinations contain XYZ but
  retain `MAPID_INVALID`.

## Confirmed design

### Control modes

```text
Normal play
  client -> original character
  applicable Reserve, Builtin, or External controller -> party bots

Direct switch
  client -> selected party bot
  selected unattended policy -> original character
  applicable baseline controller -> remaining party bots

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
  Builtin or External playerbot work, unattended-body work, and RTS orders must have
  explicit, non-overlapping transitions.
- Acquiring RTS or direct control quiesces the previous controller, increments the
  subject generation, and rejects its late work. Releasing a bot restores its
  applicable baseline controller. External autonomy receives fresh state and replans;
  it does not resume a stale target or action.
- An RTS claim on the original is not exclusive until the connected client has yielded
  gameplay input. The 24 August live test showed that `SetClientControl(player, false)`
  greys the action bar but leaves the original as Trinity's active moved unit, so stock
  WASD packets are still accepted. The custom client must park those packets, and the
  server must independently reject network-origin gameplay packets that conflict with
  the active claim while still accepting generation-authorized internal RTS packets.

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

- [x] Decide the direct-switch client boundary.
  - Selected 23 August 2026 after the stock-path no-go: a capability-negotiated custom
    client/protocol boundary comparable in responsibility to SuperUI. It must carry
    typed switch/release and actor-input requests, actor-identified UI state, explicit
    acknowledgements, and forced-release reasons while worldserver authorizes and
    revalidates every action.
  - Capability negotiation keeps unsupported clients on their ordinary path. The next
    job is the narrow version-pinned Commander component and input-custody contract.
    Its lowest-level implementation technique remains evidence-driven: patching,
    injection, reverse engineering, companion tooling, or another supportable approach.
- [ ] Decide the unattended original-character policy during direct switch.
  - Choices include hold, follow/assist/defend, the Builtin controller, the External
    controller, or the shared RTS command service. Begin with the least autonomous safe
    behavior that matches how the owner wants to play.
- [x] Decide what entering RTS mode does to eligible units.
  - Selected 24 August 2026: entering RTS immediately claims the commander's original
    character and every eligible online grouped managed bot in the same compatible map
    instance. Every claimed unit begins in commanded hold.
- [x] Decide whether the original character may be individually released while RTS
  free view remains active.
  - Selected 24 August 2026: the original character cannot be individually released
    while RTS free view remains active. Exiting RTS restores ordinary human control.
    Releasing a bot restores its applicable Reserve, Builtin, or External baseline.
- [x] Confirm the initial RTS combat and loot policy.
  - Selected 24 August 2026: commanded units perform movement only. They do not
    autonomously acquire targets, retaliate, chase, attack, or loot. Explicit attack
    and loot belong to the next backend job.
- [ ] Choose the default Reserve and initial RTS stance.
  - Reserve means no self-chosen long-term activity, not necessarily inert combat.
    Passive stays inert. Defensive responds only to legitimate attackers with a bounded
    leash and return. Each selected stance explicitly defines whether any Builtin combat
    behavior applies.
- [x] Decide Coordinator presence loss during active RTS or direct control.
  - Selected 24 August 2026: loss of the owning `playerbots.exe` connection in
    Coordinator login mode does not log out a managed bot under a valid RTS claim. RTS
    works without the executable. Normal Coordinator absence and grace behavior resumes
    after RTS exits and the claims are safely released. This connection is not the
    future External strategic heartbeat.
- [x] Decide original-character identity and movement ownership in RTS free view.
  - Selected 24 August 2026: the original remains the real original character, but its
    active controller is RTS and the same commandable runtime drives it like any other
    RTS subject. Ordinary WASD body movement is disabled until RTS exit restores normal
    human control. The action bar, attacks, spells, items, and interactions require an
    explicit client-input policy in the client job; no stock packet should be assumed
    harmless merely because movement was parked.
- [x] Decide commander arbitration when more than one human is in the group.
  - Selected 24 August 2026: one commander may own RTS claims for a group, and that
    commander must be the current group leader. A leadership change that removes that
    authority invalidates the claim.
- [x] Decide the post-resurrection RTS state.
  - Selected 24 August 2026: death clears active movement but retains RTS ownership.
    Existing packet-driven release-spirit, ghost walk, corpse reclaim, and recovery run
    normally. The unit returns alive in commanded hold and never resumes a stale move.
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
evryCore keeps its existing queued release-spirit, ghost-walk, and reclaim packets;
direct `BuildPlayerRepop`, ghost teleport, or `ResurrectPlayer()` would violate the
player-valid and non-cheaty safety outcome.

The earlier suggested 15-minute stale-command timeout is withdrawn. No arbitrary
elapsed-time release is selected. A legitimate hold must be able to last indefinitely.
Explicit RTS exit, commander or subject logout, group removal, incompatible map or
instance, subject destruction, or shutdown invalidates the applicable claim. A
same-map teleport suspends movement and requires authoritative revalidation afterward;
an incompatible map or instance releases the claim. The renewable External strategic
lease and optional Coordinator presence lease prove process health. Commander holds
remain active across those lease renewals until explicit release or lifecycle
invalidation.

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

Current branch sequence:

1. Phase 0A and the cleaned Phase 0B evidence are already on `evry`.
2. Merge `job/rts-commandable-movement` into `evry` as the implemented backend with its
   remaining live-acceptance limits documented; do not call Phase 1A complete.
3. Create a fresh client job from updated `evry` for the versioned Commander capability,
   RTS input custody, exit restoration, and the reviewed packet-origin enforcement seam.
4. After that client boundary lands, use a fresh bounded Phase 1A follow-up for the
   remaining original, five-bot, lifecycle, death, teleport, map, and Coordinator-loss
   acceptance and any fixes those tests expose.
5. Begin Phase 1B only after the full Phase 1A gate passes. Branch the later possession
   product from `evry` after its shared client and controller prerequisites have landed.

Keep each job disposable and single-purpose rather than stacking an umbrella branch.

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

Scope: prove or reject the stock-client control-transfer path. Production switch work
belongs to S1 after the boundary design is selected.

Current source result, 23 August 2026: **no-go for playable possession on the current
stock-server input path.** `SetClientControl`/`SetMovedUnit` can establish a stock
viewpoint and active mover, and movement handlers validate against that mover. The
selection, melee, cast, and action-button handlers instead mutate the session owner's
`Player`. Stock known-spell, action-button, cooldown-history, and charge packets carry
no actor identity; they can be sent as a visual probe, but incoming bar edits still
belong to the owner. A production path would need an explicitly approved server
actor/UI-routing seam, a selected client/protocol boundary, or no direct-possession
feature. This is a Phase 0B limit and result, not a permanent ban on any option.

The bounded branch harness implemented one GM/controller pair, a spike-only helper that
queued the managed bot's stock party-invite acceptance packet, managed-bot arbitration,
owner-bar snapshot and rollback, native control ordering, packet/state logs, and forced
release hooks. It built and its focused state tests passed before the live run. Cleanup
then removed the invite and switch commands, playerbot quiescing flag, state helper, and
focused tests because no production control seam was selected. The packet-driven
`acceptinvite` helper was later restored by itself to bootstrap groups for Phase 1A
testing; no direct-switch code was restored. The live
playable-possession gate and explicit-release case were measured; unchecked live items
below remain untested rather than passed.

The checklist below records the measured branch rather than commands available in the
current tree.

Live result, 23 August 2026: Magey's camera and active mover switched to Opai. The
client acknowledged Opai's GUID in active-mover, heartbeat, and fall-land packets, but
ordinary walking was not established. Selection packets still mutated Magey, spell
`2000001` and Blizzard resolved with Magey as caster, and sending Opai's 115 known
spells plus 5 action buttons did not replace Magey's visible controls. Explicit release
returned Magey to normal without relogging and Opai resumed Builtin movement. This
confirms the stock-path no-go. Melee, action-bar editing/persistence, both death cases,
and the remaining forced exits were not tested.

- [x] Add a GM-only switch command for one alive, same-map, visible, grouped managed
  bot and a separate release command.
  - Live: `.rtsspike switch Opai` reached active and `.rtsspike release` restored Magey.
- [ ] Validate controller, subject, group, map, transport, teleport, death, and existing
  control state before changing anything.
- [x] Exercise the native ordering required by client control: viewpoint, active mover,
  then `SetClientControl`.
  - Live: camera moved to Opai and the client acknowledged Opai as active mover.
- [ ] Prove that normal client movement controls the bot and that releasing restores
  the original character's mover, camera, and input without relogging.
- [ ] Route and verify selection, melee, and one ordinary known spell against the bot
  actor. Movement success alone is not proof of playable possession.
- [ ] Test whether the stock client can display the bot's known spells, action bars,
  cooldowns, cast results, and basic character state, then restore the owner's UI.
- [ ] Determine whether action-bar edits can be saved to the bot rather than the owner.
- [ ] Keep the original character safe and stationary for this spike. Its unattended
  controller remains unassigned until the unattended-body policy is chosen.
- [ ] Kill the possessed bot and verify direct possession is force-released to the
  original character with the correct mover, viewpoint, input, and death-state UI.
- [ ] Kill the unattended original character and verify possession continues, its death
  is visible to the controller, and an explicit switch back restores the real dead or
  ghost state without fabricating a resurrection.
- [ ] Force a clean return to the original character on explicit release, bot death,
  either logout, teleport or map change, group removal, failed switch, and module or
  server shutdown where applicable.
- [x] Record exactly which parts work with the stock retail client and which, if any,
  require an addon, core seam, or modified protocol.
  - Live no-go: viewpoint/active mover worked; selection, casts, and visible player UI
    remained Magey's. The owner subsequently selected a capability-negotiated custom
    client/protocol boundary comparable in responsibility to SuperUI.
- [x] Rebuild RelWithDebInfo `worldserver`.
  - 23 August 2026: `worldserver.exe` linked successfully with static
    `mod-playerbots` and `mod-rts-spike`; the focused state tests also passed.

Acceptance:

- [ ] Switch into one party bot, move it with ordinary controls, select a target, swing,
  and cast one of the bot's known spells.
- [ ] Display the bot's usable controls without permanently overwriting the owner's UI.
- [x] Switch back and verify the original character's mover, viewpoint, bars, spells,
  and input are restored.
  - Live: explicit release returned Magey to normal without relogging, found no owner
    bar differences, and resumed Opai's Builtin controller.
- [ ] Every forced exit returns control safely; no actor remains double-controlled or
  abandoned.
- [ ] Original-character death alone does not eject the controller from a living
  possessed bot; switching back while the original is dead remains safe and playable.
- [x] Write a stock-client go/no-go result before Phase 1A chooses the unattended-player
  adapter or S1 production work begins.
  - **No-go** for playable possession on the current stock-server path.

## Phase 1A — shared commandable-player movement and RTS latch

Location: `modules/mod-playerbots/` plus the smallest separately agreed adapter for the
unattended original character

Scope: a subject-neutral command-state contract, a managed-playerbot adapter, and a
proven path for the player's original character; no product UI or order translator

### SuperUI comparison and selected RTS posture

Source inspection on 24 August 2026 found that SuperUI layers control over companion
AI rather than replacing that AI with a persistent RTS ownership state:

- `SuiPossess::HandleOrder` authorizes the sender and each supplied subject when an
  order arrives, then routes move and attack through the existing `AiBotAI` task paths.
  Ordinary ordered group bots do not receive a group-wide exclusive controller claim
  or controller generation comparable to evryCore's command contract.
- A normal grouped `AiBotAI` follows its assigned party human or boss and assists the
  party in combat. An active `TASK_MOVE_TO` temporarily takes priority over formation
  follow, but arrival clears that task. `ORDER_STOP` clears movement, attack, and the
  current task; a later AI tick may therefore resume normal follow or combat-assist
  behavior. The free-view-commanded possessed bot is a special case: formation follow
  and autonomous goal selection are suppressed, but combat assist deliberately remains
  live.
- SuperUI possession is separate from ordinary group orders. A relevant teleport or
  the possessed bot's death force-releases possession and restores the original client
  actor. Death of the unattended original character does not force-release possession.
- SuperUI does not turn the original account character into a fabricated bot. While the
  human drives a bot or uses free view, `AttachToRealCharacter` attaches the same
  `AiBotAI` to that existing `Player`; its dummy bot entry is only AI-internal state.
  Leaving free view detaches that AI and restores manual control.
- SuperUI's wire contract makes the custom client responsible for input custody. Free-
  view release mode keeps the own character autonomous and tells the client to flush a
  stop and park its movement stream. The server-side order path then assumes the
  detached camera sends no character movement. Its one-second movement rejection is
  only a forced-release drain for in-flight possessed-bot packets, not a continuous
  free-view input gate.
- The reference shelf contains SuperUI's protocol and server implementation, but not
  the `MSUIClient` source. The parked-stream contract and server assumption are verified;
  the exact client key-routing implementation is not available for inspection here.

The selected evryCore behavior intentionally diverges toward a stricter RTS model. The
owner wants Commander Mode to behave as much like an RTS as practical and confirmed the
current Phase 1A posture:

- RTS entry exclusively claims the original character and every eligible grouped
  managed bot for one group-leader commander, with controller identity and generation
  barriers for stale work.
- Move completion, movement refusal, and stop all enter persistent commanded hold.
  Builtin quest, vendor, follow, combat, and loot behavior remains suppressed until an
  explicit RTS directive or release changes that state.
- Future follow, assist, attack, loot, guard, patrol, or stance behavior must be an
  explicit command or deliberately selected RTS policy. It must not appear implicitly
  because an old Builtin companion loop resumed after an order.
- Death and a compatible same-map teleport suspend movement, advance the generation,
  and return the subject to commanded hold after packet-driven recovery or authoritative
  revalidation. An incompatible map or instance still invalidates the claim.
- The original keeps its original identity and lifecycle while using this same RTS
  controller and walker. Unlike SuperUI, evryCore will not rely only on the client to
  park input: the selected production boundary also needs server-side packet-origin
  enforcement so network input and internal RTS work cannot drive it simultaneously.

This is an intentional product distinction, not missing SuperUI parity. Reference
evidence lives in `SuperUiContent/SuiWorld/CRPG/SuiPossess.cpp` (`HandleOrder`,
`OnPlayerTeleport`, and `OnPlayerDeath`) and `SuperUiContent/SuiBots/AiBotAIMain.cpp`
(`DoPartyFollow`, the possessed update gate, and `TASK_MOVE_TO`).

### Decisions before implementation

- [x] Resolve the applicable open product decisions above using the Phase 0B result.
- [x] Confirm that RTS entry immediately claims every eligible unit in commanded hold.
- [x] Confirm movement-only behavior with no autonomous combat or loot for the first
  commanded version. Phase 1B adds explicit attack and loot after this API is stable.
- [x] Restrict ownership to one commander per group, initially the current group leader,
  and invalidate claims on the concrete lifecycle events recorded above. Arrival and
  hold remain commanded indefinitely; elapsed time alone leaves ownership unchanged.
- [x] Keep stop/hold distinct from release.
  - `stop` cancels movement at the current location and enters hold. `hold` retains the
    controller and suppresses the baseline controller. `release` removes command
    control and restores the applicable Reserve, Builtin, or External baseline.
- [x] Confirm death recovery and the post-resurrection state.
  - Death suspends RTS movement while the existing packet-driven release-spirit,
    ghost-walk, reclaim, and recovery flow runs. Clear the active move payload, retain
    RTS ownership, and return alive in hold rather than resume a stale order.

### Implementation

- [x] Define a narrow public commandable-`Player` contract without exposing
  `PlayerbotRecord` or making `mod-rts` depend on playerbot internals.
- [x] Add a managed-playerbot lookup and adapter that reuses `PlayerbotWalker` rather
  than creating another movement system.
- [x] Add the separately approved unattended-original-character adapter. It queues
  automated movement only while automation owns that character's controller claim.
  Because Trinity normally excludes the apparent sender from a client movement update,
  the adapter also mirrors each accepted movement state to the owning client and waits
  for authoritative feet synchronization before reporting arrival.
- [ ] Add production client-input custody for an RTS-claimed original. The custom client
  must park direct body movement and restore it on exit; a narrow server seam must
  distinguish network-origin gameplay packets from authorized internal RTS packets and
  enforce the current controller generation.
- [x] Add owner-aware move, stop/hold, and explicit release operations using a narrow
  request type carrying controller and subject identity.
- [x] Store controller identity and generation, requested directive, moving/holding/
  recovering state, and the concrete lifecycle data needed for safe cleanup. Ownership
  expires through explicit release or a concrete lifecycle invalidation.
- [x] Quiesce the previous controller at a safe boundary, clear incompatible Builtin or
  External work, and reject late work from the old generation before command control
  begins.
- [x] While commanded, continue required session housekeeping, death handling, and the
  selected packet walker, but suppress quest, vendor, idle-combat, target acquisition,
  chasing, and loot unless a later explicit order or selected stance permits them.
- [x] Clear the active command and suspend command movement across death while normal
  release-spirit and corpse recovery runs, then apply the chosen post-resurrection
  state without releasing RTS control accidentally.
- [x] Stop movement without restoring the baseline for stop/hold. Release safely to the
  applicable Reserve, Builtin, or External controller on explicit release and the
  chosen logout, group, map, controller, and subject events.
- [x] Log command start, redirect, arrival, hold, release, refusal, recovery, and
  invalidation without a per-tick flood.
- [x] Add focused automated coverage for controller authorization, exclusive ownership,
  generation barriers, latch transitions, redirect, stop/hold, release, invalidation,
  death suspension/recovery, original restoration, and Coordinator presence pinning.
- [x] Extend `mod-rts-spike` with a test-only command that calls the new API for up to
  five grouped bots and assigns five simple, explicit offsets. This is only the scale
  harness; product formation remains Phase 2 work.
- [x] Extend the harness to include the unattended original character as a commandable
  subject only after the Phase 0B boundary decision defines a safe ownership contract;
  the stock switch did not prove a playable control handoff.
- [x] Reconfigure after adding the test source, then rebuild RelWithDebInfo `worldserver`,
  `bnetserver`, `tests`, and `playerbots_executable`.

Source/build evidence, 24 August 2026: the focused commandable/playerbot filter passes
98 assertions in 18 cases; the complete C++ suite passes 411 assertions in 53 cases;
the .NET framing suite and the published-`playerbots.exe` fake-worldserver reconnect
test pass. After the first live original-character test exposed false server-side
arrival without visible owning-client movement, the module-only movement mirror and
arrival barrier were added; all four RelWithDebInfo targets rebuilt and both C++ suites
still pass. A subsequent live test visibly moved the owning client to the requested
feet and passed the synchronization barrier; redirect, stop, exit restoration, and
lifecycle coverage remain open. The owner could still move Magey with WASD while the
RTS claim was active. The greyed action bar showed the control update reached the stock
client, but `SetClientControl(player, false)` did not remove Magey as its own active
mover. That fails exclusive input ownership and makes client input custody the next job.

### Acceptance gate

- [x] Order one bot to a valid point; it arrives and stays instead of resuming quest
  work.
  - Live 24 August 2026: Opai entered hold, completed a commanded move, stopped into
    hold on the next move, and resumed Builtin quest movement only after explicit
    release.
- [ ] Redirect that bot mid-walk; it takes the new walk without competing autonomy.
- [ ] In the RTS test state, order the original character to move, redirect, stop, and
  hold without simultaneous human movement packets.
  - Basic move passed live on 24 August 2026 after the owning-client mirror fix. Magey
    visibly followed `.rtsspike command moveoriginal -43.190189 -4308.263672
    69.915924`; the log recorded synchronization at those commanded feet before arrival
    and commanded hold. The same test then failed exclusivity because ordinary WASD
    still moved Magey during the RTS claim. Redirect and explicit stop/hold still
    require live testing after input custody exists.
- [ ] Use the throwaway spike harness to order five grouped bots to five formation
  offsets, then redirect all five mid-walk.
- [ ] Add the original character to the scale test and confirm every subject arrives
  without packet floods, walker corruption, stacking, or autonomy replacing the order.
- [ ] Confirm the selected manual policy does not retaliate, chase, or loot without an
  explicit order.
- [ ] Kill a commanded bot and confirm automatic spirit/corpse recovery does not require
  ghost commands or silently restore quest autonomy.
- [ ] Confirm explicit release restores each bot's applicable Reserve, Builtin, or
  External baseline. Confirm that exiting RTS, rather than individually releasing the
  original body, restores normal player control.
- [ ] Kill the unattended original character and confirm release-spirit, ghost walk,
  corpse recovery, and return-to-hold preserve the RTS association without requiring
  commander control of the ghost.
- [ ] Same-map teleport a commanded bot and the original character. Confirm movement
  suspends, the generation changes, authoritative revalidation returns the subject to
  hold, and a stale pre-teleport move cannot resume.
- [ ] Move a claimed subject to an incompatible map or instance and confirm the
  applicable claim releases. Repeat group removal, leader transfer, subject logout or
  destruction, commander logout, explicit RTS exit, and worldserver shutdown; confirm
  each scope invalidates once and restores the applicable baseline.
- [ ] In Coordinator login mode, enter RTS, disconnect the owning `playerbots.exe`, and
  wait beyond its ordinary grace. Confirm valid claimed bots remain online and accept
  RTS movement. Exit RTS and confirm the normal Coordinator-absence logout behavior
  resumes. Treat this only as presence-loss evidence, not an External heartbeat test.

The next bounded job is the Commander client/input-custody boundary, not Phase 1B. Start
with a capability/version handshake, RTS enter/exit acknowledgements, parked body
movement, clean restoration on normal and forced exit, and fail-closed behavior for an
unsupported client build. Pair it with the smallest reviewed worldserver ingress seam
that tags packet origin and rejects conflicting network gameplay under an RTS claim.
Do not mix the client job into this movement branch. Finish the remaining Phase 1A
scale and lifecycle gate after that boundary makes the original claim genuinely
exclusive. Phase 1B begins only after the full gate passes.

## Phase 1B — explicit attack and loot adapters

Location: `modules/mod-playerbots/` plus the Phase 1A unattended-character adapter

Scope: a separate backend job that adds explicit selected-subject actions; no command
parser, addon UI, autonomous combat stance, or automatic nearby loot

### Decisions before implementation

- [ ] Decide what an explicit attack does after its target dies or becomes invalid.
  - Recommended behavior: stop combat and return the subject to commanded hold with the
    next target supplied by another explicit order.
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
  seam selected in Phase 1A, with mutually exclusive human and automated input claims.
- [ ] Keep the ordered target explicit. Nearby acquisition, group assist, target chaining,
  and unrelated corpse loot require their own selected policy or explicit order.
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

Phase 2 reaches complete after this gate passes. The movement-only translator may begin
after Phase 1A while Phase 1B remains a separate in-progress job.

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
  destination. The context is one-order input rather than permanent UI selection.
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
- [ ] Take the ground-order map from the commander; the destination object supplies XYZ.
- [ ] Add deterministic formation offsets that keep multiple bots separated.
- [ ] Return batched commandable-unit position and command state at about 2 Hz.
- [ ] Add `rts hold` and `rts follow <name>` after move/stop/state pass.
- [ ] Add patrol, guard, passive, defensive, and assist policies only after the shared
  reserve skills define their leash, return, target, and controller semantics.
- [ ] Add a `.conf.dist` only for genuine runtime policy; keep player-model numbers as
  code constants unless a knob is justified.
- [ ] Add automated command parsing, scope, rate-limit, and formation tests.
- [ ] Rebuild RelWithDebInfo `worldserver`.

Acceptance:

- [ ] Chat, addon channel, and SOAP reach the same command tree.
- [ ] Authorization accepts only the commander's own character and eligible same-map
  group bots; every other subject receives an explicit refusal.
- [ ] A valid group move reaches only the shared service; managed bots still move only
  through the existing `PlayerbotWalker` adapter.
- [ ] The original character and selected bots accept explicit attack and loot orders
  without restoring unrelated autonomous combat or loot.
- [ ] A stale or missing subject context yields an explicit refusal and no order.
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
  units remain uninvolved.
- [ ] Enter and leave camera mode without losing command-channel replies.
- [ ] Exit RTS mode and restore normal client control to the original character. While
  free camera remains active, its UI exposes only transitions compatible with that claim.
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
- [ ] Keep the commander's original body vulnerable under ordinary game rules and drive
  it through the chosen commandable-player adapter under one exclusive controller claim.
  Position and protection remain ordinary player-valid state.
- [ ] Reject invalid maps and coordinates. The viewpoint changes visibility while player
  position changes only through an authorized player-valid movement or travel path.
- [ ] Verify creatures and terrain stream around the camera position while the
  commentator camera remains controllable.
- [ ] Kill the unattended original character, complete automatic spirit/corpse recovery,
  and verify the viewpoint and RTS association return in the chosen hold state.
- [ ] If they conflict, choose and record a new camera/streaming design before product
  implementation. Vehicle and injected-client approaches remain available when evidence
  makes one of them the better safe design.
- [ ] Rebuild RelWithDebInfo `worldserver` for any C++ version of the spike or product
  viewpoint.

Exit result: camera control and server visibility follow the commander view together,
and every exit path restores the normal viewpoint.

## S1 — production direct switch/possession

Location: the smallest module and explicitly discussed core seams selected by Phase 0B

Scope: a sibling control mode outside RTS free view; it shares controller identity and
commandable-player lifecycle with Phase 1A but has its own input and UI routing

Gate: design the selected capability-negotiated custom client/protocol boundary, decide
its exact client form and the unattended original-character policy, and complete the
required safety/threat-model review. Production feasibility requires actor and UI proof
beyond mover-only success, and the 12.1 protocol receives its own audited message IDs
rather than inheriting SuperUI's Vanilla numbers.

- [ ] Add an authorized same-map switch request for an eligible grouped managed bot.
- [ ] Store one controller-to-subject pairing and reject double control, implicit
  takeover, transport/teleport conflicts, and bot-to-bot switch without a clean release.
- [ ] Suspend the possessed bot's applicable Reserve, Builtin, or External controller,
  stop incompatible movement, and close incompatible interaction state before granting
  client input.
- [ ] Transfer viewpoint, active mover, and client control in the order proven by the
  spike, then confirm the controlled bot through normal active-mover handling.
- [ ] Route movement, selection, melee, spell, item, loot, aura-cancel, and channel
  inputs to the controlled bot using the approved client boundary.
- [ ] Present the bot's known spells, action bars, cooldowns, cast results, equipment,
  bags, money, and other agreed player-state UI. Decide whether bar edits are read-only
  or persist to the bot, with the owner's bars isolated and preserved.
- [ ] Attach the selected unattended controller to the original character with exactly
  the quest, combat, and loot capabilities chosen by its policy.
- [ ] When the unattended original character dies, clear its active task, report the
  death to the controller, keep direct possession active, and run only the selected
  player-like corpse-recovery policy.
- [ ] Allow explicit release back to a dead or ghost original character and restore its
  real death state and normal death controls exactly as they exist.
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
- [ ] The original character follows the chosen unattended policy under an exclusive
  controller claim, with human and automated input separated.
- [ ] Kill the unattended original character while the possessed bot remains alive;
  possession continues, the death is visible, and its active task does not resume.
- [ ] Switch back while the original character is dead or a ghost and verify its normal
  death UI, mover, viewpoint, and recovery controls are usable.
- [ ] Kill the possessed bot and verify forced release restores the original character
  as the client actor without losing either character's real death state.
- [ ] Switch back without relogging; both characters restore their correct controller,
  bars, spell state, position, and applicable baseline.
- [ ] Transition between direct switch and RTS free view only through explicit release
  states; no actor remains abandoned or controlled twice.
- [ ] Every forced-exit case restores a playable original character.

## Phase 5 — evryOps touchpoints

Location: `D:\WOWEmulation\Emulators\Tools\evryOps`

- [ ] Expose `mod-rts.conf` through the existing Conf page if the module gains config.
- [ ] Add read-only live bot pins to the existing Map page.
- [ ] Host the packaged addon zip when client-asset distribution is built.
- **Skipped:** VehicleSeat overlay editor; the vehicle camera rung did not win.
- **Scope boundary:** evryOps remains an operator view; movement, task assignment, and
  gameplay control stay in the selected game-side Commander clients and worldserver.

## Deferred vocabulary and polish

- `rts follow <name>` and `rts hold` follow the move/stop MVP.
- Passive, defensive, guard, patrol, follow, assist, and aggressive are planned local
  policies under Reserve or RTS control. Implement them in bounded jobs after the
  shared command contract, with the default stance chosen explicitly rather than
  inherited accidentally from Builtin combat.
- Optional auto-loot-in-reach follows reliable explicit `rts loot`; it is not the
  commanded default.
- World-map orders follow in-world reticle orders.
- Waypoint queues follow reliable direct redirects.
- Heals, buffs, class stance logic, interrupts, pets, and class rotations remain
  separate playerbot jobs, not Commander prerequisites. Commander combat posture is a
  control policy, not class stance logic.
- Per-bot strategic memory and explicit party, guild, or social information sharing
  follow `PLAYERBOTS_ARCHITECTURE.md`. Omniscient shared live spawns, continent
  convenience travel, and guide-scripted brains remain out of scope.

## Evidence log

| Date | Result |
| --- | --- |
| 22 Aug 2026 | Commentator flags enabled exact, unclamped `C_Commentator` camera movement. |
| 22 Aug 2026 | Addon command channel returned replies with commentator flags on and off. |
| 22 Aug 2026 | Commander-range ground clicks followed the detached camera after range and line-of-sight constraints were removed from the order spell. |
| 23 Aug 2026 | Fresh clone `2000001` passed learn, known, usable, reticle, and cursor-cast checks. |
| 23 Aug 2026 | Vehicle seats 2 and 3 each delivered an RTS Order destination with commentator flags off. |
| 23 Aug 2026 | Live overlay inspection confirmed spell `2000001` still needs product cleanup: cast time, mana cost, global cooldown, cooldown, and area-trigger effect remain. |
| 23 Aug 2026 | Phase 0B source inspection found active-mover-aware movement but session-owner-bound selection, melee, spell, and action-bar handlers; current stock-server playable possession was a source-level no-go. RelWithDebInfo worldserver/tests built and the focused tests passed 20 assertions; the later live result is recorded below. |
| 23 Aug 2026 | First Phase 0B switch attempt stopped safely before mutation because the harness treated normal `GetViewpoint() == nullptr` as invalid. Source confirmed null is the ordinary self-view; the predicate was corrected and no live acceptance item was marked complete. |
| 23 Aug 2026 | Phase 0B live run switched Magey's camera and active mover to Opai, but selection and spell casts remained Magey's and Opai's stock spell/action packets did not replace Magey's visible UI. Explicit release restored Magey and resumed Opai's Builtin movement. Current stock-server playable possession is a live-confirmed no-go. |
| 23 Aug 2026 | Owner selected a capability-negotiated custom client/protocol boundary comparable in responsibility to SuperUI for future direct possession. Client patching, injection, reverse engineering, custom opcodes, a purpose-built client, and companion tooling are available options; exact client form, protocol design, safety proof, and unattended-original policy remain separate decisions. |
| 24 Aug 2026 | Removed the Phase 0B-only invite, switch, release, lifecycle, actor-UI probe, playerbot-quiescing, and state-test code after retaining the live no-go evidence. No production control seam was carried forward. The cleaned `worldserver`, `bnetserver`, and `tests` targets rebuilt; all remaining 362 C++ assertions in 46 cases and the .NET protocol tests passed. |
| 24 Aug 2026 | SuperUI source comparison confirmed that its ordinary group orders temporarily override companion follow/assist AI, while evryCore deliberately keeps exclusive RTS claims and persistent commanded hold. The owner selected the stricter RTS posture; automatic follow, combat, and loot must not resume after stop or arrival. |
| 24 Aug 2026 | First Phase 1A live run passed one-bot enter, move, stop/hold, and release, but the original character remained visually stationary while the internal walker falsely reported arrival. Source traced this to Trinity excluding the apparent movement sender from `SMSG_MOVE_UPDATE`. The module now mirrors only commanded-original movement to its owning client and requires authoritative feet synchronization before arrival; builds and automated tests pass, live retest pending. |
| 24 Aug 2026 | Phase 1A original-character live retest passed its basic move: Magey visibly followed `moveoriginal` for a 27.3-yard commanded walk, navigated a local obstruction, synchronized the owning client at exactly `(-43.19, -4308.26, 69.92)`, and only then reported arrival and commanded hold. Redirect, explicit stop, exit restoration, and lifecycle cases remain untested. |
| 24 Aug 2026 | The same original-character run failed exclusive input ownership: Magey still accepted ordinary WASD movement during the RTS claim, although the action bar appeared greyed. `SetClientControl(player, false)` does not remove the original as its own active mover. The original remains the original `Player`, but production RTS must park direct client gameplay input and enforce that ownership server-side. |
| 24 Aug 2026 | The owner selected custom-client work as the next job. Begin with a versioned capability handshake, RTS enter/exit, parked original-body input, reliable restoration, unsupported-build refusal, and a narrow server packet-origin gate; direct possession and product UI can build on that boundary later. |
