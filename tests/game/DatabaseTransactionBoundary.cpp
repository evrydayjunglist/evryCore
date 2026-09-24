#include "tc_catch2.h"
#include "TransactionExecution.h"
#include <array>
#include <string>
#include <vector>

namespace
{
struct DatabaseFixture
{
    bool Active = false;
    bool Connected = true;
    bool CommitAppliedBeforeReplyLoss = false;
    int FailAt = -1;
    int Error = 0;
    int Durable = 8;
    int Working = 8;
    int Step = 0;
    int ReplayedBodyStatements = 0;
    std::vector<std::string> Calls;

    bool Execute(char const* name, int delta = 0)
    {
        Calls.emplace_back(name);
        bool body = Calls.back() == "body";
        if (Step++ == FailAt)
        {
            if (Calls.back() == "commit" && CommitAppliedBeforeReplyLoss)
                Durable = Working;
            Error = 2013;
            Connected = false;
            if (body && !Active)
            {
                // Models the old dangerous reconnect-and-retry behavior.
                Connected = true;
                Durable += delta;
                ++ReplayedBodyStatements;
            }
            return false;
        }
        if (Calls.back() == "begin")
            Working = Durable;
        else if (body)
            Working += delta;
        else if (Calls.back() == "commit")
            Durable = Working;
        return true;
    }

    int Run()
    {
        std::array statements{-2, -2};
        return Trinity::Database::ExecuteTransactionSequence(statements, Active,
            [&]() { REQUIRE_FALSE(Active); return Execute("begin"); },
            [&](int delta) { REQUIRE(Active); return Execute("body", delta); },
            [&]() { REQUIRE(Active); return Execute("commit"); },
            [&]()
            {
                REQUIRE_FALSE(Active);
                Calls.emplace_back("rollback");
                Connected = true;
                Working = Durable;
                Error = 0; // Recovery must not overwrite the returned original error.
            },
            [&]() { return Error; });
    }
};
}

TEST_CASE("Database transaction checks BEGIN and every body boundary", "[database][transaction][hero]")
{
    for (int failure = 0; failure <= 3; ++failure)
    {
        DatabaseFixture database;
        database.FailAt = failure;
        REQUIRE(database.Run() == 2013);
        REQUIRE(database.Durable == 8);
        REQUIRE(database.ReplayedBodyStatements == 0);
        REQUIRE_FALSE(database.Active);
        if (failure == 0)
            REQUIRE(database.Calls == std::vector<std::string>{"begin"});
        else
            REQUIRE(database.Calls.back() == "rollback");
    }
}

TEST_CASE("Lost commit reply remains unknown even when commit reached storage", "[database][transaction][hero]")
{
    DatabaseFixture database;
    database.FailAt = 3;
    database.CommitAppliedBeforeReplyLoss = true;
    REQUIRE(database.Run() == 2013);
    REQUIRE(database.Durable == 4);
    REQUIRE(database.ReplayedBodyStatements == 0);
    REQUIRE_FALSE(database.Active);
    // The caller must read its durable request receipt; it must not blindly spend again.
}

TEST_CASE("Successful and empty transactions preserve boundary state", "[database][transaction][hero]")
{
    DatabaseFixture database;
    REQUIRE(database.Run() == 0);
    REQUIRE(database.Durable == 4);
    REQUIRE_FALSE(database.Active);
    REQUIRE(database.Calls == std::vector<std::string>{"begin", "body", "body", "commit"});
    std::array<int, 0> empty;
    int calls = 0;
    auto never = [&]() { ++calls; return true; };
    REQUIRE(Trinity::Database::ExecuteTransactionSequence(empty, database.Active,
        never, [&](int) { ++calls; return true; }, never, never, [&]() { return 0; }) == -1);
    REQUIRE(calls == 0);
    REQUIRE_FALSE(database.Active);
}

TEST_CASE("Deadlock retries stop when a later commit has unknown outcome", "[database][transaction][hero]")
{
    DatabaseFixture database;
    database.FailAt = 3;
    database.CommitAppliedBeforeReplyLoss = true;
    int attempts = 0;
    int result = Trinity::Database::RetryDeadlockedTransaction(1213, 1213,
        [&]() { ++attempts; return database.Run(); }, []() { return true; });
    REQUIRE(result == 2013);
    REQUIRE(attempts == 1);
    REQUIRE(database.Durable == 4);
    int budget = 0;
    result = Trinity::Database::RetryDeadlockedTransaction(1213, 1213,
        []() { return 1213; }, [&]() { return budget++ < 5; });
    REQUIRE(result == 1213);
    REQUIRE(budget == 6);
    attempts = 0;
    REQUIRE(Trinity::Database::RetryDeadlockedTransaction(2013, 1213,
        [&]() { ++attempts; return 0; }, []() { return true; }) == 2013);
    REQUIRE(attempts == 0);
}
