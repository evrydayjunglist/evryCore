#include "tc_catch2.h"
#include "../../modules/mod-hero/src/HeroFreePick.h"
#include "../../modules/mod-hero/src/HeroFreePickQueue.h"
#include "../../modules/mod-hero/src/HeroFreePickSql.h"

using namespace HeroFreePick;

namespace
{
Request Commit(std::uint32_t version, EntryMask mask)
{
    return {true, std::string(32, 'a'), std::string(32, 'b'), version, mask};
}
constexpr std::uint32_t Supported = 2 | 8 | 16 | 32 | 128 | 256;

std::array<Entry, EntryCount> EconomicCatalog()
{
    auto catalog = Entries;
    // Economic invariants remain testable while acquisition policy blocks
    // rarity-limited abilities pending the normal-mode gem budgets.
    for (Entry& entry : catalog)
        entry.PurchaseEnabled = true;
    return catalog;
}
}

TEST_CASE("Hero requests are bounded and canonical", "[hero][freepick]")
{
    std::string prefix = "V3 C " + std::string(32, 'a') + " " + std::string(32, 'b') + " ";
    REQUIRE(Parse(prefix + "0 " + Hex(18)).has_value());
    REQUIRE(Parse(prefix + "0 " + Hex(AllEntries)).has_value());
    for (std::string suffix : {"0 8192", "-1 18", "4294967296 18", "00 18", "0 18 extra", "0 18\n", "0  18", "0 18 "})
        REQUIRE_FALSE(Parse(prefix + suffix).has_value());
    REQUIRE_FALSE(Parse(std::string(256, 'a')).has_value());
    REQUIRE_FALSE(Parse("V1").has_value());
    REQUIRE_FALSE(Parse("V1 S").has_value());
    REQUIRE_FALSE(Parse("V1 S " + std::string(32, 'a') + " " + std::string(32, 'b')).has_value());
    REQUIRE(Parse("V3 S " + std::string(32, 'a') + " " + std::string(32, 'b')).has_value());
}

TEST_CASE("Hero atomic desired build charges and refunds original paid amounts", "[hero][freepick]")
{
    Snapshot initial;
    Change purchase = Reduce(initial, Commit(0, 18), Supported);
    REQUIRE(purchase.Code == Result::Ok);
    REQUIRE(purchase.State.Essence == 5);
    REQUIRE(purchase.State.Paid[1] == 2);
    REQUIRE(purchase.State.Paid[4] == 2);
    auto changedCatalog = Entries;
    changedCatalog[1].Cost = 7;
    Change refund = Reduce(purchase.State, Commit(1, 16), Supported, changedCatalog);
    REQUIRE(refund.Code == Result::Ok);
    REQUIRE(refund.State.Essence == 7);
    REQUIRE(refund.Removed == 2);
    REQUIRE(refund.State.Paid[1] == 0);
    REQUIRE(Reduce(refund.State, Commit(1, 16), Supported).Code == Result::Stale);
    REQUIRE(initial == Snapshot());
}

TEST_CASE("Hero exact funds and whole-build rejection cannot partially apply", "[hero][freepick]")
{
    Snapshot initial = EarnThrough({}, 10);
    Change exact = Reduce(initial, Commit(1, 2 | 8 | 16 | 32 | 256), Supported);
    REQUIRE(exact.Code == Result::Ok);
    REQUIRE(exact.State.Essence == 0);
    Change excessive = Reduce(initial, Commit(1, Supported), Supported);
    REQUIRE(excessive.Code == Result::Insufficient);
    REQUIRE(excessive.State == initial);
    Change invalid = Reduce(exact.State, Commit(2, 1 | 2), AllEntries);
    REQUIRE(invalid.Code == Result::InvalidEntry);
    REQUIRE(invalid.State == exact.State);
    Change locked = Reduce(initial, Commit(1, 2 | 16), 2);
    REQUIRE(locked.Code == Result::Locked);
    REQUIRE(locked.State == initial);
    Change swap = Reduce(exact.State, Commit(2, 2 | 8 | 16 | 128 | 256), Supported);
    REQUIRE(swap.Code == Result::Ok);
    REQUIRE(swap.State.Essence == 0);
    REQUIRE(swap.Removed == 32);
    REQUIRE(swap.Added == 128);
}

