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

// 12.0.5 plot-entry mechanism:
//   - No more SMSG_NEIGHBORHOOD_PLAYER_ENTER_PLOT / LEAVE_PLOT opcodes (removed in
//     TC commit 4c14988 / WoW build 12.0.5.67114).
//   - No more FHousingPlotAreaTrigger_C entity fragment on the plot AT.
//   - Plot ownership / "am I on a plot" is communicated via the
//     PlayerHouseInfoComponentData.CurrentHouse UpdateField on the Player entity.
//     Server writes the plot's HouseGuid to CurrentHouse on enter and clears it
//     on exit; the client observes the UPDATE_OBJECT change to track occupancy.
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

        // 12.0.5 plot-entry: write the plot's HouseGuid to PlayerHouseInfoComponent.CurrentHouse.
        // The UPDATE_OBJECT carrying this change replaces the removed
        // SMSG_NEIGHBORHOOD_PLAYER_ENTER_PLOT opcode; the client reads CurrentHouse to
        // populate its NeighborhoodSystem TLS (+280) "am I on a plot" state.
        // Always invoked — SetCurrentHouse short-circuits when the value is unchanged, so
        // logged-in-on-plot players (alreadyOnPlot=true via HousingMap::SetPlayerCurrentPlot
        // at AddPlayerToMap) still get the field-change callback wired correctly.
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

        // 12.0.5 plot-leave: clear PlayerHouseInfoComponent.CurrentHouse so the client's
        // NeighborhoodSystem TLS drops its "on plot" flag.
        player->SetCurrentHouse(ObjectGuid::Empty);

        TC_LOG_DEBUG("housing", "at_housing_plot: Player {} left plot AT {} (own={})",
            player->GetGUID().ToString(), at->GetGUID().ToString(), isOwnPlot);
    }
};

void AddSC_at_housing_plot()
{
    RegisterAreaTriggerAI(at_housing_plot);
}
