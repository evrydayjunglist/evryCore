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
#include "HousingMgr.h"
#include "SpellDefines.h"
#include "SpellPackets.h"
#include "SystemPackets.h"
#include "WorldSession.h"
#include <algorithm>
#include <string>
#include <vector>

TEST_CASE("A character arrives on a plot at its teleport point, facing the cornerstone's rotation", "[Housing][Travel]")
{
    // NeighborhoodPlot row 372: plot 13 of Razorwind Shores (NeighborhoodMap 2, map 2736) in the 12.1 client.
    NeighborhoodPlotData plot;
    plot.PlotIndex = 13;
    plot.NeighborhoodMapID = 2;
    plot.TeleportPosition[0] = 902.67108f;
    plot.TeleportPosition[1] = -542.78632f;
    plot.TeleportPosition[2] = 1.9622f;
    plot.CornerstoneRotation[2] = 4.5902157f;
    plot.TeleportFacing = 1.570796f;

    // Teleport Home (hbcd3 2044258) and Exit House (hbcd3 1456426) both land her at 902.6711, -542.7863, 1.9622,
    // facing 4.5902157, on map 2736.
    WorldLocation const arrival = HousingMgr::MakePlotArrival(plot, 2736);
    REQUIRE(arrival.GetMapId() == 2736);
    REQUIRE(arrival.GetPositionX() == Catch::Approx(902.6711f).margin(0.0001f));
    REQUIRE(arrival.GetPositionY() == Catch::Approx(-542.7863f).margin(0.0001f));
    REQUIRE(arrival.GetPositionZ() == Catch::Approx(1.9622f).margin(0.0001f));
    REQUIRE(arrival.GetOrientation() == Catch::Approx(4.5902157f).margin(0.000001f));
}

TEST_CASE("A server-chosen destination carries the neighborhood, its facing and its map", "[Housing][Travel]")
{
    // hbcd3 2044258, SMSG_SPELL_START of Teleport Home: target flags 64 (destination), HousingGUID the neighborhood
    // 0xDC80000200000000 / 0xA584, not resident, destination 902.6711, -542.7863, 1.9622, orientation 4.5902157,
    // map 2736.
    ObjectGuid const neighborhood = ObjectGuid::Create<HighGuid::Housing>(/*subType*/ 4, /*neighborhoodMapId*/ 2, 0, 0xA584);
    REQUIRE(neighborhood.GetRawValue(1) == UI64LIT(0xDC80000200000000));
    REQUIRE(neighborhood.GetRawValue(0) == UI64LIT(0x000000000000A584));

    SpellCastTargets targets;
    targets.SetServerChosenDst(WorldLocation(2736, 902.6711f, -542.7863f, 1.9622f, 4.5902157f));
    targets.SetHousingTarget(neighborhood);
    REQUIRE(targets.IsServerChosenDst());

    WorldPackets::Spells::SpellTargetData data;
    targets.Write(data);
    REQUIRE(data.Flags == TARGET_FLAG_DEST_LOCATION);
    REQUIRE(data.Unit.IsEmpty());
    REQUIRE(data.HousingGUID == neighborhood);
    REQUIRE(!data.HousingIsResident);
    REQUIRE(data.DstLocation.has_value());
    REQUIRE(data.DstLocation->Transport.IsEmpty());
    REQUIRE(data.DstLocation->Location.Pos.GetPositionX() == Catch::Approx(902.6711f));
    REQUIRE(data.DstLocation->Location.Pos.GetPositionY() == Catch::Approx(-542.7863f));
    REQUIRE(data.DstLocation->Location.Pos.GetPositionZ() == Catch::Approx(1.9622f));
    REQUIRE(data.Orientation.has_value());
    REQUIRE(*data.Orientation == Catch::Approx(4.5902157f));
    REQUIRE(data.MapID.has_value());
    REQUIRE(*data.MapID == 2736);

    // Any other destination is written as before: no facing, no map, no housing object.
    SpellCastTargets aimed;
    aimed.SetDst(10.0f, 20.0f, 30.0f, 1.0f, 2736);
    WorldPackets::Spells::SpellTargetData aimedData;
    aimed.Write(aimedData);
    REQUIRE(!aimed.IsServerChosenDst());
    REQUIRE(aimedData.HousingGUID.IsEmpty());
    REQUIRE(!aimedData.Orientation);
    REQUIRE(!aimedData.MapID);

    // Removing the destination forgets that the server chose it.
    targets.RemoveDst();
    REQUIRE(!targets.IsServerChosenDst());
}

TEST_CASE("Housing mirror variables match retail at the character screen and in the world", "[Housing][Travel]")
{
    auto names = [](std::vector<WorldPackets::System::MirrorVarSingle> const& vars)
    {
        std::vector<std::string> result;
        for (WorldPackets::System::MirrorVarSingle const& var : vars)
            result.emplace_back(std::string(var.Name) + '=' + var.Value);
        std::ranges::sort(result);
        return result;
    };

    // The housing variables of the in-world SMSG_MIRROR_VARS, the same in all five in hbcd3 (167415, 245836, 356261,
    // 1344902, 1456481).
    std::vector<std::string> const retailInWorld =
    {
        "housingBasicDecor_MaxPreviewLimit=100",
        "housingCatalog_CartSizeLimit=20",
        "housingDecorReportScreenshotDistanceThreshold=150.000000",
        "housingDecorReportScreenshotFacingDotThreshold=0.500000",
        "housingExpertDecor_Scale_Indoor_Max=2.000000",
        "housingExpertDecor_Scale_Indoor_Min=0.200000",
        "housingExpertDecor_Scale_Outdoor_Max=2.000000",
        "housingExpertDecor_Scale_Outdoor_Min=0.200000",
        "housingExteriorLightsAllowed=1",
        "housingExteriorLightsRadiusMultiplier=0.500000",
        "housingExteriorTypeByNeighborhoodFactionRestriction=1",
        "housingMarketAddToCartTelemThrottle=15",
        "housingMarketCartFullRemoveEnabled=1",
        "housingMarketClearCartTelemThrottle=5",
        "housingMarketEnabled=1",
        "housingMarketRemoveFromCartTelemThrottle=20",
        "housingMarketShopEnabled=1",
        "housingMarketThrottleTimePeriodMs=10000",
        "housingMarketViewBundleTelemThrottle=10",
        "housingMarketViewInStoreTelemThrottle=5",
        "minNeighborhoodGroupMembers=3",
        "performHousingExpansionCheckClient=1",
    };

    std::vector<WorldPackets::System::MirrorVarSingle> inWorld;
    WorldSession::AppendHousingMirrorVars(inWorld, true);
    REQUIRE(names(inWorld) == retailInWorld);

    // The character screen list (hbcd3 1442) has the same values without minNeighborhoodGroupMembers.
    std::vector<WorldPackets::System::MirrorVarSingle> glue;
    WorldSession::AppendHousingMirrorVars(glue, false);
    std::vector<std::string> retailGlue = retailInWorld;
    std::erase(retailGlue, std::string("minNeighborhoodGroupMembers=3"));
    REQUIRE(names(glue) == retailGlue);
}