TEST_CASE("Hero snapshots and replay payloads reject corruption and overflow", "[hero][freepick]")
{
    Snapshot state;
    state.Essence = 10;
    REQUIRE_FALSE(Valid(state));
    state = {};
    state.Paid[1] = 2;
    REQUIRE_FALSE(Valid(state));
    state = {};
    state.Owned = 2;
    REQUIRE_FALSE(Valid(state));
    state = {};
    state.Version = UINT32_MAX;
    REQUIRE(Reduce(state, Commit(UINT32_MAX, 2), Supported).Code == Result::Unavailable);
    Request a = Commit(0, 18);
    Request b = a;
    b.Epoch = std::string(32, 'c');
    REQUIRE(SamePayload(a, b));
    b.Desired |= EntryMask::Bit(0);
    REQUIRE_FALSE(SamePayload(a, b));
}

TEST_CASE("Hero manual acquisition after purchase survives refund and mode exit", "[hero][freepick]")
{
    Snapshot wallet;
    auto purchase = Reduce(wallet, Commit(0, 2), Supported);
    REQUIRE(DesiredGrant(2, purchase.State.Owned, 0, 0, true, true) == Grant::Dependent);
    uint32_t learnedIndependently = 2;
    REQUIRE(DesiredGrant(2, purchase.State.Owned, learnedIndependently, 0, true, true) == Grant::Permanent);
    auto refund = Reduce(purchase.State, Commit(1, 0), Supported);
    REQUIRE(refund.State.Essence == 9);
    REQUIRE(DesiredGrant(2, refund.State.Owned, learnedIndependently, 0, true, true) == Grant::Permanent);
    REQUIRE(DesiredGrant(2, refund.State.Owned, learnedIndependently, 0, false, true) == Grant::Permanent);
    REQUIRE(DesiredGrant(2, refund.State.Owned, 0, 0, true, true) == Grant::None);
}

TEST_CASE("Hero temporary providers and unknown outcomes do not become permanent acquisitions", "[hero][freepick]")
{
    REQUIRE(DesiredGrant(2, 2, 0, 2, false, true) == Grant::Dependent);
    REQUIRE(DesiredGrant(2, 0, 0, 2, true, true) == Grant::Dependent);
    REQUIRE(DesiredGrant(2, 0, 0, 0, true, true) == Grant::None);
    REQUIRE(DesiredGrant(2, 2, 0, 0, false, true) == Grant::None);
    REQUIRE(DesiredGrant(2, 2, 0, 0, true, false) == Grant::None);
    REQUIRE(DesiredGrant(2, 2, 2, 0, true, false) == Grant::Permanent);
}

TEST_CASE("Hero progression awards each earned level once and stops at ninety", "[hero][freepick]")
{
    Snapshot state;
    REQUIRE(state.Essence == 9);
    REQUIRE(state.TalentEssence == 0);
    for (std::uint32_t level = 2; level <= 90; ++level)
    {
        Snapshot next = EarnThrough(state, level);
        REQUIRE(Valid(next));
        REQUIRE(next.Essence == (level < 10 ? 9 : level));
        REQUIRE(next.TalentEssence == (level < 10 ? 0 : level - 9));
        REQUIRE(next.Version == state.Version + 1);
        REQUIRE(EarnThrough(next, level) == next);
        REQUIRE(EarnThrough(next, level - 1) == next);
        state = next;
    }
    REQUIRE(EarnThrough(state, 100) == state);
    Snapshot catchup = EarnThrough({}, 100);
    REQUIRE(catchup.Essence == 90);
    REQUIRE(catchup.TalentEssence == 81);
    REQUIRE(catchup.EarnedLevel == 90);
    REQUIRE(catchup.Version == 1);
    state = {};
    state.Version = UINT32_MAX;
    REQUIRE(EarnThrough(state, 10) == state);
}

