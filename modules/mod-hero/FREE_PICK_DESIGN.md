# Free Pick purchase integration contract

## Current normal-mode correction, 25 September 2026

Rules revision 3 follows `hero-reference-20260921-r1`'s Area 52 Live-channel
catalog and legacy UI concepts. The evryLoader document
`docs/NORMAL_FREE_PICK_BASELINE_69814.md` is the policy/evidence record.
Regular Free Pick has no seasonal Masteries page or Class Points. Normal
shared-fee families are ordinary class abilities, with reference rarity costs.
Intimidating Shout is standalone; Rallying Cry and Tar Trap have no inferred
family discount. New restricted/unclassified purchases remain blocked until
their normal rules are defined. Current retail talent nodes and acquisition
thresholds replace retired-node and blanket-level assumptions; multi-rank or
unresolved paths stay previews. No spell effects change.

`PurchaseEnabled` governs new acquisitions independently of `Reviewed`, which
keeps the existing ownership reconciliation functional. Normal requests use
rules revision 3 in the existing V3 carrier. New profiles initialize at 3;
the owner declared existing characters disposable, so validation uses a new
Hero without conversion, deletion or a new character migration. The historical
decisions below explain the transaction infrastructure and earlier prototypes.

The owner approved this integration contract on 23 September 2026: authenticated ability messages handled by mod-hero, recorded independent/purchased spell sources, and the 5% base-mana Hero Rejuvenation fallback with existing heals allowed to finish. The selected product milestone remains the complete server-authoritative purchase, cast, refund, and persistence loop. The initial owner-tested prototype used one durable 8-AE allocation. The owner has since approved the Area 52 progression extended to level 90: 9 AE initially, then one AE and one TE at every level from 10 through 90, totaling 90 AE and 81 TE. Original purchases and their recorded prices remain in the same profile.

## Approved integration

On 25 September 2026 the owner approved the shared-cost mastery model for regular
Free Pick: 1 AE per family plus 1 AE per member, with a family fee present exactly
when at least one member is selected. Parent records live in the existing owned
ledger with catalog `retail-12.1.0.69814:masteries` and spell zero. They must never
enter spell reconciliation, `character_spell`, or independent source writes.
The native service and server both reject incomplete family selections. The
last member refund returns both recorded paid amounts. This uses existing module
hooks and changes no spell effects. Freezing Trap remains a disabled preview
until its existing core trap has a freeze handler; other mastery members use
their current native effects and ordinary retail-derived level thresholds.

On 24 September 2026 the owner approved purchasing eligible abilities independently of equipped weapons. The purchase availability check must not call `HasItemFitToSpellRequirements`. Actual casting continues to use the core's normal equipment and resource checks. Weapon skills remain part of Hero's ordinary race/class skill data, applied by `Player::LearnDefaultSkills` during creation and login.

Use a consumed `evryCA` addon message on the existing authenticated game connection. The regular Free Pick window drives it; the player does not type commands. In `WorldSession::HandleChatAddonMessage`, validate prefix/text lengths before dispatch, retain existing addon enablement and flood controls, and ask PlayerScript whether a module consumed the message. The addon opcodes are already `STATUS_LOGGEDIN` and `PROCESS_THREADUNSAFE`. The module obtains the character from that session, never from a claimed account or character in the payload.

The implementation adds narrowly scoped callbacks to the existing PlayerScript family, with no behavior for an unhandled player:

- `OnAddonMessage`: consumes a validated prefix/payload for the authenticated Player in `ChatHandler.cpp`.
- `OnSpellLearn`: observes a valid acquisition before `Player::AddSpell` returns for an already-known spell. Skip loading and module-owned reconciliation, and distinguish independent acquisition from the module's grant. Validation precedes the hook so a nonexistent or invalid spell creates no entitlement. This also covers independent callers which invoke AddSpell directly.
- `OnBeforeSpellRemove`: records removal of independent entitlement before `Player::RemoveSpell` mutates the spell map; the module can retain a currently entitled Free Pick spell. Module reconciliation suppresses this notification. The supported initial entries must have no unresolved rank, override, taught-spell, or skill dependency graph.
- `OnTalentGroupChanged`: immediately reconciles after `SetActiveTalentGroup` and `SetPrimarySpecialization` in `ActivateTalentGroup`, before the function returns. Outgoing mode-only grants cannot remain usable for a later polling interval. The ledger and ordinary spells survive the switch.
- `OnDeleteTransaction`: appends tombstone and ownership/provenance cleanup to the actual `Player::DeleteFromDB` transaction, covering player, GM, and aged-character paths.
- `OnSaveTransaction`: appends provenance writes to the same character transaction used by `Player::SaveToDB`. Existing OnSave has no transaction parameter and its independent asynchronous commit is insufficient for spell provenance. Do not change the existing callback contract for other modules.
- `OnSpellPowerCost`: allows a module to supply a narrowly validated fallback at the end of the vector overload of `SpellInfo::CalcPowerCost`, before the resulting vector is checked or spent by the normal Spell code.

