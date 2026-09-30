/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/PlayerbotGearScore.h"
#include <set>
#include <string_view>

namespace
{
    PlayerbotGearStats Stats(std::initializer_list<std::pair<PlayerbotGearStat, float>> values)
    {
        PlayerbotGearStats stats{};
        for (auto const& [stat, value] : values)
            GearStat(stats, stat) = value;
        return stats;
    }

    PlayerbotGearWorn Worn(float score) { return { true, score }; }

    // Count stands for a name that is not a stat, so Catch can print what it compared.
    PlayerbotGearStat Stat(std::string_view text) { return ParsePlayerbotGearStat(text).value_or(PlayerbotGearStat::Count); }
    PlayerbotGearWorn const Empty{};
}

TEST_CASE("Gear stat names read from the conf", "[playerbots][gear]")
{
    REQUIRE(Stat("Strength") == PlayerbotGearStat::Strength);
    REQUIRE(Stat(" critical strike ") == PlayerbotGearStat::CriticalStrike);
    REQUIRE(Stat("crit") == PlayerbotGearStat::CriticalStrike);
    REQUIRE(Stat("Weapon_DPS") == PlayerbotGearStat::WeaponDps);
    REQUIRE(Stat("vers") == PlayerbotGearStat::Versatility);
    REQUIRE(Stat("spirit") == PlayerbotGearStat::Count);
}

TEST_CASE("Conf weights change only the stats they name", "[playerbots][gear]")
{
    PlayerbotGearStats weights = PlayerbotGearRowWeights(*FindPlayerbotGearSpecRow(71));
    float const stamina = GearStat(weights, PlayerbotGearStat::Stamina);
    std::string error;
    REQUIRE(ParsePlayerbotGearWeights("Haste = 0.9; Mastery=0.1, ", weights, error));
    REQUIRE(GearStat(weights, PlayerbotGearStat::Haste) == 0.9f);
    REQUIRE(GearStat(weights, PlayerbotGearStat::Mastery) == 0.1f);
    REQUIRE(GearStat(weights, PlayerbotGearStat::Stamina) == stamina);
    REQUIRE(GearStat(weights, PlayerbotGearStat::Strength) == 1.0f);
}

TEST_CASE("A conf weight that cannot be read changes nothing", "[playerbots][gear]")
{
    PlayerbotGearStats weights = PlayerbotGearRowWeights(*FindPlayerbotGearSpecRow(63));
    PlayerbotGearStats const before = weights;
    std::string error;
    REQUIRE_FALSE(ParsePlayerbotGearWeights("Haste=0.9, Spirit=1", weights, error));
    REQUIRE((weights == before));
    REQUIRE_FALSE(error.empty());
    REQUIRE_FALSE(ParsePlayerbotGearWeights("Haste", weights, error));
    REQUIRE_FALSE(ParsePlayerbotGearWeights("Haste=fast", weights, error));
    REQUIRE_FALSE(ParsePlayerbotGearWeights("Haste=-1", weights, error));
    REQUIRE((weights == before));
}

TEST_CASE("Every specialization row is listed once and values its own main stat", "[playerbots][gear]")
{
    std::set<uint32> seen;
    for (PlayerbotGearSpecRow const& row : PLAYERBOT_GEAR_SPEC_ROWS)
    {
        REQUIRE(seen.insert(row.SpecId).second);
        PlayerbotGearStats const weights = PlayerbotGearRowWeights(row);
        float const strength = GearStat(weights, PlayerbotGearStat::Strength);
        float const agility = GearStat(weights, PlayerbotGearStat::Agility);
        float const intellect = GearStat(weights, PlayerbotGearStat::Intellect);
        REQUIRE(strength + agility + intellect == 1.0f);
        if (row.Primary == PlayerbotGearPrimary::Intellect)
            REQUIRE(GearStat(weights, PlayerbotGearStat::WeaponDps) == 0.0f);
        else
            REQUIRE(GearStat(weights, PlayerbotGearStat::WeaponDps) > 0.0f);
    }
}

