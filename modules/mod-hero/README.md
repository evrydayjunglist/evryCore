# Hero native specializations

Regular Free Pick now uses rules revision 3, based on the pinned Area 52
Live-channel catalog with explicit 12.1.0 adjustments. Corrected prices and
rarity replace provisional defaults. Shared families remain, but Intimidating
Shout, Rallying Cry and Tar Trap are standalone choices; unsupported rarity or
acquisition rules remain previews. PurchaseEnabled gates new acquisition,
while Reviewed continues to govern spell reconciliation. ValidationNode points
at a verified current retail talent record without changing its purchase key.
No spell effects or core callbacks change. Use a fresh Hero; the owner declared
prototype characters disposable and no conversion/deletion is performed. See
evryLoader `docs/NORMAL_FREE_PICK_BASELINE_69814.md` and
`diagnostics/normal-free-pick/OWNER_TEST.md` for policy, installation and testing.

Hero has three modes on the normal specialisation page, playable from level one, and a hidden Initial specialisation that every new Hero starts on:

| ID | Mode | Order index | Talent group |
|---|---|---|---|
| 601 | Free Pick | 0 | 0 |
| 602 | Free Pick — Seasonal | 1 | 1 |
| 603 | Wildcard | 2 | 2 |
| 600 | Initial | 4 | 4 |

Status on 22 September 2026, checked in game by the owner: activating a mode, all six directions between the modes, the spellbook in every mode on a Hero with borrowed spells, per-mode action bars, a cast, one log-out and log-in, and the names. A new Hero gets the activation spell without help. Still to check: the Activate buttons during a cast and in combat, a client and server restart, an ordinary class and Reaper's spec page, and a clean exit. At that checkpoint the modes granted nothing: no spells, talents, currency or acquisition, and a Hero's learned spells were shared by all three. The source implementation below adds Free Pick transactions; its owner gameplay acceptance remains separate.

- `data/sql/db-hotfixes/2026_09_21_00_mod_hero_three_specializations.sql` writes Hero's four specialisation rows, points the class default at 600, and publishes one hotfix push that also retires Initial's old id, 1479.
- `data/sql/db-hotfixes/2026_09_22_00_mod_hero_specialization_activation.sql` adds Hero's class bit to Blizzard's skill line ability row 35162, for the hidden activation spell 200749, and keeps every other field. The client only uses that spell for the Activate button once the row admits the player's class.
- `data/sql/db-characters/2026_09_21_01_mod_hero_specialization_actions.sql` copies each existing Hero's Initial action bars into the three modes' empty bar sets.
- `data/sql/db-characters/2026_09_22_01_mod_hero_specialization_ids_characters.sql` moves saved Heroes from 1479 to 600 and clears their empty per-mode loadouts, which the server rebuilds at login under the current mode names.
- `src/mod_hero.cpp` gives every Hero spell 200749 at creation and login, without replacing a copy learned by hand.
- Two changes in `src/server/game/Entities/Player/Player.cpp`, on `evry` only: a Hero keeps a chosen mode below level ten, which other classes still do not; and a switch removes only the outgoing specialisation's PvP talent spells, so it cannot take away another class's spell that a Hero learned. That specialization checkpoint added no new core hook; the later transaction implementation below does.

The client half is in evryLoader: the specialisation page fixes, a guard in Blizzard's micro menu, and `/evryspec`. The full record is [HERO_SPECIALIZATIONS_69814.md](../../../evryLoader/docs/HERO_SPECIALIZATIONS_69814.md), and the owner procedure is [OWNER_ACTIVATION_TEST.md](../../../evryLoader/diagnostics/hero-specs/OWNER_ACTIVATION_TEST.md).

Free Pick transactions are installed and the owner reported a successful gameplay test on Herro on 23 September 2026 Pacific time: "Everything appeared to work as described." Read-only checks confirmed the named test account, no GM security assignment, saved Free Pick 601, 4 AE remaining, Frostbolt and Rejuvenation owned at 2 AE each, and six successful purchase/refund receipts. This general owner report is separate from per-case fixture coverage; the saved rows do not independently establish every optional manual-learning, restart, or healing-timing check. That prototype used a named Hero starter adaptation: one durable 8-AE allocation, reviewed Normal abilities costing 2 AE, and refunds of the exact amount recorded at purchase. Rules revisions do not create a second funded wallet. Charge, Frost Nova, Blink, and Moonfire remain unavailable; spell data and level checks can make another entry unavailable for the current character.

The owner requested on 24 September 2026 that learning an ability be independent of equipped weapons. Free Pick now permits purchasing an otherwise eligible ability with empty hands or a different weapon equipped. Actual casts retain the spell's ordinary equipment and resource requirements. Hero's existing class-skill data grants its weapon skills through the normal character-creation routine and repairs missing default skills at login; this change does not add another skill-granting path.

