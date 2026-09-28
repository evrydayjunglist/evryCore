/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_PLAYERBOT_NAME_GENERATOR_H
#define EVRY_PLAYERBOT_NAME_GENERATOR_H

#include "Define.h"
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

// A made-up name for a new bot character, built from syllables so it reads like a name ("Modefil") rather than
// random letters. It is used for every race when the game's own name list has no free name left. The server still
// checks the result like any player's name (length, letters, three of a kind, reserved and profane names), and the
// factory refuses a name any character already has.
//
// Every race uses the same syllables for now. A race's own sound (harsher for orcs, softer for blood elves) can be
// added later as another set of these lists.

// A player's name may be at most this many letters (the server's MAX_PLAYER_NAME).
inline constexpr std::size_t PLAYERBOT_NAME_MAX_LETTERS = 12;

// Shorter names than this read as initials rather than names.
inline constexpr std::size_t PLAYERBOT_NAME_MIN_LETTERS = 3;

namespace PlayerbotNameParts
{
    // The sound a syllable opens with.
    inline constexpr std::array<std::string_view, 24> Onsets =
    {
        "b", "d", "f", "g", "h", "k", "l", "m", "n", "p", "r", "s",
        "t", "v", "z", "br", "dr", "gr", "kr", "tr", "th", "sh", "st", "j"
    };

    // The vowel in the middle of a syllable. Plain vowels are listed twice so they come up more often than pairs.
    inline constexpr std::array<std::string_view, 14> Vowels =
    {
        "a", "e", "i", "o", "u", "a", "e", "i", "o", "u", "ai", "ae", "ei", "ou"
    };

    // The sound a name may close with.
    inline constexpr std::array<std::string_view, 13> Endings =
    {
        "n", "l", "r", "s", "th", "k", "m", "d", "x", "sh", "nd", "rn", "g"
    };
}

namespace PlayerbotNameGeneratorDetail
{
    template <typename Random, std::size_t N>
    std::string_view PickPart(Random& random, std::array<std::string_view, N> const& parts)
    {
        return parts[random(uint32(N))];
    }

    // The server refuses three of the same letter in a row, so do not offer one.
    inline bool HasThreeInARow(std::string_view name)
    {
        for (std::size_t i = 2; i < name.size(); ++i)
            if (name[i] == name[i - 1] && name[i] == name[i - 2])
                return true;
        return false;
    }

    template <typename Random>
    std::string BuildName(Random& random, bool female)
    {
        // Two syllables most of the time, three now and then.
        uint32 const syllables = random(4u) == 0 ? 3 : 2;

        std::string name;
        for (uint32 i = 0; i < syllables; ++i)
        {
            // A name may open on its vowel ("Aren"); later syllables always open on a consonant so vowels do not pile up.
            if (i != 0 || random(5u) != 0)
                name += PickPart(random, PlayerbotNameParts::Onsets);
            name += PickPart(random, PlayerbotNameParts::Vowels);
        }

        // Women's names end on the vowel more often than men's, as they do in the game's own name lists.
        uint32 const openEndingInFour = female ? 2 : 1;
        if (random(4u) >= openEndingInFour)
            name += PickPart(random, PlayerbotNameParts::Endings);

        return name;
    }
}

// Builds one name. random(n) must return a whole number from 0 to n - 1. The result starts with a capital letter and
// has only the letters a to z after it. It is not checked against names already taken; the caller does that.
template <typename Random>
std::string MakePlayerbotName(Random& random, bool female)
{
    using namespace PlayerbotNameGeneratorDetail;

    std::string name;
    // A draw that comes out too long, too short or with three of a kind is thrown away. Almost every draw passes,
    // so the last draw is trimmed instead of looping forever on a broken random source.
    for (uint32 attempt = 0; attempt < 32; ++attempt)
    {
        name = BuildName(random, female);
        if (name.size() >= PLAYERBOT_NAME_MIN_LETTERS && name.size() <= PLAYERBOT_NAME_MAX_LETTERS && !HasThreeInARow(name))
            break;
    }

    if (name.size() > PLAYERBOT_NAME_MAX_LETTERS)
        name.resize(PLAYERBOT_NAME_MAX_LETTERS);

    if (!name.empty())
        name[0] = char(name[0] - 'a' + 'A');

    return name;
}

#endif