TEST_CASE("A warrior prefers strength and a mage intellect on the same budget", "[playerbots][gear]")
{
    PlayerbotGearStats const arms = PlayerbotGearRowWeights(*FindPlayerbotGearSpecRow(71));
    PlayerbotGearStats const fire = PlayerbotGearRowWeights(*FindPlayerbotGearSpecRow(63));
    PlayerbotGearStats const plateOfStrength = Stats({ { PlayerbotGearStat::Strength, 20 }, { PlayerbotGearStat::Stamina, 30 }, { PlayerbotGearStat::Haste, 10 } });
    PlayerbotGearStats const robeOfIntellect = Stats({ { PlayerbotGearStat::Intellect, 20 }, { PlayerbotGearStat::Stamina, 30 }, { PlayerbotGearStat::Haste, 10 } });
    REQUIRE(ScorePlayerbotGear(plateOfStrength, arms) > ScorePlayerbotGear(robeOfIntellect, arms));
    REQUIRE(ScorePlayerbotGear(robeOfIntellect, fire) > ScorePlayerbotGear(plateOfStrength, fire));
}

TEST_CASE("Secondary stats break a tie in the way her specialization likes", "[playerbots][gear]")
{
    // Fury likes haste more than versatility.
    PlayerbotGearStats const fury = PlayerbotGearRowWeights(*FindPlayerbotGearSpecRow(72));
    PlayerbotGearStats const hasteRing = Stats({ { PlayerbotGearStat::Strength, 10 }, { PlayerbotGearStat::Haste, 8 } });
    PlayerbotGearStats const versatilityRing = Stats({ { PlayerbotGearStat::Strength, 10 }, { PlayerbotGearStat::Versatility, 8 } });
    REQUIRE(ScorePlayerbotGear(hasteRing, fury) > ScorePlayerbotGear(versatilityRing, fury));
}

TEST_CASE("The upgrade margin decides when she changes", "[playerbots][gear]")
{
    REQUIRE(PlayerbotGearIsUpgrade(102.0f, 100.0f, 1.0f));
    REQUIRE_FALSE(PlayerbotGearIsUpgrade(100.5f, 100.0f, 1.0f));
    REQUIRE_FALSE(PlayerbotGearIsUpgrade(100.0f, 100.0f, 0.0f));
    REQUIRE(PlayerbotGearIsUpgrade(100.1f, 100.0f, 0.0f));
    REQUIRE_FALSE(PlayerbotGearIsUpgrade(104.0f, 100.0f, 5.0f));
    REQUIRE(PlayerbotGearIsUpgrade(1.0f, 0.0f, 50.0f));
}

TEST_CASE("What she takes off never scores higher than what she put on", "[playerbots][gear]")
{
    // She changes only for a strictly higher score, so the piece that went back in her bags is never an upgrade over
    // the piece that replaced it, whatever the margin.
    for (float margin : { 0.0f, 1.0f, 10.0f })
    {
        float const old = 50.0f;
        float const put = 60.0f;
        REQUIRE(PlayerbotGearIsUpgrade(put, old, margin));
        REQUIRE_FALSE(PlayerbotGearIsUpgrade(old, put, margin));
    }
}

TEST_CASE("Filling an empty slot comes before any other upgrade", "[playerbots][gear]")
{
    REQUIRE(PlayerbotGearGain(1.0f, 0.0f, true) > PlayerbotGearGain(500.0f, 10.0f, false));
    REQUIRE(PlayerbotGearGain(20.0f, 10.0f, false) > PlayerbotGearGain(110.0f, 100.0f, false));
}

TEST_CASE("Her class armor comes first", "[playerbots][gear]")
{
    // A warrior's mask holds cloth, leather, mail, and plate; plate is her class armor.
    uint32 const warriorMask = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4);
    REQUIRE(PlayerbotGearClassArmor(warriorMask) == 4);
    REQUIRE(PlayerbotGearClassArmor(1u << 1) == 1);
    REQUIRE(PlayerbotGearClassArmor(0) == 0);

    uint8 const plate = 4, mail = 3, leather = 2;
    REQUIRE(PlayerbotGearArmorKindAllows(plate, plate, mail));
    REQUIRE(PlayerbotGearArmorKindAllows(plate, mail, 0));        // an empty slot
    REQUIRE(PlayerbotGearArmorKindAllows(plate, mail, leather));  // replaces a lighter stand-in
    REQUIRE(PlayerbotGearArmorKindAllows(plate, mail, mail));
    REQUIRE_FALSE(PlayerbotGearArmorKindAllows(plate, mail, plate));
    REQUIRE_FALSE(PlayerbotGearArmorKindAllows(plate, leather, plate));
    // Rings, cloaks, and the like carry no armor kind and are never held back.
    REQUIRE(PlayerbotGearArmorKindAllows(plate, 0, plate));
}

