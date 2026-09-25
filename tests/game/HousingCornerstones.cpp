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
#include "HousingDefines.h"
#include "HousingMgr.h"
#include "QuaternionData.h"

namespace
{
    // NeighborhoodPlot row 372 in the 12.1 client: plot 13 of Razorwind Shores (NeighborhoodMap 2, map 2736).
    NeighborhoodPlotData MakeRazorwindPlot13()
    {
        NeighborhoodPlotData plot;
        plot.ID = 372;
        plot.NeighborhoodMapID = 2;
        plot.PlotIndex = 13;
        plot.CornerstoneGameObjectID = 475091;
        plot.CornerstonePosition[0] = 902.30554f;
        plot.CornerstonePosition[1] = -545.76392f;
        plot.CornerstonePosition[2] = 1.4305556f;
        plot.CornerstoneRotation[2] = 4.5902157f;
        return plot;
    }

    // q and -q are the same turn, and the create block packs the rotation with its w made positive
    // (GameObject::UpdatePackedRotation), so compare in that form.
    QuaternionData WithPositiveW(QuaternionData q)
    {
        if (q.w < 0.0f)
            q = QuaternionData(-q.x, -q.y, -q.z, -q.w);
        return q;
    }
}

TEST_CASE("Every plot's cornerstone is the one shared entry retail uses", "[Housing][Cornerstone]")
{
    REQUIRE(GAMEOBJECT_HOUSING_CORNERSTONE == 457142u);
}

TEST_CASE("A cornerstone's CreatedBy is the client actor retail sends for its plot", "[Housing][Cornerstone]")
{
    // hbcd3 456682-456741: the cornerstone of plot 13 on map 2736 has CreatedBy
    // 0x5000042AC0000000 / 0x0000000000073FD3, ClientActor, counter 475091.
    ObjectGuid const createdBy = HousingMgr::MakeCornerstoneCreator(MakeRazorwindPlot13(), 2736);
    REQUIRE(createdBy.GetHigh() == HighGuid::ClientActor);
    REQUIRE(createdBy.GetRawValue(1) == UI64LIT(0x5000042AC0000000));
    REQUIRE(createdBy.GetRawValue(0) == UI64LIT(0x0000000000073FD3));

    // The high half holds owner type 1 and owner id 2736, the world map, not 68272.
    REQUIRE(((createdBy.GetRawValue(1) >> 42) & 0x1FFF) == 1);
    REQUIRE(((createdBy.GetRawValue(1) >> 26) & 0xFFFF) == 2736);
}

TEST_CASE("A Razorwind Shores cornerstone stands at its DB2 position, facing its rotation plus a half turn", "[Housing][Cornerstone]")
{
    // hbcd3 456682-456741: plot 13's cornerstone at 902.30554, -545.7639, 1.4306945, orientation 1.448622,
    // rotation 0, 0, 0.6626196, 0.7489561.
    Position position;
    QuaternionData rotation;
    HousingMgr::GetCornerstonePlacement(MakeRazorwindPlot13(), 2736, nullptr, position, rotation);
    rotation = WithPositiveW(rotation);
    REQUIRE(position.GetPositionX() == Catch::Approx(902.30554f));
    REQUIRE(position.GetPositionY() == Catch::Approx(-545.7639f));
    REQUIRE(position.GetPositionZ() == Catch::Approx(1.4306f).margin(0.001f));
    REQUIRE(position.GetOrientation() == Catch::Approx(1.448622f).margin(0.00001f));
    REQUIRE(rotation.x == Catch::Approx(0.0f).margin(0.000001f));
    REQUIRE(rotation.y == Catch::Approx(0.0f).margin(0.000001f));
    REQUIRE(rotation.z == Catch::Approx(0.6626196f).margin(0.00001f));
    REQUIRE(rotation.w == Catch::Approx(0.7489561f).margin(0.00001f));
}

