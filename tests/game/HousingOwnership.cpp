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
#include "ObjectGuid.h"
#include <vector>

TEST_CASE("A house GUID matches the one retail sends", "[Housing][Ownership]")
{
    // hbcd3 1340682: HouseGUID 0xDC60000000008007 / 0x000000000354769D is the first house of Battle.net account
    // 55867037, whose house interior map 2783 is NeighborhoodMap row 7.
    ObjectGuid const guid = Housing::MakeHouseGuid(/*slot*/ 1, /*interiorNeighborhoodMapId*/ 7, /*bnetAccountId*/ 55867037);
    REQUIRE(guid.GetRawValue(1) == UI64LIT(0xDC60000000008007));
    REQUIRE(guid.GetRawValue(0) == UI64LIT(0x000000000354769D));
    REQUIRE(guid.GetHigh() == HighGuid::Housing);
    REQUIRE(guid.GetCounter() == 55867037);

    SECTION("The second house of an account differs only in the slot")
    {
        // hbcd3 457774: another account's house 0xDC60000000010007 / 0x03F2F938, slot 2.
        ObjectGuid const second = Housing::MakeHouseGuid(2, 7, 66255160);
        REQUIRE(second.GetRawValue(1) == UI64LIT(0xDC60000000010007));
        REQUIRE(second.GetRawValue(0) == UI64LIT(0x0000000003F2F938));

        ObjectGuid const sameAccountSecond = Housing::MakeHouseGuid(2, 7, 55867037);
        REQUIRE(sameAccountSecond != guid);
        REQUIRE(sameAccountSecond.GetCounter() == guid.GetCounter());
    }
}

TEST_CASE("An account owns one house per district", "[Housing][Ownership]")
{
    int32 constexpr FoundersPoint = 2735;
    int32 constexpr RazorwindShores = 2736;

    SECTION("An account with no house may buy in either district")
    {
        REQUIRE_FALSE(Housing::AccountOwnsHouseInDistrict({}, FoundersPoint));
        REQUIRE_FALSE(Housing::AccountOwnsHouseInDistrict({}, RazorwindShores));
    }

    SECTION("A house in one district leaves the other open")
    {
        std::vector<int32> const owned = { RazorwindShores };
        REQUIRE(Housing::AccountOwnsHouseInDistrict(owned, RazorwindShores));
        REQUIRE_FALSE(Housing::AccountOwnsHouseInDistrict(owned, FoundersPoint));
    }

    SECTION("A house in each district leaves neither open")
    {
        std::vector<int32> const owned = { FoundersPoint, RazorwindShores };
        REQUIRE(Housing::AccountOwnsHouseInDistrict(owned, FoundersPoint));
        REQUIRE(Housing::AccountOwnsHouseInDistrict(owned, RazorwindShores));
    }
}

TEST_CASE("A new house takes the first slot none of the account's houses uses", "[Housing][Ownership]")
{
    SECTION("The first house of an account is slot 1")
    {
        REQUIRE(Housing::FindFreeSlot({}) == 1);
    }

    SECTION("The second house is slot 2, whichever game account of the Battle.net account bought the first")
    {
        REQUIRE(Housing::FindFreeSlot({ 1 }) == 2);
    }

    SECTION("A slot freed by a deleted house is taken again, and a packed house keeps its slot")
    {
        REQUIRE(Housing::FindFreeSlot({ 2 }) == 1);
        REQUIRE(Housing::FindFreeSlot({ 2, 1 }) == 3);
    }

    SECTION("A slot listed twice counts once")
    {
        REQUIRE(Housing::FindFreeSlot({ 1, 1 }) == 2);
    }

    SECTION("No slot is left when all 255 are used")
    {
        std::vector<uint8> all;
        for (uint32 slot = 1; slot <= 255; ++slot)
            all.push_back(uint8(slot));
        REQUIRE(Housing::FindFreeSlot(all) == 0);
    }
}
