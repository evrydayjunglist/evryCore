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
#include "HousingDecorStore.h"
#include "HousingPackets.h"
#include "ObjectGuid.h"
#include "PacketOperators.h"
#include <map>
#include <set>
#include <utility>
#include <vector>

TEST_CASE("A decor GUID matches the ones retail sends", "[Housing][Decor]")
{
    SECTION("A redeemed piece")
    {
        // hled1 788898: decor 524 redeemed as 0xDC2005A30000020C / 0x00000000912C021A.
        ObjectGuid const guid = Housing::MakeDecorGuid(524, UI64LIT(0x912C021A));
        REQUIRE(guid.GetRawValue(1) == UI64LIT(0xDC2005A30000020C));
        REQUIRE(guid.GetRawValue(0) == UI64LIT(0x00000000912C021A));
        REQUIRE(guid.GetHigh() == HighGuid::Housing);
        REQUIRE(guid.GetCounter() == UI64LIT(0x912C021A));
    }

    SECTION("A starter piece")
    {
        // hbcd3 1402903: the exit door's decor, 10952, is 0xDC2005A300002AC8 / 0x0000000091090076.
        ObjectGuid const guid = Housing::MakeDecorGuid(10952, UI64LIT(0x91090076));
        REQUIRE(guid.GetRawValue(1) == UI64LIT(0xDC2005A300002AC8));
        REQUIRE(guid.GetRawValue(0) == UI64LIT(0x0000000091090076));
    }

    SECTION("The saved low part gives the same GUID back")
    {
        ObjectGuid const made = Housing::MakeDecorGuid(12247, UI64LIT(0x6AA00869));
        ObjectGuid const reloaded = Housing::MakeDecorGuid(12247, made.GetCounter());
        REQUIRE(reloaded == made);
    }

    SECTION("New pieces never share a GUID, also across entries")
    {
        Housing::NoteDecorDbId(UI64LIT(0x91090081));
        std::set<ObjectGuid::LowType> lows;
        for (uint32 i = 0; i < 16; ++i)
        {
            ObjectGuid const guid = Housing::NewDecorGuid(i % 2 ? 1700 : 2549);
            REQUIRE(guid.GetCounter() > UI64LIT(0x91090081));
            REQUIRE(lows.insert(guid.GetCounter()).second);
        }
    }
}

TEST_CASE("A redeem is only allowed for owed decor", "[Housing][Decor]")
{
    int32 constexpr OrdinaryFlags = 3;

    SECTION("Nothing is owed of decor with no starting quantity and no earned reward, so the redeem is refused")
    {
        REQUIRE(HousingDecorStore::GetOwedCount(/*startingQuantity*/ 0, OrdinaryFlags, /*earned*/ 0, /*redeemed*/ 0) == 0);
    }

    SECTION("The starting quantity is owed until it is redeemed")
    {
        // 524 has StartingQuantity 10 in 12.1 HouseDecor and was redeemed in hled1 788894.
        REQUIRE(HousingDecorStore::GetOwedCount(10, OrdinaryFlags, 0, 0) == 10);
        REQUIRE(HousingDecorStore::GetOwedCount(10, OrdinaryFlags, 0, 9) == 1);
        REQUIRE(HousingDecorStore::GetOwedCount(10, OrdinaryFlags, 0, 10) == 0);
        REQUIRE(HousingDecorStore::GetOwedCount(10, OrdinaryFlags, 0, 12) == 0);
    }

    SECTION("The DO NOT USE platforms are never owed")
    {
        REQUIRE(HousingDecorStore::GetOwedCount(4, HOUSE_DECOR_FLAGS_DO_NOT_USE, 0, 0) == 0);
    }

    SECTION("Each earned retroactive reward owes one more")
    {
        // 1271 has no starting quantity; RetroactiveDecorReward row 9 owes it for achievement 40894.
        REQUIRE(HousingDecorStore::GetOwedCount(0, OrdinaryFlags, 1, 0) == 1);
        REQUIRE(HousingDecorStore::GetOwedCount(0, OrdinaryFlags, 1, 1) == 0);
        REQUIRE(HousingDecorStore::GetOwedCount(2, OrdinaryFlags, 1, 2) == 1);
    }
}

