/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "tc_catch2.h"
#include "Housing.h"
#include "HousingDefines.h"
#include "HousingMgr.h"
#include "Neighborhood.h"
#include "NeighborhoodCharter.h"
#include <vector>

TEST_CASE("House levels stop at 12", "[Housing][Levels]")
{
    REQUIRE(MAX_HOUSE_LEVEL == 12);
}

TEST_CASE("House level budgets match the retail captures and the online tables", "[Housing][Levels]")
{
    SECTION("Captured levels (interior, exterior, exterior fixture, room placement)")
    {
        // hbcd3 458369-458372: a level 1 house.
        HouseLevelBudgets level1 = HousingMgr::GetBudgetsForLevel(1);
        REQUIRE(level1.InteriorDecor == 910);
        REQUIRE(level1.ExteriorDecor == 200);
        REQUIRE(level1.ExteriorFixture == 1000);
        REQUIRE(level1.RoomPlacement == 19);

        // hbcd3 457768-457771: a level 2 house.
        HouseLevelBudgets level2 = HousingMgr::GetBudgetsForLevel(2);
        REQUIRE(level2.InteriorDecor == 1155);
        REQUIRE(level2.ExteriorDecor == 200);
        REQUIRE(level2.ExteriorFixture == 2000);
        REQUIRE(level2.RoomPlacement == 24);

        HouseLevelBudgets level3 = HousingMgr::GetBudgetsForLevel(3);
        REQUIRE(level3.InteriorDecor == 1450);
        REQUIRE(level3.ExteriorDecor == 250);
        REQUIRE(level3.ExteriorFixture == 3000);
        REQUIRE(level3.RoomPlacement == 30);

        // hbcd3 458447-458449: a level 5 house.
        HouseLevelBudgets level5 = HousingMgr::GetBudgetsForLevel(5);
        REQUIRE(level5.InteriorDecor == 2050);
        REQUIRE(level5.ExteriorDecor == 300);
        REQUIRE(level5.ExteriorFixture == 5000);
        REQUIRE(level5.RoomPlacement == 43);

        HouseLevelBudgets level7 = HousingMgr::GetBudgetsForLevel(7);
        REQUIRE(level7.InteriorDecor == 3180);
        REQUIRE(level7.ExteriorDecor == 350);
        REQUIRE(level7.ExteriorFixture == 5000);
        REQUIRE(level7.RoomPlacement == 68);

        // hbcd3 457807-457810: a level 9 house.
        HouseLevelBudgets level9 = HousingMgr::GetBudgetsForLevel(9);
        REQUIRE(level9.InteriorDecor == 4340);
        REQUIRE(level9.ExteriorDecor == 350);
        REQUIRE(level9.ExteriorFixture == 5000);
        REQUIRE(level9.RoomPlacement == 95);
    }

    SECTION("Every level from the online tables")
    {
        uint32 const interior[] = { 910, 1155, 1450, 1745, 2050, 2360, 3180, 3500, 4340, 4675, 5545, 5975 };
        uint32 const exterior[] = { 200, 200, 250, 250, 300, 300, 350, 350, 350, 350, 350, 350 };
        uint32 const room[] = { 19, 24, 30, 36, 43, 50, 68, 76, 95, 104, 124, 134 };
        // Levels 4 and 6 from the earlier 12.0.1 capture; levels 8 and 10 to 12 have no source and take 5000.
        uint32 const fixture[] = { 1000, 2000, 3000, 4000, 5000, 5000, 5000, 5000, 5000, 5000, 5000, 5000 };
        for (uint32 level = 1; level <= MAX_HOUSE_LEVEL; ++level)
        {
            HouseLevelBudgets budgets = HousingMgr::GetBudgetsForLevel(level);
            REQUIRE(budgets.InteriorDecor == interior[level - 1]);
            REQUIRE(budgets.ExteriorDecor == exterior[level - 1]);
            REQUIRE(budgets.RoomPlacement == room[level - 1]);
            REQUIRE(budgets.ExteriorFixture == fixture[level - 1]);
        }
    }

    SECTION("A level above the cap takes the cap's budgets")
    {
        HouseLevelBudgets top = HousingMgr::GetBudgetsForLevel(MAX_HOUSE_LEVEL);
        HouseLevelBudgets above = HousingMgr::GetBudgetsForLevel(MAX_HOUSE_LEVEL + 5);
        REQUIRE(above.InteriorDecor == top.InteriorDecor);
        REQUIRE(above.RoomPlacement == top.RoomPlacement);
    }
}