TEST_CASE("Hero abilities and talents spend separate currencies in one atomic build", "[hero][freepick]")
{
    constexpr std::uint32_t firstTalent = 1u << 10;
    constexpr std::uint32_t secondTalent = 1u << 11;
    REQUIRE(Reduce({}, Commit(0, firstTalent), AllEntries).Code == Result::Insufficient);
    Snapshot levelTen = EarnThrough({}, 10);
    auto purchase = Reduce(levelTen, Commit(1, 2 | firstTalent), AllEntries);
    REQUIRE(purchase.Code == Result::Ok);
    REQUIRE(purchase.State.Essence == 8);
    REQUIRE(purchase.State.TalentEssence == 0);
    REQUIRE(purchase.State.Paid[10] == 1);
    auto excessive = Reduce(purchase.State, Commit(2, 2 | firstTalent | secondTalent), AllEntries);
    REQUIRE(excessive.Code == Result::Insufficient);
    REQUIRE(excessive.State == purchase.State);
    // An ability refund cannot pay for a second talent.
    REQUIRE(Reduce(purchase.State, Commit(2, firstTalent | secondTalent), AllEntries).Code == Result::Insufficient);
    auto swap = Reduce(purchase.State, Commit(2, 16 | secondTalent), AllEntries);
    REQUIRE(swap.Code == Result::Ok);
    REQUIRE(swap.State.Essence == 8);
    REQUIRE(swap.State.TalentEssence == 0);
    auto priceChanged = Entries;
    priceChanged[11].Cost = 5;
    auto refund = Reduce(swap.State, Commit(3, 0), AllEntries, priceChanged);
    REQUIRE(refund.Code == Result::Ok);
    REQUIRE(refund.State.Essence == 10);
    REQUIRE(refund.State.TalentEssence == 1);
    Snapshot nextLevel = EarnThrough(purchase.State, 11);
    REQUIRE(nextLevel.Essence == 9);
    REQUIRE(nextLevel.TalentEssence == 1);
    REQUIRE(nextLevel.Owned == purchase.State.Owned);
    REQUIRE(nextLevel.Paid == purchase.State.Paid);
    Snapshot corrupt = purchase.State;
    ++corrupt.TalentEssence;
    --corrupt.Essence;
    REQUIRE_FALSE(Valid(corrupt));
}

TEST_CASE("Hero reconnect waits for the old commit readback and logout source flush", "[hero][freepick]")
{
    OrderedOperations<unsigned> queue;
    OrderedOperations<unsigned>::Done commitDone, flushDone;
    std::vector<unsigned> starts;
    queue.Enqueue(7, [&](auto done) { starts.push_back(1); commitDone = std::move(done); });
    queue.Pump();
    // Destruction of the submitting session does not own or cancel this lane.
    queue.Enqueue(7, [&](auto done) { starts.push_back(2); flushDone = std::move(done); });
    queue.Enqueue(7, [&](auto done) { starts.push_back(3); done(); });
    queue.Enqueue(8, [&](auto done) { starts.push_back(4); done(); });
    queue.Pump();
    REQUIRE(starts == std::vector<unsigned>{1, 4});
    commitDone();
    queue.Pump();
    REQUIRE(starts == std::vector<unsigned>{1, 4, 2});
    queue.Pump();
    REQUIRE(starts.size() == 3);
    flushDone();
    queue.Pump();
    REQUIRE(starts == std::vector<unsigned>{1, 4, 2, 3});
    // Completed lanes may be reused by later refreshes or deletion.
    queue.Enqueue(7, [&](auto done) { starts.push_back(5); done(); });
    queue.Pump();
    REQUIRE(starts.back() == 5);
}

TEST_CASE("Hero expansion masks preserve every boundary and mixed-currency refunds", "[hero][freepick]")
{
    EntryMask selected;
    unsigned ae = 0, te = 0;
    for (unsigned slot : {13u, 31u, 32u, 63u, 64u, unsigned(EntryCount - 1)})
    {
        if (slot >= EntryCount || selected.has(slot)) continue;
        selected |= EntryMask::Bit(slot);
        (Entries[slot].Talent ? te : ae) += Entries[slot].Cost;
        if (Entries[slot].Mastery && !selected.has(Entries[slot].Mastery - 1))
        {
            selected |= EntryMask::Bit(Entries[slot].Mastery - 1);
            ae += Entries[Entries[slot].Mastery - 1].Cost;
        }
    }
    auto initial = EarnThrough({}, 90);
    auto purchased = Reduce(initial, Commit(initial.Version, selected), AllEntries, EconomicCatalog());
    REQUIRE(purchased.Code == Result::Ok);
    REQUIRE(purchased.State.Essence == 90 - ae);
    REQUIRE(purchased.State.TalentEssence == 81 - te);
    auto refunded = Reduce(purchased.State, Commit(purchased.State.Version, 0), AllEntries);
    REQUIRE(refunded.Code == Result::Ok);
    REQUIRE(refunded.State.Essence == 90);
    REQUIRE(refunded.State.TalentEssence == 81);
    for (unsigned slot : {0u, 31u, 32u, 63u, 64u, 72u, 127u})
    {
        auto bit = EntryMask::Bit(slot); EntryMask decoded;
        REQUIRE(EntryMask::Parse(Hex(bit), decoded));
        REQUIRE(decoded == bit);
        REQUIRE(decoded.has(slot));
        REQUIRE(DesiredGrant(bit, bit, bit, 0, false, true) == Grant::Permanent);
        REQUIRE(DesiredGrant(bit, bit, 0, 0, false, true) == Grant::None);
    }
    REQUIRE_FALSE(Parse("V3 C " + std::string(32,'a') + " " + std::string(32,'b') + " 0 " + Hex(EntryMask::Bit(127))).has_value());
}