TEST_CASE("A house purchase's starter pieces use up their starting quantity", "[Housing][Decor]")
{
    // 12.1 HouseDecor: StartingQuantity and Flags of the Horde starter set and the Alliance exit door.
    struct DecorRow { int32 StartingQuantity; int32 Flags; };
    std::map<uint32, DecorRow> const houseDecor =
    {
        { 1700, { 2, 3 } }, { 81, { 1, 3 } }, { 2549, { 0, 3 } }, { 10952, { 1, 34 } }, { 8910, { 0, 3 } }, { 9144, { 1, 34 } },
    };

    // What Housing::PlaceStarterDecor does for each piece it makes: count it as redeemed while it is owed.
    auto purchase = [&houseDecor](std::vector<uint32> const& starterSet, std::map<uint32, uint32>& redeemed)
    {
        for (uint32 decorEntryId : starterSet)
        {
            DecorRow const& row = houseDecor.at(decorEntryId);
            if (HousingDecorStore::StarterPieceUsesStartingQuantity(row.StartingQuantity, row.Flags, redeemed[decorEntryId]))
                ++redeemed[decorEntryId];
        }
    };
    auto owed = [&houseDecor](std::map<uint32, uint32>& redeemed, uint32 decorEntryId)
    {
        DecorRow const& row = houseDecor.at(decorEntryId);
        return HousingDecorStore::GetOwedCount(row.StartingQuantity, row.Flags, 0, redeemed[decorEntryId]);
    };

    SECTION("After the Horde starter set nothing of it is left to redeem")
    {
        // hbcd3 1299364-1299534: the purchase made these seven pieces, in this order.
        std::map<uint32, uint32> redeemed;
        purchase({ 1700, 81, 2549, 10952, 8910, 1700, 2549 }, redeemed);
        REQUIRE(redeemed[1700] == 2);
        REQUIRE(redeemed[81] == 1);
        REQUIRE(redeemed[10952] == 1);
        REQUIRE(redeemed[2549] == 0);
        REQUIRE(redeemed[8910] == 0);
        for (uint32 decorEntryId : { 1700u, 81u, 10952u, 2549u, 8910u })
            REQUIRE(owed(redeemed, decorEntryId) == 0);
    }

    SECTION("After the Alliance exit door its front door is not owed again")
    {
        std::map<uint32, uint32> redeemed;
        purchase({ 9144 }, redeemed);
        REQUIRE(owed(redeemed, 9144) == 0);
    }

    SECTION("A second purchase does not count past the starting quantity")
    {
        std::map<uint32, uint32> redeemed;
        purchase({ 1700, 81, 2549, 10952, 8910, 1700, 2549 }, redeemed);
        purchase({ 1700, 81, 2549, 10952, 8910, 1700, 2549 }, redeemed);
        REQUIRE(redeemed[1700] == 2);
        REQUIRE(redeemed[81] == 1);
        REQUIRE(redeemed[10952] == 1);
    }
}

TEST_CASE("A retroactive decor reward is earned by its criteria", "[Housing][Decor]")
{
    auto hasAchievement = [](uint32 achievementId) { return achievementId == 40894; };
    auto hasQuest = [](uint32 questId) { return questId == 48897; };
    std::vector<std::pair<int32, int32>> const achievementOnly = { { 40894, 0 } };
    std::vector<std::pair<int32, int32>> const twoQuests = { { 0, 48897 }, { 0, 47432 } };

    REQUIRE(HousingDecorStore::IsRetroactiveRewardEarned(RETROACTIVE_DECOR_REWARD_FLAG_ALL_CRITERIA_REQUIRED, achievementOnly, hasAchievement, hasQuest));
    REQUIRE_FALSE(HousingDecorStore::IsRetroactiveRewardEarned(RETROACTIVE_DECOR_REWARD_FLAG_ALL_CRITERIA_REQUIRED, { { 26760, 0 } }, hasAchievement, hasQuest));

    // With every criteria row needed, one of two quests is not enough; without the flag it is.
    REQUIRE_FALSE(HousingDecorStore::IsRetroactiveRewardEarned(RETROACTIVE_DECOR_REWARD_FLAG_ALL_CRITERIA_REQUIRED, twoQuests, hasAchievement, hasQuest));
    REQUIRE(HousingDecorStore::IsRetroactiveRewardEarned(RETROACTIVE_DECOR_REWARD_FLAG_NONE, twoQuests, hasAchievement, hasQuest));

    REQUIRE_FALSE(HousingDecorStore::IsRetroactiveRewardEarned(RETROACTIVE_DECOR_REWARD_FLAG_NONE, {}, hasAchievement, hasQuest));
}

TEST_CASE("An item-granted piece records the item as retail does", "[Housing][Decor]")
{
    // hled1 789060-789062: decor 1163 from item 0x...4000000A7D89D45C on realm 162 (hbcd3 1783383).
    REQUIRE(HousingDecorStore::MakeItemSourceValue(162, UI64LIT(0x4000000A7D89D45C)) == "162-0-4000000A7D89D45C");
}

