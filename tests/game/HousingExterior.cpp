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
#include "DB2Structure.h"
#include "GridNotifiers.h"
#include "HousingDecorEntity.h"
#include "HousingDefines.h"
#include "HousingMgr.h"
#include "ObjectGuid.h"
#include "QuaternionData.h"
#include <cmath>
#include <limits>

namespace
{
    // GameObjects.db2 row 582073 in the current client: "Plot - Plot 13" of Razorwind Shores (map 2736).
    GameObjectsEntry MakeRazorwindPlot13Row()
    {
        GameObjectsEntry row{};
        row.ID = 582073;
        row.OwnerID = 2736;
        row.Pos.X = 886.38367f;
        row.Pos.Y = -577.75696f;
        row.Pos.Z = 1.4168056f;
        row.Rot = { 0.0f, 0.0f, 0.99965727f, -0.026178394f };
        return row;
    }

    // ExteriorComponentHook 17262 in the current client: the door hook of the Orc House wall 1004.
    ExteriorComponentHookEntry MakeHook17262()
    {
        ExteriorComponentHookEntry hook{};
        hook.Position = { 15.1861f, -9.0082998f, 0.0f };
        hook.Rotation = { 0.0f, 0.0f, 22.6199f };
        hook.ID = 17262;
        hook.ExteriorComponentTypeID = 11;
        hook.ExteriorComponentID = 1004;
        return hook;
    }

    // q and -q are the same turn; compare with w made positive.
    QuaternionData WithPositiveW(QuaternionData q)
    {
        if (q.w < 0.0f)
            q = QuaternionData(-q.x, -q.y, -q.z, -q.w);
        return q;
    }

    // Room, exterior root at rootLocal/rootRot, wall 1004 at the root's origin, entry 976 on hook 17262, and the
    // Entity at 976's EntryOffset (-4.4534, 0.3277, 2.4228), where the front door stands.
    void ComposeFrontDoor(Position const& rootLocal, QuaternionData const& rootRot, Position& door, QuaternionData& doorRot)
    {
        Position room;
        QuaternionData roomRot;
        GameObjectsEntry const row = MakeRazorwindPlot13Row();
        REQUIRE(HousingMgr::GetRoomAnchor(&row, 2736, room, roomRot));

        QuaternionData const identity(0.0f, 0.0f, 0.0f, 1.0f);
        ExteriorComponentHookEntry const hook = MakeHook17262();

        Position root, wall, entry;
        QuaternionData rootWorldRot, wallRot, entryRot;
        HousingMgr::ComposeAttachment(room, roomRot, rootLocal, rootRot, root, rootWorldRot);
        HousingMgr::ComposeAttachment(root, rootWorldRot, Position(), identity, wall, wallRot);
        HousingMgr::ComposeAttachment(wall, wallRot, Position(hook.Position[0], hook.Position[1], hook.Position[2]),
            HousingMgr::GetHookRotation(hook), entry, entryRot);
        HousingMgr::ComposeAttachment(entry, entryRot, Position(-4.4534f, 0.3277f, 2.4228f), identity, door, doorRot);
    }
}

