# Reaper first playable loop — 69814

**Prepared character-screen showcase, 26 September:**
`Builds/reaper-class/creation-showcase-69814-20260926/package-01` adds a dark
hood and shoulders to the native Reaper preview, a two-handed flourish with the
installed Reaper soul effect, and a ready idle. It changes seven hotfix records;
there are no binary, Lua, gameplay or inventory changes. Review
`data/reaper-showcase-69814.json` and `evryLoader/diagnostics/reaper/showcase/`.
All 12 data/native-Lua checks and 15 private MySQL deployment cases pass.
This follow-up is **prepared, not installed or visually accepted**.
The native customization mode hides head/shoulder/weapon preview pieces rather
than loading Purpose 9 as a model outfit. Undead Face also deliberately undresses
the model immediately; select Hair to inspect its body outfit. Preserve that
retail category behavior. Restore this follow-up before the creation package.

**Installed creation equipment, 26 September:**
`Builds/reaper-class/creation-outfit-69814-20260926/package-02` adds native
character-creation preview and starting loadouts using the captured CoA Phantom
outfit and Faded Scythe. New characters receive the five equipped pieces plus a
Hearthstone through `Player::Create`; the placeholder dagger is removed from the
creation data. CoA's polearm, two-handed axe and plate proficiencies use native
skill records. Existing inventories are not migrated. Review
`data/reaper-creation-69814.json`; reproducible tools and owner instructions are in
`evryLoader/diagnostics/reaper/creation/`. All 25 offline/data/deployment tests pass.
Readback confirms the package is installed. The owner reported the new starting
gear as passable; this does not accept every race, animation, or persistence
case. No C++ or loader binary changes are required. Restore this creation
package after the showcase, and before the older damage or visual packages.

**Installed damage correction, 26 September:**
`Builds/reaper-class/damage-scaling-69814-20260926/package-01` is installed and
passes readback. It combines the owner-approved modern retail removal of stat-based
magic resistance with six hotfix changes for Murder/Soulrend. The old raw
resistance value `1` could erase 90–100% of magic damage. Explicit immunities,
scripted resistance/absorbs, armor and damage-reduction abilities remain intact.
The rebuilt server, all 221 C++ test cases (19,729 assertions), the production
callback host, and 18 disposable MySQL deployment cases pass. The first owner post-fix log confirms three Murder hits
of 44–45 and one Soulrend hit of 66 with no resistance; Reap and automatic swings
both hit for 12–13. This establishes the bounded damage fix in that sample, not
all later-level balance or edge cases. See `tools/REAPER_DAMAGE_OWNER_TEST.md` and
`data/reaper-damage-69814.json`. Restore this damage package before using an
older visual package's hash-pinned checker or rollback.

**Installed visual fidelity, 26 September:** effects package-05 is now installed
and verified. The owner reported the game launched and the refinement was subtle
but acceptable. This is a limited owner visual observation, not proof of every
cleanup/effect case. It refines Murder's crow body in Blender and restores the
original layered rendering for Soul Strike's hands/impact and Soulrend's hands.
The four model references are recorded in `data/reaper-fidelity-69814.json`.
All 32 focused visual/deployment tests passed at that checkpoint.

This module now implements a bounded **first-rank kit at levels 1–7**, with the complete starter loop available at level 6. It is an implementation for owner testing, not a completed Reaper class or an accepted gameplay result. Class 17 remains separate from Hero class 16. Native specialization IDs 610–613 and the level-10 specialization gate are unchanged. Playerbots make Reapers only with `Playerbots.CoAClasses = 1` in `modules/mod-playerbots.conf` (off by default).

The authoritative combat state is native Runic Power and three caster-owned native auras. There is no resource service, client prediction, background player-pointer map, new core hook, purchase API, or Hero economy dependency. Normal client casts use native equipment, target, range, line-of-sight, power and GCD checks; module SpellScript callbacks handle the class-specific rewards and consumption.

## Kit and resource contract

