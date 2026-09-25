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

#include "ScriptMgr.h"
#include "AreaTrigger.h"
#include "AreaTriggerAI.h"
#include "Housing.h"
#include "HousingDefines.h"
#include "HousingMap.h"
#include "HousingMgr.h"
#include "Log.h"
#include "WorldSession.h"
#include "Neighborhood.h"
#include "NeighborhoodMgr.h"
#include "Player.h"

// Stepping onto or off a plot has no packet of its own, and the plot's area trigger carries no housing fragment.
// The client learns which plot she stands on from the CurrentHouse field of her PlayerHouseInfoComponentData: this
// trigger writes the plot's house GUID there when she enters and clears it when she leaves.
struct at_housing_plot : AreaTriggerAI
{
    using AreaTriggerAI::AreaTriggerAI;

    void OnUnitEnter(Unit* unit) override
    {
        Player* player = unit->ToPlayer();
        if (!player)
            return;

        HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap());
        if (!housingMap)
            return;

        // Resolve which plot this AT represents from the HousingMap's AT registry.
        int8 plotIdx = housingMap->GetPlotIndexForAreaTrigger(at->GetGUID());
        if (plotIdx < 0)
        {
            TC_LOG_DEBUG("housing", "at_housing_plot: AT {} not registered as a plot AT — ignoring enter",
                at->GetGUID().ToString());
            return;
        }

        Neighborhood const* nbh = housingMap->GetNeighborhood();
        Neighborhood::PlotInfo const* plotInfo = nbh ? nbh->GetPlotInfo(static_cast<uint8>(plotIdx)) : nullptr;

        ObjectGuid ownerGuid = plotInfo ? plotInfo->OwnerGuid : ObjectGuid::Empty;
        ObjectGuid houseGuid = plotInfo ? plotInfo->HouseGuid : ObjectGuid::Empty;

        // Any character of the house's Battle.net account is on its own plot; anyone else is a visitor, checked
        // against the house's settings. Entering the house uses the same check (Neighborhood::CheckHouseEntry).
        bool isOwnPlot = false;
        if (!houseGuid.IsEmpty())
        {
            Neighborhood::HouseEntry entry = nbh->CheckHouseEntry(player, static_cast<uint8>(plotIdx), false);
            if (!entry.Allowed)
            {
                TC_LOG_DEBUG("housing", "at_housing_plot: Player {} denied plot access (house {} flags 0x{:X})",
                    player->GetGUID().ToString(), houseGuid.ToString(), entry.SettingsFlags);
                return;
            }
            isOwnPlot = entry.IsOwner;
        }

        // De-dup: HousingMap::AddPlayerToMap may have already pushed the CurrentHouse
        // update during the initial entity flush for players who logged out on a plot.
        int8 currentPlot = housingMap->GetPlayerCurrentPlot(player->GetGUID());
        bool alreadyOnPlot = (currentPlot == plotIdx);

        // Write the plot's house GUID to her CurrentHouse field; the client reads it to know she is on a plot. This
        // also runs when her arrival on the map already counted this plot as her current one, and SetCurrentHouse
        // sends nothing when the value is unchanged.
        player->SetCurrentHouse(houseGuid);

        if (!alreadyOnPlot)
            housingMap->SetPlayerCurrentPlot(player->GetGUID(), static_cast<uint8>(plotIdx));

        // "[DNT] In Plot" is cast whenever she stands in the box without it, also when her arrival on the map already
        // counted her plot as her current one: that is set for her own plot wherever on the map she arrives.
        housingMap->ApplyPlotAuras(player, static_cast<uint8>(plotIdx), !alreadyOnPlot);

        // Entering her own plot changes no phase: no capture shows that. Retail changes phases on tutorial quest
        // steps instead (hbcd3 716276, after "(Quest) Let's go!").

        TC_LOG_DEBUG("housing", "at_housing_plot: Player {} entered plot {} AT {} (own={}, owner={}, dedup={})",
            player->GetGUID().ToString(), plotIdx, at->GetGUID().ToString(), isOwnPlot,
            ownerGuid.IsEmpty() ? "none" : ownerGuid.ToString(), alreadyOnPlot);
    }

    void OnUnitExit(Unit* unit, AreaTriggerExitReason reason) override
    {
        if (reason != AreaTriggerExitReason::NotInside)
            return;

        Player* player = unit->ToPlayer();
        if (!player)
            return;

        HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap());
        if (!housingMap)
            return;

        int8 plotIdx = housingMap->GetPlotIndexForAreaTrigger(at->GetGUID());
        Neighborhood const* nbh = housingMap->GetNeighborhood();
        Neighborhood::PlotInfo const* plotInfo = (nbh && plotIdx >= 0)
            ? nbh->GetPlotInfo(static_cast<uint8>(plotIdx)) : nullptr;
        bool isOwnPlot = plotInfo && player->GetSession() && plotInfo->IsOwnedByAccount(player->GetSession()->GetBattlenetAccountGUID());

        // She may already have stepped into the next plot, whose trigger saw her first: then that plot is her current
        // one and keeps its aura.
        if (plotIdx < 0 || housingMap->GetPlayerCurrentPlot(player->GetGUID()) != plotIdx)
        {
            TC_LOG_DEBUG("housing", "at_housing_plot: Player {} left plot AT {}, but her current plot is another one",
                player->GetGUID().ToString(), at->GetGUID().ToString());
            return;
        }

        HousingMap::RemovePlotAuras(player);

        housingMap->ClearPlayerCurrentPlot(player->GetGUID());

        // Clear her CurrentHouse field so the client knows she is no longer on a plot.
        player->SetCurrentHouse(ObjectGuid::Empty);

        TC_LOG_DEBUG("housing", "at_housing_plot: Player {} left plot AT {} (own={})",
            player->GetGUID().ToString(), at->GetGUID().ToString(), isOwnPlot);
    }
};

void AddSC_at_housing_plot()
{
    RegisterAreaTriggerAI(at_housing_plot);
}