TEST_CASE("A plot's room stands at its GameObjects.db2 plot row, turned half a turn further", "[Housing][Exterior]")
{
    // hbcd3 1299610: plot 13's room at 886.38367, -577.75696, 1.4168056, orientation 0.05236292.
    GameObjectsEntry const row = MakeRazorwindPlot13Row();
    Position position;
    QuaternionData rotation;
    REQUIRE(HousingMgr::GetRoomAnchor(&row, 2736, position, rotation));
    rotation = WithPositiveW(rotation);
    REQUIRE(position.GetPositionX() == Catch::Approx(886.38367f));
    REQUIRE(position.GetPositionY() == Catch::Approx(-577.75696f));
    REQUIRE(position.GetPositionZ() == Catch::Approx(1.4168056f));
    REQUIRE(position.GetOrientation() == Catch::Approx(0.05236292f).margin(0.00001f));
    REQUIRE(rotation.x == Catch::Approx(0.0f).margin(0.000001f));
    REQUIRE(rotation.y == Catch::Approx(0.0f).margin(0.000001f));
    REQUIRE(rotation.z == Catch::Approx(std::sin(0.05236292f / 2.0f)).margin(0.00001f));
    REQUIRE(rotation.w == Catch::Approx(std::cos(0.05236292f / 2.0f)).margin(0.00001f));

    // hled1 267033: plot 5's room faces 1.0297484; its row (582074) turns the other way round.
    GameObjectsEntry plot5 = row;
    plot5.ID = 582074;
    plot5.Pos.X = 718.92188f;
    plot5.Pos.Y = -396.22397f;
    plot5.Pos.Z = 2.1598611f;
    plot5.Rot = { 0.0f, 0.0f, 0.87035471f, -0.49242535f };
    REQUIRE(HousingMgr::GetRoomAnchor(&plot5, 2736, position, rotation));
    REQUIRE(position.GetOrientation() == Catch::Approx(1.0297484f).margin(0.00001f));

    // A row on another map, or no row, is no anchor.
    REQUIRE_FALSE(HousingMgr::GetRoomAnchor(&row, 2735, position, rotation));
    REQUIRE_FALSE(HousingMgr::GetRoomAnchor(nullptr, 2736, position, rotation));
}

TEST_CASE("A piece on an exterior hook is turned the opposite way to the hook's DB2 angle", "[Housing][Exterior]")
{
    // hbcd3 1310953: the entry 976 on hook 17262 arrives turned z -0.1961155, w 0.9805808.
    QuaternionData rotation = WithPositiveW(HousingMgr::GetHookRotation(MakeHook17262()));
    REQUIRE(rotation.x == Catch::Approx(0.0f).margin(0.000001f));
    REQUIRE(rotation.y == Catch::Approx(0.0f).margin(0.000001f));
    REQUIRE(rotation.z == Catch::Approx(-0.1961155f).margin(0.00001f));
    REQUIRE(rotation.w == Catch::Approx(0.9805808f).margin(0.00001f));

    // hled1 824232-824237: the piece on the roof's hook 17222, whose DB2 angle is 90 degrees, arrives turned
    // z -0.7071066, w 0.70710695.
    ExteriorComponentHookEntry hook{};
    hook.Rotation = { 0.0f, 0.0f, 90.0f };
    rotation = WithPositiveW(HousingMgr::GetHookRotation(hook));
    REQUIRE(rotation.z == Catch::Approx(-0.7071066f).margin(0.00001f));
    REQUIRE(rotation.w == Catch::Approx(0.70710695f).margin(0.00001f));
}

TEST_CASE("The front door stands where the room, root, wall, entry and entry offset put it", "[Housing][Exterior]")
{
    // hbcd3 1310359 and 1310816: after the purchase the root was at (-3.557434, 4.4353027, 0) turned z 0.775054,
    // w 0.63189507, and the door's stationary position was 886.53827, -560.9102, 3.8396058, orientation 1.4311743.
    Position door;
    QuaternionData doorRot;
    ComposeFrontDoor(Position(-3.557434f, 4.4353027f, 0.0f), QuaternionData(0.0f, 0.0f, 0.775054f, 0.63189507f), door, doorRot);
    REQUIRE(door.GetPositionX() == Catch::Approx(886.53827f).margin(0.001f));
    REQUIRE(door.GetPositionY() == Catch::Approx(-560.9102f).margin(0.001f));
    REQUIRE(door.GetPositionZ() == Catch::Approx(3.8396058f).margin(0.001f));
    REQUIRE(door.GetOrientation() == Catch::Approx(1.4311743f).margin(0.0001f));

    // hled1 257038 and 282029: the owner moved the root to its room's centre turned a quarter turn, and the door
    // followed to 892.78076, -566.205, 3.8396058, orientation 1.2283689.
    ComposeFrontDoor(Position(0.0f, 0.0f, 0.0f), QuaternionData(0.0f, 0.0f, 0.7071066f, 0.70710695f), door, doorRot);
    REQUIRE(door.GetPositionX() == Catch::Approx(892.78076f).margin(0.001f));
    REQUIRE(door.GetPositionY() == Catch::Approx(-566.205f).margin(0.001f));
    REQUIRE(door.GetPositionZ() == Catch::Approx(3.8396058f).margin(0.001f));
    REQUIRE(door.GetOrientation() == Catch::Approx(1.2283689f).margin(0.0001f));
}