The existing Advancement window sends bounded `evryCA` addon requests over the authenticated game session. The module derives character identity from the session and commits the complete desired build in one characters-database transaction. Durable receipts distinguish retry from a different request reusing the same ID. The database version constraint turns a conflicting update into a rollback, and the module reads the receipt and wallet before confirming success. Database uncertainty withdraws module-only grants until authority is recovered; it never creates a blind second purchase.

`src/HeroFreePick.h` contains the shared parser and transaction reducer. `src/HeroFreePickSql.h` builds the same SQL used by the module and isolated database tests. `src/HeroFreePickQueue.h` serializes each character's asynchronous operations across logout and reconnect. `src/HeroFreePick.cpp` supplies authenticated handling, module-owned completion/readback, spell entitlement reconciliation, and the approved resource adaptation. The migration is `data/sql/db-characters/2026_09_24_00_mod_hero_free_pick.sql`. No new database or external process is required. The seven PlayerScript callbacks and `.learn` known-spell notification are described in `FREE_PICK_DESIGN.md`.

Purchased ownership, ordinary permanent learning, and an ordinary dependent grant are separate sources. A manual, quest, trainer, or item acquisition after Free Pick already granted the spell makes an independent permanent copy. Its provenance is written in the character save transaction and in subsequent advancement transactions; a login reconstructs it after a crash. Immutable server session tokens let an ordinary save persist that source even before the first asynchronous login read finishes. Old-session writes cannot overwrite a newer source decision. Skill/talent-style dependent acquisitions keep their normal provider lifetime and are reconstructed by that provider at login. Both kinds survive a Free Pick refund or mode exit. The generic core RemoveSpell API still means revoking the ordinary spell entitlement; it supplies no identifier for distinguishing several unrelated temporary providers, so this module does not claim to solve the core's general multiple-provider revocation problem. Free Pick withdrawals do not invoke that ordinary-source revocation path.

Hero Rejuvenation uses the pinned retail 5%-of-base-mana row when no ordinary mana row applies. The module clears the aura prerequisite only on a local copy and uses the ordinary cost calculation, insufficient-power check, and deduction. It does not grant a Druid class aura. On refund or mode exit, further module-only casts stop, while already applied Rejuvenation heals on the Hero or other targets finish normally. The runtime checks that Rejuvenation remains neither a single-target-limited aura nor a ChangeSpec-interrupted aura before making it purchasable.

Restore instructions must retain the ledger tables. Before returning to a binary without source reconciliation, apply the prepared recovery SQL while worldserver is stopped; it removes stale explicitly revoked spell rows and materializes durable independent copies. An independent acquisition committed with a refund immediately before a crash could exist in provenance before the ordinary character spell save. This recovery step does not grant purchased-only abilities. The owner controls deployment, migrations, process restarts, and gameplay tests.

Permanent character deletion removes the ledger, provenance, and session tokens atomically with the character row and leaves a tombstone against queued writes. Configured soft deletion preserves these records alongside the retained character, so restoring that character restores the same wallet and sources. A genuinely new character reusing a permanently deleted GUID receives an atomic lifecycle reset at creation.


The approved talent expansion replaces the prototype allowance with 9 AE and
0 TE initially, then one AE and one TE per level from 10 through 90, totaling
90 AE and 81 TE. The forward migration
`data/sql/db-characters/2026_09_24_01_mod_hero_free_pick_progression.sql` adds the
missing one AE to existing wallets and preserves every purchase and refund price.
Login and the existing level-change callback catch up missing awards once.
AE and TE remain separate and cannot fund each other's purchases.

Lonely Winter (205024), Thick Hide (16931) and Fleet Footed (378813) are the
first native single-rank talents, each 1 TE at level 10. Their current retail
spell effects are unchanged. No effect hotfix, replacement spell or new core
hook is included. Normal passive learning/removal applies; independently learned
copies survive refund and mode exit. Acquisition does not require a resource
pool or equipped weapon. This expansion is built and fixture-tested; its
current-client gameplay acceptance is still pending. Install the matching
server, native V3 client and Lua talent UI together, following evryLoader's
`diagnostics/character-advancement/OWNER_TEST.md`.


The current catalog expansion is six cumulative batches of ten, alternating
abilities and talents, for 73 total entries. `HeroFreePickCatalog.h` is generated
from evryLoader's `lua-state/catalog-batches.json` and exact-build spell evidence.
`HeroEntryMask.h` matches its native `entry_mask.hpp`. Runtime spell-data checks
remain authoritative. No effects or resource requirements were edited for these
batches. The startup receipt migration is
`2026_09_25_01_mod_hero_free_pick_catalog_bits.sql`; older receipts and all purchases
remain intact. See evryLoader `diagnostics/catalog-batches/OWNER_TEST.md` for the
matched installers and live acceptance procedure; V2 packages are historical.
