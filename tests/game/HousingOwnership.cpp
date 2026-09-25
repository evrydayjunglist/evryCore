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
#include <cmath>
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

TEST_CASE("The first house of an account is free and every other purchase costs the plot", "[Housing][Purchase]")
{
    // NeighborhoodPlot Cost is 10,000,000 copper (1000 gold) on every district plot in the 12.1 data.
    uint64 constexpr PlotCost = UI64LIT(10000000);

    SECTION("An account with no house pays nothing (hbcd3: no money drop at the purchase)")
    {
        REQUIRE(Housing::GetPurchasePrice(0, PlotCost) == 0);
    }

    SECTION("A second house costs the plot's price")
    {
        REQUIRE(Housing::GetPurchasePrice(1, PlotCost) == PlotCost);
    }

    SECTION("Buying back a packed house costs the plot's price, because its row still counts")
    {
        REQUIRE(Housing::GetPurchasePrice(1, PlotCost) == PlotCost);
        REQUIRE(Housing::GetPurchasePrice(2, PlotCost) == PlotCost);
    }
}

TEST_CASE("A purchase unpacks the account's packed house from the same district", "[Housing][Purchase]")
{
    int32 constexpr FoundersPoint = 2735;
    int32 constexpr RazorwindShores = 2736;

    SECTION("Without a packed house a new house is built")
    {
        REQUIRE(Housing::ChoosePackedHouseToUnpack({}, RazorwindShores, false) == -1);
        REQUIRE(Housing::ChoosePackedHouseToUnpack({}, RazorwindShores, true) == -1);
    }

    SECTION("The packed house from this district is unpacked")
    {
        REQUIRE(Housing::ChoosePackedHouseToUnpack({ RazorwindShores }, RazorwindShores, false) == 0);
        REQUIRE(Housing::ChoosePackedHouseToUnpack({ FoundersPoint, RazorwindShores }, RazorwindShores, false) == 1);
    }

    SECTION("A packed house from the other district stays packed while another house may be built")
    {
        REQUIRE(Housing::ChoosePackedHouseToUnpack({ FoundersPoint }, RazorwindShores, false) == -1);
    }

    SECTION("At the house cap, a packed house from the other district is unpacked instead")
    {
        REQUIRE(Housing::ChoosePackedHouseToUnpack({ FoundersPoint }, RazorwindShores, true) == 0);
    }
}

TEST_CASE("A house that changes plot takes its exterior decor along", "[Housing][Purchase]")
{
    float constexpr QuarterTurn = 1.57079632679f;
    Position const fromPlot(100.0f, 200.0f, 10.0f, 0.0f);
    Position const toPlot(300.0f, 400.0f, 20.0f, QuarterTurn);

    Housing::PlacedDecor original;
    original.PosX = 105.0f;
    original.PosY = 202.0f;
    original.PosZ = 11.0f;

    SECTION("It keeps its place and height relative to the plot and turns with it")
    {
        Housing::PlacedDecor decor = original;
        Housing::MoveDecorBetweenPlots(fromPlot, toPlot, decor);

        // Five yards ahead of the house and two to its left, one yard up, on a plot facing a quarter turn further.
        REQUIRE(decor.PosX == Catch::Approx(298.0f).margin(0.001));
        REQUIRE(decor.PosY == Catch::Approx(405.0f).margin(0.001));
        REQUIRE(decor.PosZ == Catch::Approx(21.0f).margin(0.001));
        REQUIRE(decor.RotationX == Catch::Approx(0.0f).margin(0.0001));
        REQUIRE(decor.RotationY == Catch::Approx(0.0f).margin(0.0001));
        REQUIRE(decor.RotationZ == Catch::Approx(std::sin(QuarterTurn / 2.0f)).margin(0.0001));
        REQUIRE(decor.RotationW == Catch::Approx(std::cos(QuarterTurn / 2.0f)).margin(0.0001));
    }

    SECTION("Moving it back puts it where it was")
    {
        Housing::PlacedDecor decor = original;
        decor.RotationZ = std::sin(0.3f);
        decor.RotationW = std::cos(0.3f);
        Housing::PlacedDecor const before = decor;
        Housing::MoveDecorBetweenPlots(fromPlot, toPlot, decor);
        Housing::MoveDecorBetweenPlots(toPlot, fromPlot, decor);

        REQUIRE(decor.PosX == Catch::Approx(before.PosX).margin(0.001));
        REQUIRE(decor.PosY == Catch::Approx(before.PosY).margin(0.001));
        REQUIRE(decor.PosZ == Catch::Approx(before.PosZ).margin(0.001));
        REQUIRE(decor.RotationZ == Catch::Approx(before.RotationZ).margin(0.0001));
        REQUIRE(decor.RotationW == Catch::Approx(before.RotationW).margin(0.0001));
    }

    SECTION("On the same plot nothing changes")
    {
        Housing::PlacedDecor decor = original;
        Housing::MoveDecorBetweenPlots(fromPlot, fromPlot, decor);

        REQUIRE(decor.PosX == Catch::Approx(original.PosX).margin(0.001));
        REQUIRE(decor.PosY == Catch::Approx(original.PosY).margin(0.001));
        REQUIRE(decor.PosZ == Catch::Approx(original.PosZ).margin(0.001));
        REQUIRE(decor.RotationW == Catch::Approx(1.0f).margin(0.0001));
    }
}