TEST_CASE("A house's exterior root has an Entity GUID made from its plot", "[Housing][Exterior]")
{
    ObjectGuid const root = HousingMgr::MakeExteriorRootGuid(2736, 13);
    REQUIRE(root.GetHigh() == HighGuid::Entity);
    REQUIRE(root.GetMapId() == 2736);
    REQUIRE(root.GetEntry() == 0);
    REQUIRE(root.GetCounter() == HOUSING_EXTERIOR_ROOT_GUID_COUNTER_BASE + 13);

    // Every plot of a map has its own, and none of them can meet a counter the map hands out from 1.
    REQUIRE(HousingMgr::MakeExteriorRootGuid(2736, 12) != root);
    REQUIRE(HousingMgr::MakeExteriorRootGuid(2736, 0).GetCounter() > UI64LIT(0xFFFFFFFF));
}

TEST_CASE("A dragged house's placement is checked against its room and turns the root", "[Housing][Exterior]")
{
    // hled1 645894: the client put the root at (-7.4893, 1.7693, 0.02055) facing 1.5707932, and the root's update
    // at 645924 carried z 0.70710564, w 0.7071079.
    Position const dragged(-7.4893188f, 1.7693481f, 0.0205500f, 1.5707932f);
    REQUIRE(HousingMgr::IsRootPlacementInRoom(dragged));

    // The same turn as a quaternion; its sign is the server's choice, and both signs turn the root alike.
    QuaternionData const rotation = WithPositiveW(QuaternionData::fromEulerAnglesZYX(dragged.GetOrientation(), 0.0f, 0.0f));
    REQUIRE(rotation.z == Catch::Approx(0.70710564f).margin(0.00001f));
    REQUIRE(rotation.w == Catch::Approx(0.7071079f).margin(0.00001f));

    // The room's geobox is 35 yards each way along x, 30 along y, and from 1 below the anchor to 125 above it.
    REQUIRE(HousingMgr::IsRootPlacementInRoom(Position(35.0f, -30.0f, 125.0f, 0.0f)));
    REQUIRE_FALSE(HousingMgr::IsRootPlacementInRoom(Position(35.5f, 0.0f, 0.0f, 0.0f)));
    REQUIRE_FALSE(HousingMgr::IsRootPlacementInRoom(Position(0.0f, -30.5f, 0.0f, 0.0f)));
    REQUIRE_FALSE(HousingMgr::IsRootPlacementInRoom(Position(0.0f, 0.0f, -1.5f, 0.0f)));
    REQUIRE_FALSE(HousingMgr::IsRootPlacementInRoom(Position(0.0f, 0.0f, 125.5f, 0.0f)));
    REQUIRE_FALSE(HousingMgr::IsRootPlacementInRoom(Position(std::nanf(""), 0.0f, 0.0f, 0.0f)));
    REQUIRE_FALSE(HousingMgr::IsRootPlacementInRoom(Position(0.0f, 0.0f, std::numeric_limits<float>::infinity(), 0.0f)));
}

