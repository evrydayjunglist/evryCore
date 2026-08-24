# Playerbots architecture

This is the authoritative architecture and long-term product direction for
`mod-playerbots`, `playerbots.exe`, and their relationship with Commander Mode.
[Commander Mode](COMMANDER_MODE.md) remains the implementation plan for direct switch
and RTS control.

Last updated: 23 August 2026

## Vision

Playerbots have two useful operating levels.

With `mod-playerbots` and `mod-rts`, but without `playerbots.exe`, they are
commandable RTS-ready characters. Their life goal stays unset while they safely execute
local orders and tactical policies such as hold, passive,
defensive, follow, assist, guard, patrol, move, attack, loot, and interact as those
capabilities are implemented.

Adding `playerbots.exe` turns those embodied characters into a simulated population.
The executable owns long-horizon purpose: personalities, schedules, activity and
quest choice, groups, relationships, guild ambitions, professions, economic goals,
housing plans, durable memory, and replanning from results.

The split is hierarchical:

> `playerbots.exe` decides why and what next. Worldserver decides how the character
> safely and legally does it now.

There is one embodied skill runtime and one automated playerbot movement
implementation. Reserve, Builtin autonomy, External autonomy, and RTS request the same
skills rather than creating separate walkers or combat engines. Direct human control is
a sibling exclusive controller that transfers the real client's input actor; it does
not run another automated movement producer at the same time.

## Decisions captured here

Selected:

- `playerbots.exe` is an optional external strategic brain, not only a login supervisor.
- The external strategic-task bridge is allowed and remains subordinate to worldserver.
- Bots without the executable may be online RTS-ready reserve units with bounded local
  tactical skills but no self-chosen long-term life.
- Presence and strategic autonomy are independent configuration and authority axes.
- RTS and direct human control preempt strategic autonomy through the shared controller
  contract.
- The C++ world runtime, C# executable, Lua addon, and loopback framed-message stack are
  retained.
- All game actions remain worldserver-validated and use player-like packet paths.
- Per-bot durable memory and explicit human-equivalent group or guild information
  sharing are allowed; automatic omniscient knowledge merging is not.

Still open:

- the default Reserve stance and the first tactical policies enabled for RTS units;
- whether active RTS or direct human control temporarily pins presence when a
  Coordinator lease expires, or is safely released before logout;
- the exact future strategic-autonomy configuration key and migration defaults;
- the first external strategic activity chosen for the end-to-end proof.

## Selected stack

- `worldserver`, `mod-playerbots`, and `mod-rts` remain C++.
- The retail addon remains Lua.
- `playerbots.exe` remains a C# .NET 10 Worker application.
- Worldserver and the executable communicate over loopback TCP with bounded,
  length-prefixed messages.
- UTF-8 JSON is the current strategic-contract encoding because it stays inspectable
  while the contract changes. gRPC or another encoding remains available when measured
  traffic and tooling benefits justify the dependency and build cost.
- When durable planner memory is needed, use a private SQLite store owned by
  `playerbots.exe`. TrinityCore world and character databases retain game authority,
  while SQLite stores planner-owned memory only.
- Distribution builds remain self-contained single-file executables. One executable
  hosts isolated bot planners with bounded shared resources rather than requiring a
  process or operating-system thread for every bot.

C# is selected because strategic work is stateful, asynchronous, and slow relative to
the world tick. Domain modeling, persistence, diagnostics, testing, and iteration speed
matter more there than native-cycle performance. Movement, combat timing, interaction,
and all authoritative state remain in C++, keeping executable pauses and garbage
collection independent from the world thread.

## Responsibilities

### Worldserver and `mod-playerbots`

Worldserver owns the live `Player` and is the final authority for every action:

- real `Player` rows, `WorldSession`, login, logout, and presence transitions;
- exclusive controller arbitration and controller generations;
- player-filtered observations sent to an external planner;
- navigation, movement heartbeats, geometry, facing, and safe stopping;
- tactical target selection allowed by the active order and stance;
- combat timing, known spells, resources, cooldowns, and failure handling;
- interaction, quest-step execution, loot, vendor, travel, and other embodied skills;
- death, teleport, time-sync, and session protocol mechanisms;
- phase, visibility, range, line of sight, cost, permission, and prerequisite checks;
- queueing the same client packets a real player would send.

