# mod-rts-spike

Throwaway harness for the RTS commander-mode plan's Phase 0. It answers the questions that cannot be answered from source, because they are about how the live retail client behaves. Delete this whole folder once the answers are recorded below.

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

Outcome to write down: **works** → camera ladder lands on rung 1, and the real work is implementing the unhandled commentator opcodes in the `game` branch. **absent or inert** → rung 1 is dead; run `/rtsspike tactical` to judge whether the stock max-zoom camera (rung 4) is tall enough while the vehicle-seat spike (rung 3) is scheduled.

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

## Spike 4 — walker at RTS scale

Not scripted here — it needs the mod-playerbots order API that does not exist yet (Phase 1). Note it as blocked on Phase 1 rather than run it now. When Phase 1 lands, order five grouped bots to one point with formation offsets and redirect them mid-walk.

## Answers (fill in, then delete the module)

- Spike 1 commentator camera:
- Spike 2 addon channel:
- Spike 3 order reticle:
- Spike 4 walker at scale: blocked on Phase 1