The `.learn` command and quest-reward spell learning have known-spell returns before LearnSpell. Their validated known-spell paths now notify the acquisition path too, so manually learning an ability already granted by Free Pick records independent ownership. The ordinary-class error behavior can remain unchanged; a Hero conversion must report its actual result. Merely observing successful spellbook insertions misses this case.

`PlayerSpell::dependent` is not a complete source ledger. Free Pick may use dependent grants to avoid persisting an unowned spell into `character_spell`, but AddSpell currently only changes `dependent=false` to true, not the reverse. The integration must preserve existing independent copies, and explicitly make an independently acquired copy persist when it was previously module-only. The module tracks this source transition, rather than inferring refundable ownership from HasSpell. Independent provenance is written with the character save and is included in a transaction snapshot before any refund can remove the mode entitlement.

## Rejuvenation resource adaptation

The pinned retail Rejuvenation mana rows require Druid specialization auras. Hero does not have those auras, so the ordinary calculation yields no mana cost. There is no existing SpellScript power-cost mutation hook: Spell.cpp calculates costs before CheckCast, and CheckPower and TakePower then use that same vector.

The implementation validates pinned Rejuvenation SpellPower row 305256 (5% base mana; required aura 137011), copies that row locally with only RequiredAuraSpellID cleared, and runs the existing row calculator. Apply this fallback only to Hero casting Rejuvenation when the normal cost vector has no applicable mana cost. This preserves the ordinary cost modifiers, insufficient-mana rejection, and deduction machinery without granting a Druid class aura or changing retail Druids. The row identity and cost fields are checked at runtime before enabling purchase. The interface must describe this as the Hero starter rule. The owner explicitly approved this resource adaptation on 23 September 2026.

## Carrier alternatives

The existing `TrinityCore` addon command protocol is a viable authenticated carrier. It consumes `i`/`h`, four echo characters, and a command string; it emits acknowledgement and generic success/failure messages. Those messages are transport acknowledgements, not durable purchase results. A CommandScript can use the ordinary-player Help permission (507, linked from player role 199 in the base auth data), avoiding another RBAC migration, but that couples the feature to an unrelated permission. Permission zero is not an unconditional grant: RBAC checks membership normally.

The approved dedicated addon hook was selected because it keeps availability a game feature and avoids coupling to command permissions. The owner approved this small additional core callsite. A new daemon, socket, fifth database, or manual GM command workflow is unnecessary. Leaving Rejuvenation unavailable would fail the selected damage-and-healing milestone; globally removing retail aura conditions or granting the entire Druid aura has broader gameplay consequences and is not recommended.

## Durable transaction contract

Use the existing characters database. A stable profile key contains realm, character GUID, and Free Pick mode/ruleset family. The rules revision is a field, never part of a key that creates a newly funded wallet on upgrade. Persist a wallet version and balance, catalog-entry identity and amount actually paid, independent spell provenance, and immutable request receipts. Entry identity is the advancement catalog key, not its current spell mapping.

An apply request expresses the final desired set of the installed append-only catalog (up to 128 entries); the server computes the complete add/remove diff and validates the final build. The mask is only a compact transport encoding of a versioned catalog order. Unknown bits, unsupported entries, malformed integers, invalid identity, stale revision/version, death, combat, casting, unavailable entries, and insufficient AE or TE reject the whole diff. Same request ID and payload returns the durable result; the same ID with a different payload rejects. Stored paid values determine refunds exactly once.

Only one apply per current character session is admitted at a time. Database uniqueness and version guards remain authoritative across sessions/restarts. Append wallet, owned-entry changes, durable receipt, and required provenance in one transaction. A transaction conflict rolls back every operation. The implementation must ensure a failed compare/version check aborts the SQL transaction rather than silently continuing after an UPDATE that affected zero rows.

