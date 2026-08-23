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

  **Rerun with those two changed, and it works.** With the camera 300 yards from the
  body, clicks under the commander camera reached the server at -521.698 -3956.561
  41.273 and -522.517 -3963.887 45.798, both within about twelve yards of the camera
  and over three hundred from the character. So the client's terrain raycast follows
  the commentator camera, and ground ordering at commander range needs no client
  patching at all — only a spell whose range and line of sight allow it. The Blizzard
  cast itself was invisible from up there, which is the same streaming limit Spike 1
  found: the effect spawned by the body's seer range, so the client was never told
  about it.

  This run overlaid Blizzard's own `SpellMisc` (ID 164352) rather than using the clone,
  because a cloned spell could not be cast: the client had its data (`GetSpellName`
  answered) but neither the spellbook nor a `/cast` by name would reach it, and
  `SkillLineAbility` is not the reason, since Blizzard has no row there either. That
  overlay changes Blizzard for every character and should be deleted when the spike is.
- Spike 4 walker at scale: blocked on Phase 1