- **Reap — 600000**, source CoA **500357**, learned at level **1**. Main-hand melee weapon, native melee range, hostile living target. Physical damage is **90% of normalized main-hand weapon damage, then +3**. Native armor, hit, dodge, parry, block and critical calculations still apply. It costs no power. The first damaging hostile hit of a cast generates **70–130 internal Runic Power (7–13 displayed)**. Soul Collector also grants one fragment. A miss, immunity, or fully absorbed/zero-damage hit gives neither reward. The source helper 355461 has a 61-outcome integer roll; 10 displayed RP is its average, not an exact fixed gain.
- **Murder — 600001**, source **500376**, learned at level **1** by the reconstruction's effective baseline class-grant list. It costs **300 internal RP (30 displayed)**, has a **15-yard** hostile range and a travelling missile. After the damage correction package, Shadow damage is **1.20 × attack power + 0 or 1**, before native modifiers and mitigation. The historical 13-point base was interpreted by the modern core as a percentage of ExpectedStat, not flat damage; the removed per-level field was not a reliable modern scaling rule. A landed hostile hit grants one soul **only while Soul Collector is present**, including a full absorb. Immunity gives no soul. A living hit target receives the source's **−3% melee hit chance for 10 seconds** through helper 600011/source 560421. No critical-hit talent bonus is granted.
- **Soul Strike — 600002**, source **500517**, learned at level **6**. Main-hand melee weapon and hostile living target; **400 internal RP (40 displayed)**. Shadow damage is **75% normalized weapon damage, then +3**. A landed hostile hit grants one soul, including full absorb/zero damage, but not immunity. The separate self-heal 600010/source 500522 uses **floor(post-mitigation hit damage × 0.8) + floor(current missing health × 0.1)**. As in the reconstruction's hit contract, the damage term includes overkill. It cannot critically heal or receive caster healing bonuses; native target healing modifiers/absorb and the maximum-health cap still apply. At full health, the heal has no effective health gain. Source helpers 573293 and 500574 supply the 80% and 10% constants; their stale child tooltip does not override the parent's formula.
- **Soulrend — 600003**, source **573316**, learned at level **1**, available only with **three owned souls and owned Soul Infusion**. Main-hand melee weapon and hostile living target. After the damage correction package, Shadow damage is **1.80 × attack power + 0.297 × Shadow spell power + 0 or 1**. The original Shadow spell-power contribution is retained; the historical 17-point ExpectedStat percentage base is removed. It costs no RP. A landed hit consumes all souls, fragments and infusion and generates **150 internal RP (15 displayed)**. Full absorb still consumes and energizes. Immunity consumes but does not energize; miss, dodge and parry preserve the resources. Invalid/failed casts do not consume them. Evading/vanished targets with no hit do not spend the resources in this port. The **15 RP gain is an explicit provisional retail tuning choice**: the reconstruction labels it a placeholder and no stronger captured numeric value was found. It is not claimed as established official CoA tuning.
- **Soul Collector — 600004**, source **706731**, automatically learned passive at level **1**. It enables Reap's fragments and Murder's baseline soul generation. It does not grant talent effects such as critical-hit souls, Soul Splinters, Harvest Time, or Essence Invigoration.

All four active spells have the source's **1,250 ms base global cooldown**, native haste handling, and no separate cooldown. Murder is prevented by silence; the melee abilities use the native melee prevention category. Murder and Soul Strike pay their native RP cost at cast launch, including a subsequent miss/absorb/immunity; no refund flag is added. Failed requirements do not reach the module reward callbacks. Triggered child effects are ordinary server spell mechanics and do not replace the player's cast.

All baseline resources requested for this slice are implemented:

- **Runic Power** uses native power type 6: **1000 internal / 100 displayed maximum**, native initial state, regeneration/decay, packets and cost calculation. No mana or rune cost is substituted. Reap generates it; Murder and Soul Strike spend it; Soulrend returns some. Native saturation prevents overflow.
- **Soul Fragments — 600005 / source 805077:** visible count **0–2**. Each third fragment is converted synchronously into one soul. A newly generated fragment refreshes the **30-second** duration; gaining a soul directly does not refresh existing fragments. Conversion still spends three fragments at the soul cap.
- **Reaped Souls — 600006 / source 500363:** **0–3**. Reap conversions, Murder with its passive, and Soul Strike increase them. Reaching three grants infusion once. Additional gains cannot exceed the cap. The owner's 26 September lifetime correction removes timed expiry: souls last until spent, death, or reconnect. The original combat package used CoA's **300,000,010 ms** duration, displayed as `4 d`; the separate lifetime overlay must be installed to correct it.
- **Soul Infusion — 600007 / source 803031:** one owned aura at three souls, also without timed expiry after the lifetime correction. Soulrend consumes it together with all owned souls/fragments. Removing infusion clears its associated counters; removing the souls also removes infusion. Foreign-owned same-ID auras are neither spent nor cleared. No client cancellation is offered for these counters.

