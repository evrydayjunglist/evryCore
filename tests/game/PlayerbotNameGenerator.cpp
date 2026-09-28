/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/PlayerbotNameGenerator.h"
#include <unordered_set>

namespace
{
    // A fixed random source, so a failing name can be reproduced.
    struct TestRandom
    {
        uint64 State = 0x9E3779B97F4A7C15ull;

        uint32 operator()(uint32 count)
        {
            State = State * 6364136223846793005ull + 1442695040888963407ull;
            return uint32(State >> 33) % count;
        }
    };

    bool IsVowel(char c)
    {
        return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
    }
}

TEST_CASE("Made-up bot names are names a player could take", "[playerbots][names]")
{
    TestRandom random;
    for (uint32 i = 0; i < 20000; ++i)
    {
        std::string const name = MakePlayerbotName(random, (i % 2) == 1);
        INFO(name);
        REQUIRE(name.size() >= PLAYERBOT_NAME_MIN_LETTERS);
        REQUIRE(name.size() <= PLAYERBOT_NAME_MAX_LETTERS);
        REQUIRE(name[0] >= 'A');
        REQUIRE(name[0] <= 'Z');
        for (std::size_t c = 1; c < name.size(); ++c)
        {
            REQUIRE(name[c] >= 'a');
            REQUIRE(name[c] <= 'z');
        }
        REQUIRE_FALSE(PlayerbotNameGeneratorDetail::HasThreeInARow(name));

        // Every syllable has a vowel, so a name never runs four consonants together.
        std::size_t consonants = 0;
        for (char c : name)
        {
            consonants = IsVowel(char(c | 0x20)) ? 0 : consonants + 1;
            REQUIRE(consonants < 4);
        }
    }
}

TEST_CASE("Made-up bot names rarely repeat", "[playerbots][names]")
{
    TestRandom random;
    std::unordered_set<std::string> seen;
    uint32 const draws = 10000;
    for (uint32 i = 0; i < draws; ++i)
        seen.insert(MakePlayerbotName(random, (i % 2) == 1));

    // The factory refuses a taken name and draws again, so a few repeats cost only another draw.
    REQUIRE(seen.size() > draws * 9 / 10);
}

TEST_CASE("Women's made-up names end on a vowel more often than men's", "[playerbots][names]")
{
    TestRandom random;
    uint32 femaleVowelEnds = 0;
    uint32 maleVowelEnds = 0;
    for (uint32 i = 0; i < 4000; ++i)
    {
        if (IsVowel(MakePlayerbotName(random, true).back()))
            ++femaleVowelEnds;
        if (IsVowel(MakePlayerbotName(random, false).back()))
            ++maleVowelEnds;
    }

    REQUIRE(femaleVowelEnds > maleVowelEnds);
}