`CharacterDatabase.CommitTransaction` merely enqueues. The module owns its asynchronous transaction and query callbacks in a WorldScript, after map workers have joined; their lifetime is independent of WorldSession. A mutex-protected per-character queue holds each operation through transaction completion and authoritative readback. A reconnect queues initialization behind the old commit and logout source flush. Callbacks capture state and GUID, never a Player or WorldSession pointer, and re-resolve the exact current state before changing a player. The production queue is exercised by the reconnect ordering test. Durable receipt lookup still determines an uncertain COMMIT result.

The baseline `MySQLConnection::ExecuteTransaction` ignored START TRANSACTION and COMMIT return values and could reconnect within a transaction. This milestone corrects those execution boundaries, disallows statement replay after a lost transaction connection, and retains receipt readback because a lost COMMIT reply still has an unknown outcome. The callback alone cannot prove a durable success. Read back the matching immutable receipt and authoritative wallet before sending a successful result or applying grants. On read failure the result is unknown; retain the request identity for retry. These checks do not imply that unrelated database behavior has been exhaustively tested.

Reconciliation treats the ledger as authority and is idempotent at commit/application/reply crash boundaries. An asynchronous completion rechecks current character, generation, and mode; if the player switched or logged out, it does not grant an inactive mode's spells. Login and mode entry rebuild active grants from the durable state. The transaction-aware deletion callback removes profile/ownership/receipt/provenance/session rows together with character deletion, including GM and aged-character paths; foreign keys to profile rows provide dependent cleanup and prevent orphan records.

## Bounded wire contract

Reserved prefix: `evryCA`. Version: `V3`. Requests remain one ASCII message of at most 255 bytes. Each response uses two bounded, correlated parts, assembled in fixed native storage before any state is published. Ownership masks are exactly 32 lowercase hex digits; other numeric fields are canonical decimal integers.

Request: `V3 S <nonce32hex> <request32hex>` or `V3 C <nonce32hex> <request32hex> <expectedVersion> <desiredHex>`. S requests a snapshot or a previous receipt; C applies a desired set. The nonce identifies the current native lifecycle. Request identity persists across timeout/retry and is distinct from the nonce. The authenticated player supplies identity; any optional client identity field must be checked, never trusted.

Replies: `V3H <nonce32hex> <request32hex> <PlayerGUID> <mode> <result> <version> <ae> <te> <ownedHex> <availableHex> <rulesRevision> <earnedLevel> <entryCount>` and `V3P <nonce32hex> <request32hex> <version> <paidASCII>`. One paid-price character per catalog entry encodes ASCII 33 plus its original price. Both parts remain below 255 bytes through 128 entries. Native validates identity, version, count, masks and separate currency conservation, accepts either part order, and publishes only the complete pair. A retry preserves the economic request identity and clears partial assembly. Matching client and server catalogs are required. A server result includes the current authoritative snapshot even when returning an old request receipt; older versions cannot replace newer confirmed state. No client-side price or balance is accepted.

The native bridge and module use this same fixed grammar. Ordinary unavailable/loading/refused states are visible. A timeout does not become a new purchase. No Lua or client-owner work occurs on a database worker thread.

## Validation boundary

Pure reducer and parser tests, a built worldserver, and SQL/package checks are source and fixture evidence. They do not establish that a retail client can cast or heal. Owner acceptance still requires ordinary-player 9 to 7 to 5 AE purchases, TE purchase/refund and level awards, real Frostbolt and Rejuvenation casts, reload/relog/restart persistence, full exact refund, duplicate requests, mode transitions, independent/manual overlap, refusal paths, and a clean exit.

## Existing spell effects

Owner-approved gameplay policy: refund or mode exit withdraws future casts immediately; already applied effects on other targets finish their ordinary duration. Player::RemoveSpell clears owned auras on the player and known pet auras, but does not find every multi-target Rejuvenation on another unit. We will not claim that it does. Strict cancellation would require separate cross-target source tracking and must not delete an independently reapplied aura. Existing specialization cleanup remains in force.

## Source ordering and action bars