namespace
{
void WriteDecorPlace(WorldPacket& wire, bool withQuaternion)
{
    // hbcd3 1443057: decor 12247 placed in room 1 of the house.
    wire << ObjectGuid::Create<HighGuid::Housing>(1, 1443, 12247, UI64LIT(0x6AA00869));
    wire << float(-979.10394f) << float(-993.3236f) << float(0.03240151f);
    wire << float(-3.1388304f) << float(0.0f) << float(0.0f);
    if (withQuaternion)
        wire << float(0.0f) << float(0.0f) << float(0.99999f) << float(0.00138f);
    wire << float(1.0f);
    wire << ObjectGuid::Empty;
    wire << ObjectGuid::Create<HighGuid::Housing>(2, 0, 1, 1);
    wire << ObjectGuid::Empty;
    wire << int32(25);
}
}

TEST_CASE("Decor placement is read in either float layout", "[Housing][Decor][Packets]")
{
    SECTION("Seven floats, as the 12.0.7 captures have")
    {
        WorldPacket wire(CMSG_HOUSING_DECOR_PLACE);
        WriteDecorPlace(wire, false);
        REQUIRE(wire.size() == 54); // hbcd3 1443057 is 54 bytes

        WorldPackets::Housing::HousingDecorPlace packet(std::move(wire));
        packet.Read();
        REQUIRE(packet.FloatCount == 7);
        REQUIRE(packet.DecorGuid == ObjectGuid::Create<HighGuid::Housing>(1, 1443, 12247, UI64LIT(0x6AA00869)));
        REQUIRE(packet.Position.Pos.GetPositionX() == Catch::Approx(-979.10394f));
        REQUIRE(packet.Rotation.Pos.GetPositionX() == Catch::Approx(-3.1388304f));
        REQUIRE(packet.Scale == Catch::Approx(1.0f));
        REQUIRE(packet.RoomGuid == ObjectGuid::Create<HighGuid::Housing>(2, 0, 1, 1));
        REQUIRE(packet.AnchorMeshObjectGuid.IsEmpty());
        REQUIRE(packet.AttachPoint == 25);
    }

    SECTION("Eleven floats, as the port read the 12.1 client")
    {
        WorldPacket wire(CMSG_HOUSING_DECOR_PLACE);
        WriteDecorPlace(wire, true);
        REQUIRE(wire.size() == 70);

        WorldPackets::Housing::HousingDecorPlace packet(std::move(wire));
        packet.Read();
        REQUIRE(packet.FloatCount == 11);
        REQUIRE(packet.Rotation.Pos.GetPositionX() == Catch::Approx(-3.1388304f));
        REQUIRE(packet.Quaternion[2] == Catch::Approx(0.99999f));
        REQUIRE(packet.Scale == Catch::Approx(1.0f));
        REQUIRE(packet.RoomGuid == ObjectGuid::Create<HighGuid::Housing>(2, 0, 1, 1));
        REQUIRE(packet.AttachPoint == 25);
    }

    SECTION("A decor move in either layout")
    {
        for (bool withQuaternion : { false, true })
        {
            WorldPacket wire(CMSG_HOUSING_DECOR_MOVE);
            wire << ObjectGuid::Create<HighGuid::Housing>(1, 1443, 81, UI64LIT(0x91090075));
            wire << float(0.5f) << float(1.5f) << float(2.5f);
            wire << float(1.5708f) << float(0.0f) << float(0.0f);
            if (withQuaternion)
                wire << float(0.0f) << float(0.0f) << float(0.7071f) << float(0.7071f);
            wire << float(1.25f);
            wire << ObjectGuid::Empty;
            wire << ObjectGuid::Create<HighGuid::Housing>(2, 0, 1, 1);
            wire << ObjectGuid::Empty;
            wire << int32(-1) << uint8(2) << uint8(3);
            wire.WriteBit(true);
            wire.FlushBits();

            WorldPackets::Housing::HousingDecorMove packet(std::move(wire));
            packet.Read();
            REQUIRE(packet.FloatCount == (withQuaternion ? 11 : 7));
            REQUIRE(packet.Position.Pos.GetPositionZ() == Catch::Approx(2.5f));
            REQUIRE(packet.Scale == Catch::Approx(1.25f));
            REQUIRE(packet.Field_80 == -1);
            REQUIRE(packet.Field_86 == 3);
            REQUIRE(packet.IsBasicMove);
        }
    }
}