TEST_CASE("House experience needed for each level", "[Housing][Levels]")
{
    uint32 const expected[] = { 0, 10, 1200, 2400, 3700, 5700, 7900, 10300, 12900, 15750, 18850, 22200 };
    for (uint32 level = 1; level <= MAX_HOUSE_LEVEL; ++level)
        REQUIRE(HousingMgr::GetFavorThresholdForLevel(level) == expected[level - 1]);

    REQUIRE(HousingMgr::GetFavorThresholdForLevel(MAX_HOUSE_LEVEL + 1) == 22200);
}

TEST_CASE("Housing caps and gates", "[Housing][Levels]")
{
    REQUIRE(HOUSING_MAX_PET_BEDS_INTERIOR == 100);
    REQUIRE(HOUSING_MAX_PET_BEDS_EXTERIOR == 25);
    REQUIRE(HOUSING_REQUIRED_EXPANSION == EXPANSION_MIDNIGHT);
}

TEST_CASE("A charter takes one signature per Battle.net account", "[Housing][Charter]")
{
    REQUIRE(MIN_CHARTER_SIGNATURES == 10);

    uint32 constexpr Creator = 100;
    std::vector<uint32> const signers = { 200, 300 };

    SECTION("A new account signs")
    {
        REQUIRE(NeighborhoodCharter::CheckSignature(400, Creator, signers, false) == HOUSING_RESULT_SUCCESS);
    }

    SECTION("Another character of the creator's account does not")
    {
        REQUIRE(NeighborhoodCharter::CheckSignature(Creator, Creator, signers, false) == HOUSING_RESULT_PERMISSION_DENIED);
    }

    SECTION("Another character of an account that already signed does not")
    {
        REQUIRE(NeighborhoodCharter::CheckSignature(300, Creator, signers, false) == HOUSING_RESULT_DUPLICATE_CHARTER_SIGNATURE);
    }

    SECTION("An account that signed another open charter does not")
    {
        REQUIRE(NeighborhoodCharter::CheckSignature(400, Creator, signers, true) == HOUSING_RESULT_DUPLICATE_CHARTER_SIGNATURE);
    }
}

TEST_CASE("A guild's members are counted by Battle.net account", "[Housing][Guild]")
{
    std::vector<Housing::GuildMemberAccount> members =
    {
        { 1, false }, { 1, true },   // two characters of one account, one of them active
        { 2, false },                // an inactive account
        { 3, true },
        { 0, true },                 // a member whose account is not known is not counted
    };

    uint32 accounts = 0;
    uint32 activeAccounts = 0;
    Housing::CountBattlenetAccounts(members, accounts, activeAccounts);
    REQUIRE(accounts == 3);
    REQUIRE(activeAccounts == 2);
}