These are embodied skills. They may contain immediate tactical decisions, but they do
not choose an unrelated long-term activity while the bot is in reserve or under human
command.

### `playerbots.exe`

The executable owns strategic autonomy:

- per-character personality, preferences, schedules, and long-lived goals;
- choosing an activity, quest, destination, group, guild goal, profession goal,
  economic goal, or housing project;
- semantic plans and one bounded current intention per externally controlled bot;
- social coordination and group scheduling under an explicit knowledge-sharing policy;
- durable planner-owned memory and recovery journals;
- observing results, abandoning invalid plans, and replanning from current state;
- reconciliation after its own restart or a worldserver restart;
- status, structured logs, traces, and operator-facing diagnostics.

It sends typed strategic intents. `WorldSession`, world-tick execution, and authoritative
`Player` state remain inside worldserver.

Examples of the boundary:

- The executable may choose to progress a quest the bot knows. Worldserver chooses a
  currently legal target, approaches it, fights or interacts, and reports progress.
- The executable may choose willing characters and a goal to form a group or guild.
  Worldserver performs the real invitations, acceptances, registrar interactions,
  costs, permissions, and packet flows.
- The executable may design a housing project. Worldserver may execute it only through
  player-accessible housing capabilities with normal materials, costs, placement, and
  permissions. Direct object spawning or database mutation is not an implementation.

### `mod-rts` and the retail addon

The addon owns the human-facing selection and command interface. `mod-rts` authenticates
and translates those orders into the shared commandable-`Player` service.

`mod-rts` does not own controller state, pathfinding, movement, combat, or interaction.
It calls the same embodied skills used by built-in and external autonomy.

## Presence and strategic autonomy

Presence and intelligence are separate choices.

The implemented `Playerbots.LoginMode` currently has these meanings:

- `Automatic`: worldserver logs in the configured roster during startup. This remains
  the default and currently runs the existing in-process autonomous brain.
- `Coordinator`: worldserver prepares the roster but leaves it offline until
  `playerbots.exe` sends the idempotent `ensureBotsOnline` request. Loss of the owning
  connection starts the current grace period and then logs the managed bots out.

The selected future design adds an independent strategic-autonomy axis with these
semantics:

- `Reserve`: no self-chosen long-term activity; local orders and stances remain
  available.
- `Builtin`: the existing in-process activity selector supplies long-horizon autonomy
  for compatibility and migration.
- `External`: a healthy `playerbots.exe` strategic lease supplies long-horizon goals;
  loss of that lease returns the bot to reserve before presence policy does anything
  else.

The exact configuration key is added by the job that implements this axis. Its
semantics must remain independent from `Playerbots.LoginMode`.

Important combinations are:

- Automatic presence plus Reserve: RTS-ready bots without `playerbots.exe`.
- Automatic presence plus External: the living-population mode. Executable loss
  returns bots to reserve but leaves them online for RTS.
- Coordinator presence plus External: the strict mode. Executable loss first makes
  the bots safe, then logs them out.
- Automatic presence plus Builtin: the current legacy behavior during migration.

The executable may hold both a strategic lease and, in Coordinator login mode, the
presence lease. They remain different authority claims even when one process holds
both.

## Control model

An online bot has three orthogonal kinds of state.

Presence tracks whether its session is offline, logging in, online, quiescing, or
logging out.

The exclusive controller identifies who may produce gameplay intentions. Controllers
include the retail client, reserve, built-in autonomy, external autonomy, RTS, direct
control, and the chosen unattended-original-character policy. Exactly one controller
may drive a `Player` at a time.

A directive and stance live under that controller. Examples include hold, move, follow,
patrol, guard, attack, loot, passive, defensive, and assist. Hold is not a controller:
`RTS + Hold` retains RTS ownership and authorization indefinitely.