**Lifetime adaptation:** soul resources clear on death and reconnect. The three temporary auras use the existing native `AURA_CANNOT_BE_SAVED` custom attribute; login also reconciles them to empty. `/reload` alone retains server state. This deliberately avoids banking a temporary infusion across logout; it differs from the reconstruction's ordinary saveable-aura behavior. Runic Power retains the core's native lifecycle. There are no dangling player references or delayed resource jobs.

`tools/generate_reaper_lifetime.py` generates `data/reaper-lifetime-69814.json`, a
single-row overlay on the frozen combat baseline. It changes only owned
SpellDuration 100's Duration and MaxDuration to native `-1`. Exactly Reaped Soul
and Soul Infusion reference it; Soul Fragment keeps native duration 9 (30 seconds).
The original generator, manifest and applied SQL remain historical baseline inputs;
new installations apply the lifetime overlay after the combat package. The guarded
owner package is `Builds/reaper-class/soul-lifetime-69814-20260926/package-01`.
Loader `diagnostics/reaper/effects/LIFETIME_OWNER_TEST.md` gives install, test and
restore commands. Neither a server rebuild nor a client/UI update is required.
Package preparation and fixture checks do not establish an in-game lifetime result.

## Learning, equipment and ownership

Forward hotfix rows associate only the five baseline abilities with class skill **1311** and class mask **65536**. Native `LearnDefaultSkills`/`SetSkill` handles creation, `UpdateSkillsForLevel` calls `LearnSkillRewardedSpells` on level-up, and `_LoadSkills` applies the same rewards when loading a saved character. There are no module login spell grants. Hidden helpers have no skill-line learning row. Learning never removes a manual/independent spell and does not replace saved action bars.

The installed creation package now supplies the source CoA **Faded Scythe,
Phantom Hauberk, Phantom Legplates, Phantom Boots and Phantom Shirt**, plus a
Hearthstone, through native Purpose 9 loadout 1001 and `Player::Create`. Native
skill records provide polearm, two-handed axe and plate proficiency. Existing
characters keep their equipment; there are no login grants or inventory repairs.
The earlier minimum Worn Dagger 2092 creation row is removed by that installed
overlay. The original SQL remains historical baseline material, and historical
receipt 600012 remains inert. The prepared showcase adds only Purpose 1 preview
memberships; its hood and shoulders never enter a new character's inventory.

The rank-one scope is intentional. CoA's next Reap rank exists at level 6, and several other next ranks start at level 8. Those replacement chains are not claimed here: this kit keeps the first-rank ability identities, with the documented modern AP adaptation for Murder and Soulrend. Normal later leveling remains possible, but higher-level balance and full rank progression are unfinished. Test the complete loop at level 6 before evaluating higher-level balance.

Current class-level stat rows remain the pre-existing Demon Hunter-derived placeholders. At level 6 their base columns are STR 21, AGI 26, STA 438 and INT 30; these are data inputs, not a predicted final Undead character sheet. The core applies ExpectedStat/race/item adjustments. Class metadata grants AP from strength and agility; native normalized weapon damage and explicit AP/SP coefficients use those actual stats. The Death Knight-derived base-mana column is zero and none of this kit needs mana. This change does not claim level-90 stat tuning.

## Provenance and translation

Reference reconstruction: `F:\evry\WOWEmulation\Emulators\Reference\cores\azerothcore-wotlk-coa`, commit `49e0dca1b1751118a99efc07e7738c360a136dcb`. Captured client/builder data: sibling `coa-datamine`, commit `5078f9ac22541c45c17a866c23aac6c12073de23`. These are independent reconstruction/capture evidence, not official server source or an owner gameplay trace.