Each server-generated random session token allocates one immutable database source epoch without granting more AE or changing the build version. Repeated initialization and ordinary saves reuse that token. A save that precedes the asynchronous login read allocates the token and writes source intent in the same transaction as character_spell; no deliberately failing save or in-memory-only durability substitute is used. A reconnect carries any uninitialized predecessor tokens in oldest-first order, so a newly queued save cannot give an older session a later epoch. Reads resolve their own token epoch, not the most recently allocated profile counter. Each permanent acquisition/revocation also has an increasing local revision. Source rows compare the pair (epoch, revision): an older session's delayed write can finish if no newer intent for that spell exists, but cannot override a newer source or inflate its revision. Initial spellbook imports fill only absent source rows; an explicit durable revocation wins over a stale character_spell row. Actual new/changed spell grants before the module login callback remain independent acquisitions. Purely dependent grants keep their provider lifetime and are not converted to permanent purchases.

The mode callback withdraws outgoing grants immediately and runs again after the current action-bar load finishes. Each action-bar load has a process-wide generation, so a stale callback cannot overwrite a newer load, including a rapid return to the same mode or a relog to the same character. Snapshot reconciliation skips action-bar cleanup while a load is pending. Refunded-only buttons are cleared from the current loaded Free Pick bars; independent and dependent external copies keep their buttons. The regular character save persists this cleanup, and reconciliation repeats it after a crash.
New-character saves clear any old profile, session tokens, provenance, and tombstone for a reused GUID in the same transaction that inserts the new character. The core can reuse deleted GUIDs after restart; this reset prevents a new character from inheriting old purchases or being blocked by the old deletion tombstone. Rollback recovery removes stale character_spell copies explicitly revoked in the durable source ledger, then materializes independent copies. Purchased-only spells are never materialized.

Permanent character deletion removes the ledger, provenance, and session tokens atomically with the character row and leaves a tombstone against queued writes. Configured soft deletion preserves these records alongside the retained character, so restoring that character restores the same wallet and sources. A genuinely new character reusing a permanently deleted GUID receives an atomic lifecycle reset at creation.


## Talents and level progression

The owner explicitly requested keeping current 12.1.0 spell effects unchanged.
The first talents are Lonely Winter (205024), Thick Hide (16931), and Fleet
Footed (378813). Each has one native rank, costs 1 TE and unlocks at level 10.
Their native TraitNodeEntry IDs are 80238, 103306 and 112657, respectively.
The original ten ability indices are unchanged; these talents occupy indices
11 through 13 in the versioned transport catalog. Current native tooltips and
spell behavior apply, even where they differ from older Ascension talents.
There are no effect changes, custom spell IDs or hotfix-table writes.

Purchasing checks level and server-reviewed spell data, without checking
current power pools or equipped weapons. The normal cast and passive learning
paths still apply all native requirements. AE and TE have independent balance
and conservation checks, and mixed purchases/refunds commit atomically. Paid
prices remain the refund authority. Passive grants are removed on refund and
mode exit unless another source retains them; this does not alter their effects.

The forward character migration adds one AE to existing wallets and adds
`talent_balance` and `earned_level`, preserving receipts, ownership and sources.
The existing OnLevelChanged PlayerScript callback triggers a queued refresh.
A single guarded database update catches up to the current level and advances
both balances and the version. Repeated or lower levels add nothing. Level
reductions by GM command retain earned currency; reset/prestige is out of scope.
The parser, SQL reducer and client conserve AE and TE independently against the
same level allowance. No new core seam is required by this expansion.

The matching client protocol is V3 and catalog schema is 2. All three deployment
parts must be updated together. The new profile cannot be read by the original
8-AE executable. Keep the database and package backups; do not attempt an
executable-only downgrade after publishing this migration.


## Approved six-batch expansion (25 September 2026)

The owner selected three batches of ten abilities and three of ten talents.
Install order alternates abilities/talents; counts are 23, 33, 43, 53, 63, 73.
Generated `HeroFreePickCatalog.h` and the native catalog share stable positions.
New abilities cost 2 AE; new single-rank talents cost 1 TE from level 10. Existing
prices, initial identities, supported-entry policy, source ledger and progression
are retained. Current retail effects are explicitly unchanged at owner request.

The receipt migration adds nullable canonical `desired_bits` without rewriting
legacy receipts. Reads fall back to old `desired_mask`; new receipts preserve its
low word and record all 128 bits. This extends the existing approved mod-hero
carrier and callbacks; it adds no new core seam. The client UI uses hex nibbles
and a paid-price string rather than Lua floating-point ownership masks.

The loader's `diagnostics/catalog-batches/OWNER_TEST.md` provides the six exact
cumulative installers. Build/test/package evidence is distinct from owner live
acquisition, effects, refund and persistence testing. Do not downgrade catalogs
after purchasing entries that an earlier server cannot represent.