Execution state records what the embodied runtime is currently doing, including
walking, fighting, interacting, recovering, crossing a teleport barrier, or draining
for logout.

Every controller change increments a per-subject generation. The old producer is
quiesced at a safe boundary, incompatible work is cleared, and late requests from the
old generation are rejected before they can queue another action.

RTS or direct control always preempts external autonomy for the selected subject.
`playerbots.exe` is told that the intention was suspended or invalidated. When human
control releases the bot, external autonomy receives fresh authoritative state and
replans from current truth rather than resuming a stale target, interaction, purchase,
or combat step.

RTS and direct switch also need session-level state because acquiring or releasing
control may be an atomic transition across the commander, the original body, and one or
more selected bots. A per-`Player` boolean is not enough.

## Reserve skills and stances

Reserve means no self-chosen life goal, not a completely inert session.

- Hold keeps the current owner and position and does no unrelated work.
- Passive remains inert even when attacked.
- Defensive responds only to legitimate attackers, obeys a bounded leash, and returns
  to the previous order or anchor.
- Guard protects an assigned unit or place according to its stance and returns after
  a response.
- Follow and assist use player-like movement and actions with no teleport catch-up.
- Patrol follows player-authored waypoints through the normal walker and obeys its
  stance and leash.
- Explicit move, attack, loot, interact, stop, and release orders use the shared
  commandable-`Player` capabilities as they become available.

The default reserve stance and the exact first set of tactical policies remain product
choices in `COMMANDER_MODE.md`. Commanded combat behavior begins from that explicit
choice rather than inheriting the current autonomous default.

## Strategic bridge

A C# strategic-task bridge is selected and allowed. The boundary is capability-scoped,
versioned, authenticated, and server-authoritative.

The current protocol is only the executable foundation. It uses protocol version 2,
bounded length-prefixed JSON, a serialized request/response client, and login/status
operations. It is not yet the strategic protocol.

Before the first writable strategic intent, the bridge needs:

- a dedicated reader loop and correlated concurrent requests;
- server-pushed observations, progress, controller changes, and failures;
- bounded incoming, outgoing, and per-bot queues with backpressure;
- an authenticated coordinator identity and explicit capability negotiation;
- a server-issued, heartbeat-expiring strategic lease rather than socket lifetime as
  proof of health;
- a world boot epoch, realm identity, coordinator instance identity, lease identity,
  per-bot controller generation, intent identity, revision, idempotency key, and trace
  identity where applicable;
- an authoritative snapshot after connect, monotonically sequenced deltas, and resync
  after a sequence gap;
- at-least-once delivery with idempotent state-setting and one-shot intent handling;
- cross-language golden-message, malformed-message, retry, duplicate, stale-generation,
  backpressure, reconnect, and restart tests.

Loopback defines transport scope; authenticated capability negotiation defines writable
authority, so a second local process gains nothing merely by connecting.

JSON remains inspectable while the contract is changing. Measured traffic or tooling
needs may select gRPC or a binary encoding through a separate dependency and build
decision.

## Information and non-cheat boundary

The external planner is useful only if it remains subject to player-like information
and action rules.

- A bot may expose its quest log, inventory, spells, legitimate map knowledge, visible
  and phase-valid nearby world, party or guild state, and action results.
- Exposed observations are limited to player-legible state; hidden spawns, unrestricted
  world objects, database tables, another player's private state, and server-wide
  omniscience fall outside that boundary.
- One executable may host many isolated bot agents. Private observations remain scoped
  to the observing bot unless the explicit sharing policy permits communication.
- Group, guild, and social coordination may share information through an explicit
  policy comparable to what players could communicate. The policy must say what is
  shared and with whom.
- Planner-owned memory does not override current world truth. Worldserver revalidates
  phase, visibility, range, line of sight, geometry, resources, cooldowns, costs,
  prerequisites, permissions, and controller ownership at execution time.
- The executable sends typed strategic intents. Worldserver alone translates accepted
  intents into packet actions and retains privileged operations such as SQL, teleports,
  inventory edits, quest completion, and direct group or guild mutation.