TEST_CASE("Decor has to stand inside its room's geobox where the room stands on the map", "[Housing][Exterior][Decor]")
{
    // The box retail sends for a plot's room, which the client checks placements against.
    Position const min(HOUSING_ROOM_FALLBACK_GEOBOX_MIN_X, HOUSING_ROOM_FALLBACK_GEOBOX_MIN_Y, HOUSING_ROOM_FALLBACK_GEOBOX_MIN_Z);
    Position const max(HOUSING_ROOM_FALLBACK_GEOBOX_MAX_X, HOUSING_ROOM_FALLBACK_GEOBOX_MAX_Y, HOUSING_ROOM_FALLBACK_GEOBOX_MAX_Z);

    SECTION("Yard decor retail accepted on plot 13 of Razorwind Shores is inside the plot's room")
    {
        GameObjectsEntry const row = MakeRazorwindPlot13Row();
        Position room;
        QuaternionData roomRot;
        REQUIRE(HousingMgr::GetRoomAnchor(&row, 2736, room, roomRot));

        // hled1 791438, 805424 and 810989: three places in map coordinates, each answered with success.
        REQUIRE(HousingMgr::IsInsideRoomGeobox(room, min, max, Position(908.2863f, -567.74677f, 1.4373517f), HOUSING_DECOR_BOUNDS_MARGIN));
        REQUIRE(HousingMgr::IsInsideRoomGeobox(room, min, max, Position(878.77026f, -595.11035f, 7.2617664f), HOUSING_DECOR_BOUNDS_MARGIN));
        REQUIRE(HousingMgr::IsInsideRoomGeobox(room, min, max, Position(887.4449f, -593.4711f, 1.4373479f), HOUSING_DECOR_BOUNDS_MARGIN));

        // Forty yards along the road, high in the air, or back at the map's origin is off the plot.
        REQUIRE_FALSE(HousingMgr::IsInsideRoomGeobox(room, min, max, Position(926.4f, -577.7f, 1.4f), HOUSING_DECOR_BOUNDS_MARGIN));
        REQUIRE_FALSE(HousingMgr::IsInsideRoomGeobox(room, min, max, Position(886.4f, -577.7f, 130.0f), HOUSING_DECOR_BOUNDS_MARGIN));
        REQUIRE_FALSE(HousingMgr::IsInsideRoomGeobox(room, min, max, Position(0.0f, 0.0f, 0.0f), HOUSING_DECOR_BOUNDS_MARGIN));
    }

    SECTION("A plot far from the map's origin takes decor too")
    {
        // GameObjects.db2 row 581971: "Plot - Plot 0" of Founder's Point, more than 1024 yards from the origin.
        GameObjectsEntry row{};
        row.ID = 581971;
        row.OwnerID = 2735;
        row.Pos.X = 2987.019f;
        row.Pos.Y = -224.60591f;
        row.Pos.Z = 113.10083f;
        row.Rot = { 0.0f, 0.0f, 0.90258533f, 0.43051097f };
        Position room;
        QuaternionData roomRot;
        REQUIRE(HousingMgr::GetRoomAnchor(&row, 2735, room, roomRot));

        REQUIRE(HousingMgr::IsInsideRoomGeobox(room, min, max, Position(2995.0f, -220.0f, 114.0f), HOUSING_DECOR_BOUNDS_MARGIN));
        REQUIRE_FALSE(HousingMgr::IsInsideRoomGeobox(room, min, max, Position(3060.0f, -224.6f, 113.1f), HOUSING_DECOR_BOUNDS_MARGIN));
    }

    SECTION("Inside, the room turns with its orientation")
    {
        // hbcd3 1443057: retail accepted a piece in the house's first room, "Square Room (Small)" (HouseRoom 1), which
        // stood at (-985, -1000, 0.1) facing 0 (hbcd3 1403035). Its box is RoomWmoData row 5 from the client's data.
        Position const smallRoom(-985.0f, -1000.0f, 0.1f, 0.0f);
        Position const smallRoomMin(-11.505f, -11.5052f, -0.00030000001f);
        Position const smallRoomMax(11.5052f, 11.5051f, 11.0f);
        REQUIRE(HousingMgr::IsInsideRoomGeobox(smallRoom, smallRoomMin, smallRoomMax, Position(-979.10394f, -993.3236f, 0.03240151f), HOUSING_DECOR_BOUNDS_MARGIN));
        // The same piece fifteen yards farther along x is past that small room's wall.
        REQUIRE_FALSE(HousingMgr::IsInsideRoomGeobox(smallRoom, smallRoomMin, smallRoomMax, Position(-964.10394f, -993.3236f, 0.03240151f), HOUSING_DECOR_BOUNDS_MARGIN));

        // The rest uses the plot room's larger box on a room standing at the interior's origin.
        Position const hall(-1000.0f, -1000.0f, 0.1f, 0.0f);

        // Thirty-four yards along y fits the box's 35 along x only once the room is turned a quarter turn.
        Position const point(-1000.0f, -966.0f, 0.1f);
        REQUIRE_FALSE(HousingMgr::IsInsideRoomGeobox(hall, min, max, point, HOUSING_DECOR_BOUNDS_MARGIN));
        Position const turned(-1000.0f, -1000.0f, 0.1f, float(M_PI / 2.0));
        REQUIRE(HousingMgr::IsInsideRoomGeobox(turned, min, max, point, HOUSING_DECOR_BOUNDS_MARGIN));

        // A room on a floor higher up does not take a piece on the ground floor.
        Position const upstairs(-1000.0f, -1000.0f, 0.1f + 2.0f * HOUSE_INTERIOR_FLOOR_HEIGHT, 0.0f);
        REQUIRE_FALSE(HousingMgr::IsInsideRoomGeobox(upstairs, min, max, Position(-990.0f, -1000.0f, 0.1f), HOUSING_DECOR_BOUNDS_MARGIN));
    }

    SECTION("A position that is not a number is nowhere")
    {
        Position const hall(-1000.0f, -1000.0f, 0.1f, 0.0f);
        REQUIRE_FALSE(HousingMgr::IsInsideRoomGeobox(hall, min, max, Position(std::nanf(""), -1000.0f, 0.1f), HOUSING_DECOR_BOUNDS_MARGIN));
    }
}