TEST_CASE("A two-hand weapon is compared against both hands", "[playerbots][gear]")
{
    PlayerbotGearReplaced const both = PlayerbotGearWhatItReplaces(PlayerbotGearHand::TwoHand, Empty, Worn(40), false, Worn(30));
    REQUIRE(both.Allowed);
    REQUIRE_FALSE(both.Nothing);
    REQUIRE(both.Score == 70.0f);

    PlayerbotGearReplaced const onlyMain = PlayerbotGearWhatItReplaces(PlayerbotGearHand::TwoHand, Empty, Worn(40), false, Empty);
    REQUIRE(onlyMain.Score == 40.0f);

    PlayerbotGearReplaced const bare = PlayerbotGearWhatItReplaces(PlayerbotGearHand::TwoHand, Empty, Empty, false, Empty);
    REQUIRE(bare.Nothing);
}

TEST_CASE("One hand against a two-hand weapon, and the off hand behind it", "[playerbots][gear]")
{
    PlayerbotGearReplaced const main = PlayerbotGearWhatItReplaces(PlayerbotGearHand::MainHand, Empty, Worn(70), true, Empty);
    REQUIRE(main.Allowed);
    REQUIRE(main.Score == 70.0f);

    PlayerbotGearReplaced const off = PlayerbotGearWhatItReplaces(PlayerbotGearHand::OffHand, Empty, Worn(70), true, Empty);
    REQUIRE_FALSE(off.Allowed);

    PlayerbotGearReplaced const offBesideOneHand = PlayerbotGearWhatItReplaces(PlayerbotGearHand::OffHand, Empty, Worn(40), false, Worn(10));
    REQUIRE(offBesideOneHand.Allowed);
    REQUIRE(offBesideOneHand.Score == 10.0f);
}

TEST_CASE("Hands do not flip back and forth", "[playerbots][gear]")
{
    // A one-hand weapon and an off hand, 40 and 30. A two-hand weapon of 75 replaces both.
    float const margin = 1.0f;
    PlayerbotGearReplaced const forTwoHand = PlayerbotGearWhatItReplaces(PlayerbotGearHand::TwoHand, Empty, Worn(40), false, Worn(30));
    REQUIRE(PlayerbotGearIsUpgrade(75.0f, forTwoHand.Score, margin));

    // Now the old one-hand weapon is judged against the two-hand weapon alone and loses; the old off hand cannot go on.
    PlayerbotGearReplaced const backToMain = PlayerbotGearWhatItReplaces(PlayerbotGearHand::MainHand, Empty, Worn(75), true, Empty);
    REQUIRE_FALSE(PlayerbotGearIsUpgrade(40.0f, backToMain.Score, margin));
    REQUIRE_FALSE(PlayerbotGearWhatItReplaces(PlayerbotGearHand::OffHand, Empty, Worn(75), true, Empty).Allowed);
}

TEST_CASE("Specializations the module has no row for get an even start", "[playerbots][gear]")
{
    PlayerbotGearStats const tank = PlayerbotGearArchetypeWeights(PlayerbotGearPrimary::Strength, PlayerbotGearRole::Tank);
    PlayerbotGearStats const caster = PlayerbotGearArchetypeWeights(PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Damage);
    REQUIRE(GearStat(tank, PlayerbotGearStat::Armor) > GearStat(caster, PlayerbotGearStat::Armor));
    REQUIRE(GearStat(caster, PlayerbotGearStat::WeaponDps) == 0.0f);
    REQUIRE(GearStat(caster, PlayerbotGearStat::Haste) == GearStat(caster, PlayerbotGearStat::Mastery));
    REQUIRE(FindPlayerbotGearSpecRow(1446) == nullptr);
}