TEST_CASE("Hero masteries charge one family fee and reject incomplete selections", "[hero][freepick]")
{
    // Check every subset after removing the unsupported shout/trap members.
    // Acquisition policy is tested separately; this exercises the family ledger.
    REQUIRE(EntryCount == 83);
    auto initial = EarnThrough({}, 90);
    auto catalog = EconomicCatalog();
    REQUIRE(Entries[78].Mastery == 0); // Rallying Cry
    REQUIRE(Entries[79].Mastery == 0); // Intimidating Shout
    REQUIRE(Entries[82].Mastery == 0); // Tar Trap
    REQUIRE(Entries[77].Mastery == 77); // Battle Shout retains its family.
    REQUIRE(Entries[81].Mastery == 81); // Freezing Trap retains its family.
    for (unsigned selection = 0; selection < 1024; ++selection)
    {
        EntryMask desired;
        unsigned cost = 0;
        for (unsigned i = 0; i < 10; ++i)
            if (selection & (1u << i)) { desired |= EntryMask::Bit(73 + i); cost += Entries[73 + i].Cost; }
        bool complete = bool(selection & 1) == bool(selection & 6) &&
            bool(selection & 8) == bool(selection & 16) && bool(selection & 128) == bool(selection & 256);
        // Freezing Trap is preview-only until its existing core gains a freeze handler.
        complete = complete && !(selection & 256);
        auto result = Reduce(initial, Commit(initial.Version, desired), AllEntries, catalog);
        REQUIRE(result.Code == (complete ? Result::Ok : Result::InvalidEntry));
        if (!complete) { REQUIRE(result.State == initial); continue; }
        REQUIRE(result.State.Essence == 90 - cost);
        REQUIRE(result.State.TalentEssence == 81);
        REQUIRE(Valid(result.State));
        auto refunded = Reduce(result.State, Commit(result.State.Version, 0), AllEntries);
        REQUIRE(refunded.Code == Result::Ok);
        REQUIRE(refunded.State.Essence == 90);
    }
}

TEST_CASE("Hero mastery swaps preserve fees and refunds preserve independent grants", "[hero][freepick]")
{
    auto family = EntryMask::Bit(73), first = EntryMask::Bit(74), second = EntryMask::Bit(75);
    auto initial = EarnThrough({}, 10);
    auto bought = Reduce(initial, Commit(initial.Version, family | first), AllEntries, EconomicCatalog());
    REQUIRE(bought.State.Essence == 8);
    auto swapped = Reduce(bought.State, Commit(bought.State.Version, family | second), AllEntries, EconomicCatalog());
    REQUIRE(swapped.Code == Result::Ok);
    REQUIRE(swapped.State.Essence == 8);
    REQUIRE(swapped.Added == second);
    REQUIRE(swapped.Removed == first);
    REQUIRE(swapped.State.Paid[73] == 1);
    REQUIRE(Reduce(bought.State, Commit(bought.State.Version, family | first | second), family | first).Code == Result::Locked);
    auto priceChanged = Entries; priceChanged[73].Cost = 7;
    auto refunded = Reduce(swapped.State, Commit(swapped.State.Version, 0), AllEntries, priceChanged);
    REQUIRE(refunded.State.Essence == 10);
    REQUIRE(DesiredGrant(second, refunded.State.Owned, second, 0, true, true) == Grant::Permanent);
    REQUIRE(DesiredGrant(second, swapped.State.Owned, second, 0, false, true) == Grant::Permanent);
    REQUIRE(Reduce(swapped.State, Commit(bought.State.Version, 0), AllEntries).Code == Result::Stale);
    // Spend all but one AE on ordinary abilities; the first member still needs two.
    auto scarce = EarnThrough({}, 10);
    scarce.Owned = EntryMask::Bit(1); scarce.Paid[1] = 9; scarce.Essence = 1;
    auto denied = Reduce(scarce, Commit(scarce.Version, scarce.Owned | family | first), AllEntries, EconomicCatalog());
    REQUIRE(denied.Code == Result::Insufficient);
    REQUIRE(denied.State == scarce);
}