TEST_CASE("A Razorwind Shores cornerstone whose DB2 position is stale stands where retail puts it", "[Housing][Cornerstone]")
{
    // NeighborhoodPlot row 371, plot 12: the DB2 puts the cornerstone at 519.73438, 693.20313, 155.52083, 61 yards
    // from where retail creates it (hbcd3 457672: 544.6215, 637.1024, 155.30049, orientation 0.567232, rotation
    // 0, 0, 0.27982903, 0.96004987).
    NeighborhoodPlotData plot;
    plot.ID = 371;
    plot.NeighborhoodMapID = 2;
    plot.PlotIndex = 12;
    plot.CornerstoneGameObjectID = 582082;
    plot.CornerstonePosition[0] = 519.73438f;
    plot.CornerstonePosition[1] = 693.20313f;
    plot.CornerstonePosition[2] = 155.52083f;
    plot.CornerstoneRotation[2] = 2.6005416f;

    Position position;
    QuaternionData rotation;
    HousingMgr::GetCornerstonePlacement(plot, 2736, nullptr, position, rotation);
    rotation = WithPositiveW(rotation);
    REQUIRE(position.GetPositionX() == Catch::Approx(544.6215f));
    REQUIRE(position.GetPositionY() == Catch::Approx(637.1024f));
    REQUIRE(position.GetPositionZ() == Catch::Approx(155.30049f));
    REQUIRE(position.GetOrientation() == Catch::Approx(0.567232f));
    REQUIRE(rotation.z == Catch::Approx(0.27982903f).margin(0.00001f));
    REQUIRE(rotation.w == Catch::Approx(0.96004987f).margin(0.00001f));

    // The same plot index on another map keeps its own DB2 values.
    HousingMgr::GetCornerstonePlacement(plot, 2735, nullptr, position, rotation);
    REQUIRE(position.GetPositionX() == Catch::Approx(519.73438f));
    REQUIRE(position.GetPositionY() == Catch::Approx(693.20313f));
}

TEST_CASE("A Founder's Point cornerstone stands at its GameObjects.db2 row", "[Housing][Cornerstone]")
{
    // NeighborhoodPlot row 423: plot 6 of Founder's Point (NeighborhoodMap 1, map 2735). GameObjects.db2 row 527260
    // 'Cornerstone - Plot 6' is on map 2735 at 2666.5061, -443.28299, 121.72236, rotation 0, 0, 0.98836154, -0.15212339.
    NeighborhoodPlotData plot;
    plot.ID = 423;
    plot.NeighborhoodMapID = 1;
    plot.PlotIndex = 6;
    plot.CornerstoneGameObjectID = 527260;
    plot.CornerstonePosition[0] = 2662.9531f;
    plot.CornerstonePosition[1] = -427.98785f;
    plot.CornerstonePosition[2] = 121.83334f;
    plot.CornerstoneRotation[2] = 0.13089991f;

    GameObjectsEntry row{};
    row.ID = 527260;
    row.OwnerID = 2735;
    row.Pos.X = 2666.5061f;
    row.Pos.Y = -443.28299f;
    row.Pos.Z = 121.72236f;
    row.Rot = { 0.0f, 0.0f, 0.98836154f, -0.15212339f };

    Position position;
    QuaternionData rotation;
    HousingMgr::GetCornerstonePlacement(plot, 2735, &row, position, rotation);
    REQUIRE(position.GetPositionX() == Catch::Approx(2666.5061f));
    REQUIRE(position.GetPositionY() == Catch::Approx(-443.28299f));
    REQUIRE(position.GetPositionZ() == Catch::Approx(121.72236f));
    REQUIRE(rotation.z == Catch::Approx(0.98836154f));
    REQUIRE(rotation.w == Catch::Approx(-0.15212339f));
    // The row's rotation is a turn of about 3.447 radians about the vertical axis.
    REQUIRE(position.GetOrientation() == Catch::Approx(3.4470f).margin(0.0005f));

    // A row on another map is not this plot's cornerstone.
    row.OwnerID = 2736;
    HousingMgr::GetCornerstonePlacement(plot, 2735, &row, position, rotation);
    REQUIRE(position.GetPositionX() == Catch::Approx(2662.9531f));
    REQUIRE(position.GetPositionY() == Catch::Approx(-427.98785f));
}