The effective baseline grants and first ranks were traced in `AscensionCustomClassData.h`, `AscensionSpellProgressionData.h`, `AscensionCustomResourceData.h`, `AscensionCompat.cpp`, `AscensionReaperSoulStrike.cpp`, and `AscensionReaperTalents.cpp`. The captured Reaper resource frame uses 805077/500363/803031 for its three-part display. Named raw Spell CSV records in the documented CoA base-chain variant supplied the missing Soul Strike/Soulrend/helper fields. The Area 52 overlay and a raw row's `live=null`/absence from the talent builder are not treated as proof of baseline availability. The class-grant list establishes the selected acquisition policy.

Resource conversion and removal helpers 805078/561290/561294 are translated into synchronous native callbacks, not copied as unsupported WotLK effects 175/183. The raw Reaped Soul record also references Spectral Affinity 504034 and a flat family modifier. The reconstruction's effective `AddAura` resource path does not execute that non-aura trigger; no unconditional extra damage buff or unimplemented talent benefit is invented here. Broader talent interactions remain outside this kit.

Retail translation is pinned to **12.1.0.69814**, executable SHA-256 `c7c4795b5de2f0f5b3da840ee06120714bf520a2ada8b9cb7130d2a3754e4195`. Read-only server DB2 extracts use current core layouts; their internal asset build labels can predate the executable and are not presented as fresh CASC extraction proof. Allocation checks include native row IDs, encrypted-section ID reservations, native table bounds and existing hotfix IDs. Family 36 is unused in the captured native SpellClassOptions and live hotfix rows, matching the existing class shell without importing WotLK flag masks.

`data/reaper-69814.json` contains every owned record, source mapping, table hash, build and evidence hash. Spells **600000–600012** are allocated in a verified hole inside the shipped Spell/SpellName ranges. The forward push is **111761 / UniqueId 1609394700**, following observed maximum 111760. Every custom spell includes its **client-only Spell.db2 root blob**, typed name/misc/effects and supporting records, plus hotfix metadata. Native visuals/icons are reused as presentation adaptations; no new DLL or executable patch is required.

## Files, verification and delivery

- `src/spell_reaper.cpp` registers the actual native cast/aura/login callbacks; `ReaperSpellMechanics.h` contains the transition and reward contract used by them.
- `data/sql/db-hotfixes/2026_09_25_00_mod_coa_reaper_combat.sql` and the same basename under `db-world` are forward migrations. They accept empty or identical owned rows, reject foreign values/builds before writing, and roll back all changes in their database on failure. Old applied migrations are untouched.
- `tools/generate_reaper.py` regenerates SQL/manifest from captured read-only evidence. `tools/validate_records.py` validates IDs, roots, field linkage, costs, equipment, learning and effect order. Evidence lives under `F:\evry\WOWEmulation\Emulators\Builds\reaper-class\first-playable-loop-20260925\implementation`.
- `tests/game/ReaperSpellMechanics.cpp` supplies nine discovered Catch2 cases. `tools/test_callbacks.py` compiles the **unchanged production .cpp** against an isolated host and executes its registered hooks. This covers callbacks and state mutation; the host does not claim to emulate native hit rolls, costs or client casting.
- Loader `diagnostics/reaper` contains the Lua, MySQL, file-deployment fixtures and guarded owner scripts. The MySQL fixture starts/stops only its own disposable process/data directory/port, and never reads live credentials. The owner-only installer uses the runtime configuration without printing credentials.

The original combat package at `F:\evry\WOWEmulation\Emulators\Builds\reaper-class\first-playable-loop-20260925\candidate` is installed, confirmed by its journal, target hashes and read-only database checks. The owner said “I think it works”; this is a limited owner report, not completion of every acceptance gate. The package and its recovery files are preserved.

The installed presentation update is `F:\evry\WOWEmulation\Emulators\Builds\reaper-class\coa-presentation-69814-20260925\package-01`. A read-only check on 26 September confirmed its 25 files, creation row and migration ledger. It removes login spell/item grants, adds the normal creation equipment row, and delivers original CoA Reaper icons/resource art plus Hero/Reaper class portraits and creation buttons. Owner gameplay acceptance of that newer presentation remains unrecorded. Its frozen preparation reports retain their historical state. The paired loader source is `diagnostics/reaper/presentation`; no native DLL or core seam is added.

