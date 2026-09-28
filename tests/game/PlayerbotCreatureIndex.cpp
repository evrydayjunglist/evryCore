/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/PlayerbotCreatureIndex.h"

#include <algorithm>

namespace
{
    bool Holds(std::vector<uint64_t> const& ids, uint64_t id)
    {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }

    bool Holds(std::vector<uint32_t> const& entries, uint32_t entry)
    {
        return std::find(entries.begin(), entries.end(), entry) != entries.end();
    }
}

TEST_CASE("The creature index finds an entry's spawns on one map only", "[playerbots][scale]")
{
    PlayerbotCreatureIndex index;
    REQUIRE_FALSE(index.SpawnsAreCurrent(0));

    index.BuildSpawns({ { 10, 1, 3098 }, { 11, 1, 3098 }, { 12, 0, 3098 }, { 13, 1, 3124 } }, 4);
    REQUIRE(index.SpawnsAreCurrent(4));
    REQUIRE_FALSE(index.SpawnsAreCurrent(5));

    std::vector<uint64_t> const& kalimdor = index.SpawnIds(1, 3098);
    REQUIRE(kalimdor.size() == 2);
    REQUIRE(Holds(kalimdor, 10));
    REQUIRE(Holds(kalimdor, 11));
    REQUIRE_FALSE(Holds(kalimdor, 12));

    REQUIRE(index.SpawnIds(0, 3098).size() == 1);
    REQUIRE(index.SpawnIds(1, 3124).size() == 1);
    REQUIRE(index.SpawnIds(1, 99999).empty());
    REQUIRE(index.SpawnIds(530, 3098).empty());
}

TEST_CASE("A kill credit entry includes every entry whose kill counts as it", "[playerbots][scale]")
{
    PlayerbotCreatureIndex index;
    index.BuildKillCredits({
        { 100, { 500, 0 } },
        { 101, { 500, 500 } },   // the same credit twice on one template counts once
        { 500, { 500 } },        // a template crediting itself adds nothing
        { 102, { 600 } },
    }, 4);
    REQUIRE(index.KillCreditsAreCurrent(4));

    std::vector<uint32_t> const entries = index.EntriesGivingCredit(500);
    REQUIRE(entries.size() == 3);
    REQUIRE(Holds(entries, 500));
    REQUIRE(Holds(entries, 100));
    REQUIRE(Holds(entries, 101));
    REQUIRE_FALSE(Holds(entries, 102));

    // An entry nobody else credits is still its own credit.
    std::vector<uint32_t> const alone = index.EntriesGivingCredit(777);
    REQUIRE(alone.size() == 1);
    REQUIRE(alone.front() == 777);

    REQUIRE(index.EntriesGivingCredit(0).empty());
}

TEST_CASE("Building the creature index again replaces what it held", "[playerbots][scale]")
{
    PlayerbotCreatureIndex index;
    index.BuildSpawns({ { 10, 1, 3098 } }, 1);
    index.BuildSpawns({ { 20, 1, 3124 } }, 1);
    REQUIRE(index.SpawnIds(1, 3098).empty());
    REQUIRE(index.SpawnIds(1, 3124).size() == 1);

    index.BuildKillCredits({ { 100, { 500 } } }, 1);
    index.BuildKillCredits({ { 101, { 600 } } }, 1);
    REQUIRE(index.EntriesGivingCredit(500).size() == 1);
    REQUIRE(index.EntriesGivingCredit(600).size() == 2);
}
