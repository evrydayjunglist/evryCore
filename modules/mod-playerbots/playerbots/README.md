# playerbots.exe

`playerbots.exe` is the out-of-process C# host for evryCore playerbots. The selected
long-term role is the strategic brain described in
[`PLAYERBOTS_ARCHITECTURE.md`](../../../PLAYERBOTS_ARCHITECTURE.md).

The current executable is only the login and transport foundation. It does not yet
choose activities, issue gameplay intentions, persist planner memory, or control a bot.

## Current behavior

- Connects to the worldserver playerbot bridge on loopback TCP.
- Uses protocol version 2: a four-byte big-endian length followed by one bounded UTF-8
  JSON document.
- Performs the handshake, reads realm status and the configured roster, and sends
  heartbeats.
- In `Playerbots.LoginMode = Coordinator`, sends the idempotent
  `ensureBotsOnline` request after reading the roster.
- Reconnects and reasserts the roster after worldserver restarts.
- In Automatic login mode, uses the bridge read-only; worldserver retains legacy login
  and in-process autonomy.

The current client serializes request and response exchanges. Worldserver does not yet
push observations or controller events, and the bridge has no strategic gameplay
commands.

## Selected future role

The executable will own long-horizon personality, schedules, goals, activity and quest
selection, groups, relationships, guild ambitions, professions, economy, housing plans,
per-bot memory, and replanning.

It will send typed strategic intentions. Worldserver remains authoritative for the live
`Player`, controller ownership, filtered observations, navigation, combat timing,
interactions, death and teleport protocol, costs, permissions, and real client packets.

Without this executable, the future Reserve controller may keep bots online for RTS and
bounded local hold, passive, defensive, follow, assist, guard, patrol, and explicit
action behavior. Presence remains a separate `Playerbots.LoginMode` choice.

Before strategic writes are added, the bridge needs authenticated duplex messaging,
heartbeat-expiring leases, world and controller generations, idempotent intentions,
snapshots and sequenced events, bounded backpressure, and restart reconciliation. See
the architecture document for the full contract and implementation order.

## Projects

- `src/Playerbots.Protocol` owns framing and shared C# protocol primitives.
- `src/Playerbots.Host` owns the Worker host, connection, reconciliation, and current
  coordinator loop.
- `tests/Playerbots.Protocol.Tests` contains framing tests and the fake-worldserver
  reconnect integration test.

As strategic behavior grows, keep one executable and separate transport, protocol,
domain, planning, persistence, and test responsibilities. Use bounded per-bot mailboxes
and planner concurrency rather than one process or operating-system thread per bot.

## Build

The module CMake file publishes a self-contained, untrimmed, single-file executable for
the configured platform and copies it beside the server binaries. The normal `modules`
target depends on that publish unless `PLAYERBOTS_BUILD_EXECUTABLE` is disabled.

Build the C# solution directly for development:

```powershell
dotnet build modules/mod-playerbots/playerbots/Playerbots.slnx --configuration Release
```

Run the framing tests:

```powershell
dotnet run --project modules/mod-playerbots/playerbots/tests/Playerbots.Protocol.Tests/Playerbots.Protocol.Tests.csproj --configuration Release
```

Run the fake-worldserver reconnect test against a published executable:

```powershell
dotnet run --project modules/mod-playerbots/playerbots/tests/Playerbots.Protocol.Tests/Playerbots.Protocol.Tests.csproj --configuration Release -- --host D:/WOWEmulation/Emulators/Builds/evryCore-job-modules/bin/RelWithDebInfo/playerbots.exe
```

## Runtime configuration

The host reads standard .NET configuration. The current defaults are:

- host `127.0.0.1`;
- port `3444`;
- heartbeat interval `15` seconds.

Command-line configuration uses the normal .NET form, for example:

```powershell
playerbots.exe --Playerbot:Port=3444 --Playerbot:HeartbeatSeconds=15
```

Worldserver-side settings remain in the copied `modules/mod-playerbots.conf` beside the
server. A build refreshes only the `.dist` file.