TEST_CASE("Hero family ownership resolves by identity and never creates spell zero sources", "[hero][freepick]")
{
    for (std::size_t i = 0; i < EntryCount; ++i)
        REQUIRE(OwnedIndex(CatalogFor(Entries[i]), Entries[i].Advancement, Entries[i].Spell) == i);
    REQUIRE_FALSE(OwnedIndex(MasteryCatalog, 4, 0));
    REQUIRE_FALSE(OwnedIndex(RetailAbilityCatalog, 1, 0));
    auto init = InitializeSql(1, 42);
    REQUIRE(init.size() == 82); // Two wallet statements, eighty actual spells.
    std::array<std::uint64_t, EntryCount> revisions{};
    auto parents = EntryMask::Bit(73) | EntryMask::Bit(76) | EntryMask::Bit(80);
    REQUIRE(SourceSql(1, 42, parents, parents, revisions).empty());
    auto sources = SourceSql(1, 42, AllEntries, AllEntries, revisions);
    REQUIRE(sources.size() == 80);
}

TEST_CASE("Hero acquisition policy cannot be bypassed and does not change paid refunds", "[hero][freepick]")
{
    auto catalog = EconomicCatalog();
    auto first = EntryMask::Bit(1);
    auto initial = EarnThrough({}, 90);
    auto bought = Reduce(initial, Commit(initial.Version, first), AllEntries, catalog);
    REQUIRE(bought.Code == Result::Ok);
    auto originalPaid = bought.State.Paid[1];
    catalog[1].PurchaseEnabled = false;
    catalog[1].Cost = originalPaid + 5;
    REQUIRE(catalog[1].Reviewed);
    // A forged all-available mask must not bypass the catalog policy.
    auto blocked = Reduce(initial, Commit(initial.Version, first), AllEntries, catalog);
    REQUIRE(blocked.Code == Result::Locked);
    REQUIRE(blocked.State == initial);
    // Keeping an owned spell and refunding it both remain valid.
    auto kept = Reduce(bought.State, Commit(bought.State.Version, first), 0, catalog);
    REQUIRE(kept.Code == Result::Ok);
    REQUIRE(kept.State.Paid[1] == originalPaid);
    REQUIRE(DesiredGrant(first, kept.State.Owned, 0, 0, true, true) == Grant::Dependent);
    auto refunded = Reduce(kept.State, Commit(kept.State.Version, 0), 0, catalog);
    REQUIRE(refunded.Code == Result::Ok);
    REQUIRE(refunded.State.Essence == initial.Essence);
    REQUIRE(refunded.State.TalentEssence == initial.TalentEssence);
    REQUIRE(DesiredGrant(first, refunded.State.Owned, first, 0, true, true) == Grant::Permanent);
    REQUIRE(Reduce(refunded.State, Commit(refunded.State.Version, first), AllEntries, catalog).Code == Result::Locked);
}

TEST_CASE("Hero current catalog blocks restricted new purchases independently of availability", "[hero][freepick]")
{
    auto initial = EarnThrough({}, 90);
    bool restricted = false;
    for (std::size_t i = 0; i < Entries.size(); ++i)
    {
        if (!Entries[i].Reviewed || Entries[i].PurchaseEnabled)
            continue;
        restricted = true;
        auto desired = EntryMask::Bit(i);
        if (!Entries[i].Spell)
            continue; // A fee alone is already structurally invalid.
        if (Entries[i].Mastery)
            desired |= EntryMask::Bit(Entries[i].Mastery - 1);
        auto blocked = Reduce(initial, Commit(initial.Version, desired), AllEntries);
        REQUIRE(blocked.Code == Result::Locked);
        REQUIRE(blocked.State == initial);
    }
    REQUIRE(restricted);
}

TEST_CASE("Hero current validation nodes preserve persisted talent identities", "[hero][freepick]")
{
    REQUIRE(RulesRevision == 3);
    for (std::size_t i = 0; i < Entries.size(); ++i)
    {
        if (!Entries[i].Talent || !Entries[i].ValidationNode)
            continue;
        REQUIRE(OwnedIndex(TalentCatalog, Entries[i].Advancement, Entries[i].Spell) == i);
        if (Entries[i].ValidationNode != Entries[i].Advancement)
            REQUIRE_FALSE(OwnedIndex(TalentCatalog, Entries[i].ValidationNode, Entries[i].Spell));
    }
    auto statements = InitializeSql(1, 42);
    REQUIRE(statements.front().find(",601,3,0,0,9 FROM") != std::string::npos);
}
