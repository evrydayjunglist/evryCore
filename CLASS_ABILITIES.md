# Class ability repairs

This job brings the implemented work from the old class-abilities track onto
`game`, incorporates the useful differences in agatho's fork, and fixes defects
found during review. It does not establish that every ability of the affected
classes is complete or matches current retail gameplay.

## Sources and selections

The destination starts at `game` commit
`dc34a663ffa61777e05aa5a7713ae765e934f5bf`. The source is
`F:\evry\WOWEmulation\Emulators\Source\TrinityCore-evry`, branch
`master-track/class-abilities`, commit
`5a6d2b22cdc237ab0209b6d011e1b5285026a141`. All 40 non-merge commits after
`932cf01717440428480422fad2e4fe55ac2f754f` were dry-run individually with
`git cherry-pick -n`; they applied without conflicts. No old master merges were
imported. The owner requested consolidation into logical commits after review.

[Agatho's class-abilities branch](https://github.com/agatho/TrinityCore/tree/ba30c46792c013bb7b1c56475a433bab088f670f)
largely ports that same local source. The review recorded 153 remote branch
heads and inspected the relevant feature, integration and test branches. The
selection is therefore a single implementation with corrections, rather than
two overlapping sets of scripts.

- Keep the local DK, Evoker, DH and pet scripts, with the review fixes below.
- Adapt agatho's Windfury null guard from
  `1e7d8bb9194f999ad6d8b93a4ceeee0926c2cf5a`.
- Extend the existing proc-family check to cast-ended events, following
  `2ea9d7ef2a3603fbe9a1ebbe64c156189b34d0c7`. Keep the separate spell-type gate
  for hit-style events.
- Adapt the Soulmonger and Vampiric Embrace guards from
  `f6a67316018e9031f595529f2bed20fd17ca87ae`. Do not copy its database repair for
  a corrupt Vampiric Embrace row from a different database installation.
- Adapt zero-Fury termination from
  `dd7039abf0ba8bfa4bdc7527e8ac6871a039e351` into the existing Devourer script.
  Do not add its second Devourer implementation or its no-op scripts.
- Adapt Fel Rush from `a64ed80925ec4d9b5802d900ef6225e64aff823f` and its engine
  dependency `3455f22feebdc404aeb29fe2a2826572a0e7e4a8`. Use the real 195072
  spell, native line targeting, and an explicit Fel Rush movement allowlist.
  Keep the existing Monk behavior; the proposed Monk speed scaling was an
  untested extrapolation.
- Do not import unrelated intro quests, trait/packet systems, old 3.3.5 branch
  work, or duplicate SQL/script implementations.

Agatho kept two old Soul Reaper scripts because it had not verified the changed
data. That is not evidence of a better implementation. The local data-based
removal is retained; the separate retained Soul Reaper implementation is not
removed by it.

## Corrections made during import

Shared engine support covers summon damage, primary-stat/critical-strike/
versatility support buffs, leech, and caster-dependent spell schools. School
conversion also reaches immunity, reflection and critical-damage checks.
Immolation Aura bypasses early aura refresh when A Fire Inside permits
independent casts, so the separate durations can actually exist.

Leech now pays on its own timer and discards its pending pool on death. It
excludes self-healing and uses effective healing. The old source incorrectly
waited for the next player cast. The timing model follows the repeating event
in [SimulationCraft's pinned implementation](https://github.com/simulationcraft/simc/blob/c97e14c7a5ad8b9b7d49f9c2e07c614c389024c0/engine/player/player.cpp):
base interval 1.5 seconds, 1 second for Rogue, multiplied by spell haste. This
is source-based modeling, not a new retail packet measurement.

Death Knight corrections tie Doomed Bidding to qualifying successful casts and
exclude dead Lesser Ghouls from Outnumber. Demon Hunter corrections give each
Immolation Aura its own damage bank and ramp, reset Ragefire's cap per tick,
deliver Chaotic Disposition's actual bonus damage without recursion, preserve
deferred damage, keep cast percentages on the cast, and avoid double-counting
fragment procs or reading consumed aura stacks. Void Metamorphosis ends when
Fury reaches zero.

Evoker corrections cover outgoing ally-aura ownership, concurrent state access
and cleanup, Time Dilation's delayed-damage arithmetic, multi-target beam
amounts, replay ownership, and critical-hit hooks. Time Dilation's four replay
ticks conserve the delayed amount even for a one-point hit; the old unsigned
calculation could underflow into a very large final hit.
Stretch Time now accumulates overlapping repayment over its data-defined
duration and ends when paid, instead of repeating the full absorbed hit on a
permanent damage aura. Plot the Future grants personal haste while preserving
Exhaustion and avoiding a raid-wide Bloodlust cast.
Essence Burst grants and consumption checks now use each specialization's
cost-modifier aura, so the proc applies to that specialization's spenders.

Fel Rush's forced movement speed is scoped to its known bundles. It does not
use the remote patch's global 60-yard-per-second cutoff. Airborne application
and removal batch speed/gravity changes so intermediate aura states do not
leave dash-speed horizontal drift. The current extracted data already contains
the helper spells that the old hotfix SQL would replace; that SQL is omitted.
The missing 197707 trigger is suppressed only when that spell is absent.

## Evidence and remaining work

The implementation, compile, fixture and gameplay evidence are separate:

- The imported SQL is checked in an isolated SQLite fixture for repeatable
  application and matching C++ spell registrations. This is not a MySQL update
  run, live database validation or server startup test.
- Static spell-hook checks use read-only exports of the deployed DB2 files.
  Their embedded schema tag is `WOWSTATIC_12_1_0_68914`. That is not proof of
  the exact client build from which those files were extracted. The export
  manifests preserve this distinction.
- The old Fel Rush handoff records owner acceptance of ground/air movement on
  build 68275. That historical observation does not validate this build's
  movement, air damage or collision behavior.
- Current build and test results are recorded in the generated commit packet.
  No live database updates, server boot or owner gameplay test are part of
  this import.

The source's full-class completion claims are not carried forward. Known
remaining work includes the empowered Dream Breath healing baseline and its
Echo/Stasis reproduction, Future Self timing, Prescience target-selection
heuristics, and area-trigger geometry. Several DH talent probabilities and
timings remain estimates, including Fallout, Untethered Rage, Wounded Quarry,
Voidrush, Soulshaper and Dark Matter. Independent Immolation Aura instances do
not yet have a separately enforced five-instance cap. Other Unholy talents and
Deathbringer work were not completed by the source track.

## Commit and validation packet

The packet is stored outside the repository at:

```text
F:\evry\WOWEmulation\Emulators\Builds\class-abilities-20260923
```

It includes the pinned fork inventory, class review findings, DB2 exports,
SQL/static validation helpers, build/test logs, ordered patches, and detailed
commit messages. `Prepare-Commit.ps1` stages one reviewed group at a time. It
checks the branch, parent tree, index and task-file contents before staging.
It does not create commits. The complete implementation stays in the working
tree while the index holds only the next group.

Open **PowerShell** and run:

```powershell
Set-Location 'F:\evry\WOWEmulation\Emulators\Source\evryCore'
& 'F:\evry\WOWEmulation\Emulators\Builds\class-abilities-20260923\Prepare-Commit.ps1'
git diff --cached --stat
git diff --cached
```

Read the displayed commit message, then run the exact `git commit -F` command
printed by the helper. Run the helper again to prepare the next group. Repeat
until it reports that every group is committed. Do not use `git add .`: the
existing untracked `modules/` directory is outside this job.

To rebuild after all groups are committed, use the same PowerShell window:

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build D:\WOWEmulation\Emulators\Builds\evryCore-job-modules --target worldserver tests --config RelWithDebInfo --parallel 12'
& 'F:\evry\WOWEmulation\Emulators\Builds\evryCore-job-modules\bin\RelWithDebInfo\tests.exe' '[ClassAbilities]' --reporter compact
```

The build must finish successfully and the test program must print
`All tests passed`. If `worldserver.exe` is running, stop it through its normal
server console before rebuilding; do not terminate it to recover a link error.

## Owner gameplay checks

After reviewing the complete commit packet and loading the changes through the
normal server deployment process, use the ordinary spellbook and talent UI on
test characters. Keep client build, class/spec/talents, spell names and combat
log timestamps with any failure report.

1. On an Unholy DK, build and spend Lesser Ghoul charges, cast Apocalypse and
   Putrefy, and consume Sudden Doom with Death Coil/Epidemic. Buff expiry must
   not count as a cast. Outnumber must decrease when a ghoul dies, even while
   its corpse remains.
2. On a DH with A Fire Inside and Ragefire, overlap Immolation Auras. Their
   ramps and explosions should remain independent. Check Void Metamorphosis
   at zero Fury and repeated fragment consumption for duplicate grants.
3. On an Evoker with a friendly player, check Ebon Might on the ally and its
   extensions; use Time Dilation for both small and larger damage. Damage
   replay must finish without huge hits or duplicated absorption. Check Echo
   and Stasis only against the explicitly supported spell paths.
4. With leech on the character sheet and missing health, deal only autoattack
   or periodic damage without pressing another spell. Healing should still
   arrive. Death and resurrection must not release healing from the old life.
5. On a DH, test Fel Rush while standing, running, jumping backward, double-jumping,
   gliding and falling. Check charges, root rejection, ground/air path damage,
   collision and lack of drift after the dash. Check ordinary movement and
   Monk Roll/Evoker Hover for regressions as separate observations.
6. On a Shaman without Unruly Winds, trigger Windfury repeatedly. On a Priest,
   test Halo with and without Phantom Reach and Vampiric Embrace during
   ordinary spell use. There must be no server crash.

## Owner merge commands

Only after all prepared groups are committed and accepted, run in PowerShell:

```powershell
Set-Location 'F:\evry\WOWEmulation\Emulators\Source\evryCore'
git status --short --branch
git switch game
if ($LASTEXITCODE -ne 0) { throw 'Stop and report the switch error.' }
git merge --no-ff job/class-abilities -m "Merge class ability repairs into game"
if ($LASTEXITCODE -ne 0) { throw 'Stop and report the merge output before renaming the branch.' }
git branch -m job/class-abilities merged/class-abilities
if ($LASTEXITCODE -ne 0) { throw 'Stop and report the branch rename error.' }
git switch evry
if ($LASTEXITCODE -ne 0) { throw 'Stop and report the switch error.' }
git merge --no-ff game -m "Merge game class ability repairs into evry"
if ($LASTEXITCODE -ne 0) { throw 'Stop and report the merge output; do not discard either side.' }
git status --short --branch
```

These commands are owner-run; the agent does not merge or push. The README job
entry accompanies the job. If the merge reports a conflict, stop and send the
complete terminal output for resolution. A README resolution must keep evry's
module/playerbot entries and the class-ability entry from game. Rebuild the intended `evry` runtime
after that merge; the `game` validation build does not establish module or
gameplay acceptance on `evry`.
