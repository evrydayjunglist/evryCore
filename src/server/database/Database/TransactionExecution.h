#ifndef TRINITY_TRANSACTION_EXECUTION_H
#define TRINITY_TRANSACTION_EXECUTION_H

#include <utility>

namespace Trinity::Database
{
template<class Attempt, class Budget>
int RetryDeadlockedTransaction(int error, int deadlockError, Attempt attempt, Budget budget)
{
    while (error == deadlockError && budget())
        error = attempt();
    return error;
}

// Shared by the real connection and failure-injection tests. A failed BEGIN
// must execute no body, and a lost COMMIT reply is failure/unknown, not success.
// Recovery happens only after leaving the transaction; an individual body
// statement must never reconnect and retry in a new autocommit session.
template<class Statements, class Begin, class Apply, class Commit, class Rollback, class Error>
int ExecuteTransactionSequence(Statements const& statements, bool& active,
    Begin begin, Apply apply, Commit commit, Rollback rollback, Error error)
{
    if (statements.empty())
        return -1;
    auto failure = [&]() { int code = error(); return code ? code : -1; };
    if (!begin())
        return failure();

    struct ActiveScope
    {
        bool& Flag;
        explicit ActiveScope(bool& flag) : Flag(flag) { Flag = true; }
        ~ActiveScope() { Flag = false; }
    } scope(active);

    auto abort = [&]()
    {
        int code = failure();
        active = false;
        rollback();
        return code;
    };
    for (auto const& statement : statements)
        if (!apply(statement))
            return abort();
    if (!commit())
        return abort();
    return 0;
}
}
#endif
