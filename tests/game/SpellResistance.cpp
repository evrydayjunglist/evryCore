/* This file is part of the TrinityCore Project. See AUTHORS file for Copyright information.
 * Licensed under the GNU General Public License, version 2 or later. */

#include "tc_catch2.h"
#include "Creature.h"
#include "DB2Stores.h"
#include "DummyData.h"
#include "Unit.h"

namespace
{
void LoadFlightDefaults()
{
    static UnitTestDataLoader::DB2<FlightCapabilityEntry, &FlightCapabilityEntry::ID> flight(sFlightCapabilityStore);
    if (!sFlightCapabilityStore.LookupEntry(1))
    {
        auto loader = flight.Loader();
        loader.Add().ID = 1;
    }
}
}

TEST_CASE("Legacy creature resistance does not remove retail spell damage", "[Combat][SpellResistance]")
{
    LoadFlightDefaults();
    Creature victim;
    victim.SetMaxHealth(1000);
    victim.SetHealth(1000);
    for (SpellSchools school : { SPELL_SCHOOL_FIRE, SPELL_SCHOOL_NATURE,
        SPELL_SCHOOL_FROST, SPELL_SCHOOL_SHADOW, SPELL_SCHOOL_ARCANE })
    {
        for (int32 resistance : { 0, 1, 5, 100, 500 })
        {
            CAPTURE(school, resistance);
            victim.SetResistance(school, resistance);
            DamageInfo damage(nullptr, &victim, 100, nullptr, SpellSchoolMask(1 << school), SPELL_DIRECT_DAMAGE, BASE_ATTACK);
            Unit::CalcAbsorbResist(damage);
            CHECK(damage.GetDamage() == 100);
            CHECK(damage.GetResist() == 0);
            CHECK(damage.GetAbsorb() == 0);
        }
    }
}

TEST_CASE("Retail resistance removal preserves explicit damage accounting", "[Combat][SpellResistance]")
{
    LoadFlightDefaults();
    Creature victim;
    victim.SetMaxHealth(1000);
    victim.SetHealth(1000);
    victim.SetResistance(SPELL_SCHOOL_SHADOW, 1);
    DamageInfo damage(nullptr, &victim, 100, nullptr, SPELL_SCHOOL_MASK_SHADOW, SPELL_DIRECT_DAMAGE, BASE_ATTACK);
    // Ability scripts can still explicitly absorb or resist a hit.
    damage.AbsorbDamage(25);
    damage.ResistDamage(10);
    Unit::CalcAbsorbResist(damage);
    CHECK(damage.GetDamage() == 65);
    CHECK(damage.GetAbsorb() == 25);
    CHECK(damage.GetResist() == 10);
}

TEST_CASE("Explicit school immunity remains separate from legacy resistance", "[Combat][SpellResistance]")
{
    LoadFlightDefaults();
    Creature victim;
    victim.SetResistance(SPELL_SCHOOL_SHADOW, 500);
    CHECK_FALSE(victim.IsImmunedToDamage(SPELL_SCHOOL_MASK_SHADOW));
    victim.ApplySpellImmune(123, IMMUNITY_SCHOOL, SPELL_SCHOOL_MASK_SHADOW, true);
    CHECK(victim.IsImmunedToDamage(SPELL_SCHOOL_MASK_SHADOW));
    CHECK_FALSE(victim.IsImmunedToDamage(SPELL_SCHOOL_MASK_FIRE));
    victim.ApplySpellImmune(123, IMMUNITY_SCHOOL, SPELL_SCHOOL_MASK_SHADOW, false);
    CHECK_FALSE(victim.IsImmunedToDamage(SPELL_SCHOOL_MASK_SHADOW));
}