The current update's recovery returns the exact previous combat server and UI, removes only its creation row/new artwork/overrides, and keeps earned items, spells and settings. Its new migration ledger remains applied to prevent an automatic reapplication after restore. It makes no client hotfix/cache changes. The original combat package has a separate broader recovery contract and source-drift guards; do not bypass those guards with the newer working sources.

Passing source checks, isolated SQL/Lua/callback fixtures and compilation establish a reviewable implementation. Native client identity (including the historic Blackout Kick alias), actual casting, visible resources, ordinary hostile combat, reload/relog/fresh-launch and clean exit remain **owner gameplay acceptance gates**.

## Installed 3D effects, 26 September 2026

The preceding correction was effects package-04: three native soul buff icon fields
now select their original CoA art, and Reap's model reference selects a derivative
with restored native rendering metadata. Its CoA animation tracks, custom
textures, tint/opacity and effect scale are preserved. This addresses the reported
shared Shadow Bolt fallback and a concrete rendering-conversion defect behind
the Reap impact investigation. All 25 correction tests pass and installation is
verified; the corrected native appearance still needs owner testing. Review rows
are in `data/reaper-visual-correction-69814.json`. After restoring newer overlays, use package-04 Check and the
loader's `diagnostics/reaper/effects/VISUAL_CORRECTION_OWNER_TEST.md` procedure.
Restore package-04 before changing earlier effects/lifetime packages.

All five visual chains are now installed. The owner confirmed Murder's crow and
corrected gesture on package-02. At the owner's request, package-03 installed
Reap, Soul Strike, Soulrend and Soul Infusion while the game and servers were
closed. All 59 added files, 80 visual records and four bindings pass readback;
the Murder package and installed soul-lifetime correction remain intact.
The four newly added effects still require owner gameplay testing.

`tools/generate_reaper_remaining_effects.py` and
`data/reaper-effects-complete-69814.json` extend the original graph without
changing its 100 records. Infusion's separate hash-guarded reconstruction repairs
two malformed tracks using the matching native reference while preserving the
41 valid CoA tracks and source customization. It uses the normal aura visual
binding and start/end events, so the existing resource lifetime controls it.
No gameplay code or native binaries changed. The 35 new deployment/asset tests
and 45 existing effects tests passed. See loader
`diagnostics/reaper/effects/REMAINING_OWNER_TEST.md` for startup, tests and rollback.
Use package-03 Check while installed. Restore package-03 before restoring the
duration correction or Murder. Earlier packages and migrations remain frozen.

### Earlier delivery history

`tools/generate_reaper_effects.py` and `data/reaper-effects-69814.json` supplied the original separate visual-record generator and review manifest. The first owner package, `Builds/reaper-class/3d-effects-69814-20260926/package-01`, delivered Murder's converted CoA crow through existing typed hotfix tables and client-only blobs. At that checkpoint the other active spells were unbound and Infusion was excluded for two inconsistent tracks. Package-03 resolves that delivery gap as documented above.

The forward/restore SQL is in that candidate, deliberately outside automatic migration directories until delivery is accepted. Earlier applied migrations and all gameplay code are unchanged. Loader source `diagnostics/reaper/effects/README.md` records translation evidence, conversion limits, reproducibility and recovery. Its `OWNER_TEST.md` gives exact normal-cast, shutdown and restore procedures. The new 38-case structural/deployment suite and all 84 existing Reaper/Hero cases passed, together with record validation and a newly compiled production callback host. Those checks establish offline behavior, not native model loading or visual parity.

The owner subsequently saw package-01's crow reach the enemy and confirmed that the character made no casting gesture. The animation link incorrectly used effect type 1 (procedural effect), where the exact native cast/impact records use type 6 (character animation). The generator and dependency resolver are corrected in `records-v3.json` and this module's review manifest. Package-02 changes only those two Murder records; its seventeen payload files are identical. All 45 focused effects/deployment tests pass. The owner must restore package-01 with `3d-effects-69814-20260926/installer-02/Install.ps1`, then install package-02 using its `OWNER_TEST.md`. After replacement, use package-02 for Check/Restore/reinstall. The owner has now installed package-02 and confirmed the casting gesture works; its read-only Check passes. See the effects output OWNER_OBSERVATION_02.md. Gameplay and runtime binaries are unchanged by that visual correction.