- Every game mutation follows a deliberately implemented player capability and queues
  the packet a real client would send.

## Persistence

World and character databases remain authoritative for game state. `playerbots.exe`
receives its permitted view through the bridge, keeping database authority and planner
knowledge separated.

When durable strategic memory is implemented, a private SQLite store is the selected
starting point. Key bot state by realm and stable character identity, not roster index.
It may contain personalities, preferences, long-term goals, relationships, schedules,
resumable semantic plans, an outbox, and a bounded decision journal.

Durable memory contains semantic goals rather than volatile target GUIDs, movement
steps, current tactical actions, or a worldserver controller generation. A new world
boot epoch requires a fresh snapshot and revalidation of every durable goal.

## Failure and restart behavior

When `playerbots.exe` disconnects or its strategic heartbeat expires:

1. Stop accepting new external intentions immediately.
2. Invalidate the external controller generation.
3. Finish only indivisible protocol work such as an already validated jump or required
   teleport acknowledgement.
4. Clear incompatible strategic work and enter the configured reserve stance.
5. Keep the session online under Automatic presence, or continue into controller-aware
   safe logout under Coordinator presence.

A half-open socket is not health. Socket close may accelerate expiry, but only a
server-enforced renewable deadline proves a live strategic lease.

After worldserver restarts, it issues a new boot epoch and no volatile action survives.
The executable reconnects, reads authoritative roster and bot state, reasserts durable
desired goals, and replans.

After `playerbots.exe` restarts, it does the same reconciliation. Old clicks, reward
selections, invitations, purchases, placements, and combat targets require fresh
validation before any new action.

The behavior when a Coordinator presence lease expires during active RTS or direct
human control remains an explicit product decision. The two safe choices are to pin
presence until that human-control session releases, or to release and restore the human
session safely before logout. Restoration of the controller's playable original
character always precedes a directly controlled subject's logout.

## Implementation order

The first strategic feature proves the bridge with a bounded semantic activity while
the current quest loop stays intact until embodied skills and controller migration are
ready.

1. Keep the working executable login foundation and existing built-in autonomy intact.
2. Retain the completed stock-client direct-switch no-go and cleaned-up evidence from
   `COMMANDER_MODE.md`.
3. Build the subject-neutral controller, generation, quiescing, hold, release, and
   stale-request contract.
4. Add reserve stances and reusable follow, patrol, guard, assist, move, attack, loot,
   and interaction capabilities in bounded jobs.
5. Separate the current in-process activity selector from its movement, combat,
   interaction, death, and session skill runtime. Keep it as the Builtin controller
   during migration.
6. Mature the bridge for authenticated duplex strategic intents, observations,
   reconciliation, and failure handling.
7. Prove one coarse external activity end to end, including executable loss,
   worldserver restart, RTS preemption, retry, duplicate delivery, stale-generation
   rejection, and non-cheat tests.
8. Move long-horizon quest and activity choice outward incrementally.
9. Add grouping, guilds, professions, economy, and housing one legitimate packet-driven
   capability family at a time.

“Everything a player does” is the destination. Each new verb still needs a safe,
player-valid embodied capability before the strategic planner can request it.

## Current implementation

The `job/playerbot-executable` foundation currently provides:

- a .NET 10 Worker host and protocol library;
- self-contained single-file publishing with the module build;
- a loopback TCP listener with bounded frames and world-thread request handling;
- handshake, status, roster, heartbeat, and reconnect behavior;
- `Automatic` login compatibility;
- opt-in `Coordinator` login with idempotent `ensureBotsOnline`;
- coordinator-loss grace, in-place logout, and fresh login after reconnect;
- protocol framing and fake-server reconnect tests plus the C++ disconnect-grace test.

It does not yet provide strategic autonomy, an external-autonomy mode, reserve stances,
authenticated writable access, a heartbeat-expiring health lease, duplex events,
controller generations, snapshots and deltas, planner persistence, or any remote
gameplay intent.

That distinction must remain visible in documentation and logs: the selected
architecture is the target, while the current executable is still its login and
transport foundation.