TEST_CASE("The decor entity the house's exit door rides is kept on the map's grids", "[Housing][Interior]")
{
    // Without a grid container of its own the map's visibility never met it, so it was sent out of range to the
    // character who saw it made and never to anyone who came in later, and taking it off the map would have hit the
    // grid assertion in RemoveFromGrid.
    constexpr bool inGridContainer = GridTypeMapContainer::TypeExists<HousingDecorEntity>;
    REQUIRE(inGridContainer);

    constexpr uint32 decorMask = Trinity::GridMapTypeMaskForType<HousingDecorEntity>::value;
    REQUIRE(decorMask == GRID_MAP_TYPE_MASK_HOUSING_DECOR_ENTITY);
    REQUIRE((GRID_MAP_TYPE_MASK_ALL & decorMask) == decorMask);

    // Every grid object type has its own bit, and the full mask is exactly all of them.
    constexpr uint32 masks[] = { GRID_MAP_TYPE_MASK_CORPSE, GRID_MAP_TYPE_MASK_CREATURE, GRID_MAP_TYPE_MASK_DYNAMICOBJECT,
        GRID_MAP_TYPE_MASK_GAMEOBJECT, GRID_MAP_TYPE_MASK_PLAYER, GRID_MAP_TYPE_MASK_AREATRIGGER, GRID_MAP_TYPE_MASK_SCENEOBJECT,
        GRID_MAP_TYPE_MASK_CONVERSATION, GRID_MAP_TYPE_MASK_MESHOBJECT, GRID_MAP_TYPE_MASK_HOUSING_ROOM_ENTITY,
        GRID_MAP_TYPE_MASK_HOUSING_DECOR_ENTITY };
    uint32 all = 0;
    for (uint32 mask : masks)
    {
        REQUIRE((all & mask) == 0);
        all |= mask;
    }
    REQUIRE(all == uint32(GRID_MAP_TYPE_MASK_ALL));
}
