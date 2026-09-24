#include "tc_catch2.h"
#include "../../modules/mod-hero/src/HeroFreePick.h"
#include "../../modules/mod-hero/src/HeroFreePickQueue.h"

using namespace HeroFreePick;

namespace
{
Request Commit(std::uint32_t version, std::uint32_t mask)
{
    return {true, std::string(32, 'a'), std::string(32, 'b'), version, mask};
}
constexpr std::uint32_t Supported = 2 | 8 | 16 | 32 | 128 | 256;
}

TEST_CASE("Hero requests are bounded and canonical", "[hero][freepick]")
{
    std::string prefix = "V1 C " + std::string(32, 'a') + " " + std::string(32, 'b') + " ";
    REQUIRE(Parse(prefix + "0 18").has_value());
    for (std::string suffix : {"0 1024", "-1 18", "4294967296 18", "00 18", "0 18 extra", "0 18\n", "0  18", "0 18 "})
        REQUIRE_FALSE(Parse(prefix + suffix).has_value());
    REQUIRE_FALSE(Parse(std::string(256, 'a')).has_value());
    REQUIRE_FALSE(Parse("V1").has_value());
    REQUIRE_FALSE(Parse("V1 S").has_value());
    REQUIRE_FALSE(Parse("V2 S " + std::string(32, 'a') + " " + std::string(32, 'b')).has_value());
    REQUIRE(Parse("V1 S " + std::string(32, 'a') + " " + std::string(32, 'b')).has_value());
}

TEST_CASE("Hero atomic desired build charges and refunds original paid amounts", "[hero][freepick]")
{
    Snapshot initial;
    Change purchase = Reduce(initial, Commit(0, 18), Supported);
    REQUIRE(purchase.Code == Result::Ok);
    REQUIRE(purchase.State.Essence == 4);
    REQUIRE(purchase.State.Paid[1] == 2);
    REQUIRE(purchase.State.Paid[4] == 2);
    auto changedCatalog = Entries;
    changedCatalog[1].Cost = 7;
    Change refund = Reduce(purchase.State, Commit(1, 16), Supported, changedCatalog);
    REQUIRE(refund.Code == Result::Ok);
    REQUIRE(refund.State.Essence == 6);
    REQUIRE(refund.Removed == 2);
    REQUIRE(refund.State.Paid[1] == 0);
    REQUIRE(Reduce(refund.State, Commit(1, 16), Supported).Code == Result::Stale);
    REQUIRE(initial == Snapshot());
}

TEST_CASE("Hero exact funds and whole-build rejection cannot partially apply", "[hero][freepick]")
{
    Snapshot initial;
    Change exact = Reduce(initial, Commit(0, 2 | 8 | 16 | 32), Supported);
    REQUIRE(exact.Code == Result::Ok);
    REQUIRE(exact.State.Essence == 0);
    Change excessive = Reduce(initial, Commit(0, Supported), Supported);
    REQUIRE(excessive.Code == Result::Insufficient);
    REQUIRE(excessive.State == initial);
    Change invalid = Reduce(exact.State, Commit(1, 1 | 2), AllEntries);
    REQUIRE(invalid.Code == Result::InvalidEntry);
    REQUIRE(invalid.State == exact.State);
    Change locked = Reduce(initial, Commit(0, 2 | 16), 2);
    REQUIRE(locked.Code == Result::Locked);
    REQUIRE(locked.State == initial);
    Change swap = Reduce(exact.State, Commit(1, 2 | 8 | 16 | 128), Supported);
    REQUIRE(swap.Code == Result::Ok);
    REQUIRE(swap.State.Essence == 0);
    REQUIRE(swap.Removed == 32);
    REQUIRE(swap.Added == 128);
}

TEST_CASE("Hero snapshots and replay payloads reject corruption and overflow", "[hero][freepick]")
{
    Snapshot state;
    state.Essence = 9;
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
    ++b.Desired;
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
    REQUIRE(refund.State.Essence == 8);
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