TEST_CASE("House access settings", "[Housing][Visiting]")
{
    HouseVisitorRelation const stranger;

    SECTION("The captured default lets anyone onto the plot and no visitor into the house")
    {
        REQUIRE(HOUSE_SETTING_DEFAULT == 0x20);
        REQUIRE(HousingMgr::AccessSettingsAllow(HOUSE_SETTING_DEFAULT, false, stranger));
        REQUIRE_FALSE(HousingMgr::AccessSettingsAllow(HOUSE_SETTING_DEFAULT, true, stranger));
    }

    SECTION("No access bit lets no visitor in")
    {
        HouseVisitorRelation everything;
        everything.Neighbor = everything.Guild = everything.Friend = everything.Party = true;
        REQUIRE_FALSE(HousingMgr::AccessSettingsAllow(0, false, everything));
        REQUIRE_FALSE(HousingMgr::AccessSettingsAllow(0, true, everything));
    }

    SECTION("A limited setting lets in only the relations it names")
    {
        HouseVisitorRelation guildmate;
        guildmate.Guild = true;
        REQUIRE(HousingMgr::AccessSettingsAllow(HOUSE_SETTING_HOUSE_ACCESS_GUILD, true, guildmate));
        REQUIRE_FALSE(HousingMgr::AccessSettingsAllow(HOUSE_SETTING_HOUSE_ACCESS_FRIENDS, true, guildmate));
        REQUIRE_FALSE(HousingMgr::AccessSettingsAllow(HOUSE_SETTING_PLOT_ACCESS_GUILD, true, guildmate));
        REQUIRE(HousingMgr::AccessSettingsAllow(HOUSE_SETTING_PLOT_ACCESS_GUILD, false, guildmate));
    }
}

TEST_CASE("Who may buy a plot in a neighborhood", "[Housing][Neighborhood]")
{
    uint32 constexpr Guild = 7;

    SECTION("A public neighborhood of the server takes only its own faction")
    {
        REQUIRE(Neighborhood::CheckResidentJoin(true, true, NEIGHBORHOOD_FACTION_HORDE, 0, HORDE, 0, false) == HOUSING_RESULT_SUCCESS);
        REQUIRE(Neighborhood::CheckResidentJoin(true, true, NEIGHBORHOOD_FACTION_HORDE, 0, ALLIANCE, 0, false) == HOUSING_RESULT_INCORRECT_FACTION);
    }

    SECTION("A guild neighborhood takes its guild's members of either faction without an invitation")
    {
        REQUIRE(Neighborhood::CheckResidentJoin(false, false, NEIGHBORHOOD_FACTION_HORDE, Guild, ALLIANCE, Guild, false) == HOUSING_RESULT_SUCCESS);
        REQUIRE(Neighborhood::CheckResidentJoin(false, false, NEIGHBORHOOD_FACTION_HORDE, Guild, HORDE, 0, true) == HOUSING_RESULT_INVALID_GUILD);
    }

    SECTION("A charter neighborhood takes either faction with an invitation")
    {
        REQUIRE(Neighborhood::CheckResidentJoin(false, false, NEIGHBORHOOD_FACTION_ALLIANCE, 0, HORDE, 0, true) == HOUSING_RESULT_SUCCESS);
        REQUIRE(Neighborhood::CheckResidentJoin(false, false, NEIGHBORHOOD_FACTION_ALLIANCE, 0, HORDE, 0, false) == HOUSING_RESULT_MISSING_PRIVATE_NEIGHBORHOOD_INVITE);
    }

    SECTION("The other faction may be invited to a charter or guild neighborhood, not to the server's public ones")
    {
        // A charter neighborhood keeps the charter's faction as its restriction; that does not refuse an invitation.
        REQUIRE(Neighborhood::IsFactionAllowed(false, NEIGHBORHOOD_FACTION_ALLIANCE, HORDE));
        REQUIRE(Neighborhood::IsFactionAllowed(false, NEIGHBORHOOD_FACTION_HORDE, ALLIANCE));
        REQUIRE(Neighborhood::IsFactionAllowed(true, NEIGHBORHOOD_FACTION_HORDE, HORDE));
        REQUIRE_FALSE(Neighborhood::IsFactionAllowed(true, NEIGHBORHOOD_FACTION_HORDE, ALLIANCE));
        REQUIRE_FALSE(Neighborhood::IsFactionAllowed(true, NEIGHBORHOOD_FACTION_ALLIANCE, HORDE));
    }
}
