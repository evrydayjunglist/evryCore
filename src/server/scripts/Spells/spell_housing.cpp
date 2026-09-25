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
#include "GameObject.h"
#include "HouseInteriorMap.h"
#include "Housing.h"
#include "HousingDefines.h"
#include "HousingMap.h"
#include "HousingMgr.h"
#include "Log.h"
#include "MapManager.h"
#include "Neighborhood.h"
#include "NeighborhoodMgr.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellPackets.h"
#include "SpellScript.h"

enum HousingPurchaseQuests
{
    QUEST_MY_FIRST_HOME              = 91863
};

// 1253555 - [DNT] Skip First Housing Tutorial
// Cast on the buyer through 1253572 (House Purchase Cover Spell) when a house is bought. Its effect skips questline
// 6063, which holds only "My First Home" (91863), so letting it through would complete that quest at the purchase.
// Retail refused it with SPELL_FAILED_DONT_REPORT while the buyer had 91863 in her log (hbcd3 1299941), and she
// turned the quest in herself later (hbcd3 1318088). A purchase by a character without 91863 in her log was not
// captured; it is refused as well, so no purchase skips the tutorial quest.
class spell_housing_skip_first_housing_tutorial : public SpellScript
{
    SpellCastResult CheckCast() const
    {
        Unit* caster = GetCaster();
        TC_LOG_DEBUG("housing", "spell_housing_skip_first_housing_tutorial: refused spell {} for {} (quest {} {})",
            GetSpellInfo()->Id, caster ? caster->GetGUID().ToString() : std::string("<no caster>"), QUEST_MY_FIRST_HOME,
            caster && caster->IsPlayer() && caster->ToPlayer()->FindQuestSlot(QUEST_MY_FIRST_HOME) < MAX_QUEST_LOG_SIZE
                ? "is in her log" : "is not in her log");
        return SPELL_FAILED_DONT_REPORT;
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_housing_skip_first_housing_tutorial::CheckCast);
    }
};

// 1234192 - the front door's goober spell (server-side; retail's client has no record of it)
// Retail: the client casts Opening (1271364) on the door, the door opens, and the character casts 1234192 on herself;
// then TRANSFER_PENDING and NEW_WORLD take her to the house interior map 2783 at (-1000, -1000, 0.1)
// (hbcd3 1342506-1344674). Any character of the house's Battle.net account enters as its owner; anyone else is a
// visitor, let in by the house's settings (Neighborhood::CheckHouseEntry). Owner and visitor land in the house's one
// interior instance, which MapManager picks from the house named here.
class spell_housing_enter_house : public SpellScript
{
    SpellCastResult CheckCast()
    {
        // A door cast from the door itself has no unit caster; only a character walks in.
        Unit* caster = GetCaster();
        Player* player = caster ? caster->ToPlayer() : nullptr;
        if (!player)
            return SPELL_FAILED_DONT_REPORT;

        HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap());
        Neighborhood* neighborhood = housingMap ? housingMap->GetNeighborhood() : nullptr;
        if (!neighborhood)
        {
            TC_LOG_DEBUG("housing", "spell_housing_enter_house: {} is not on a neighborhood map", player->GetGUID().ToString());
            return SPELL_FAILED_DONT_REPORT;
        }

        uint8 plotIndex = INVALID_PLOT_INDEX;
        GameObject* door = housingMap->FindHouseDoorInReach(player, GetSpellInfo()->Id, plotIndex);
        if (!door)
        {
            TC_LOG_DEBUG("housing", "spell_housing_enter_house: {} stands at no house door", player->GetGUID().ToString());
            return SPELL_FAILED_DONT_REPORT;
        }

        Neighborhood::HouseEntry entry = neighborhood->CheckHouseEntry(player, plotIndex, true);
        if (entry.HouseGuid.IsEmpty() || !entry.Allowed)
        {
            TC_LOG_DEBUG("housing", "spell_housing_enter_house: {} may not enter the house on plot {} (house {}, settings 0x{:X})",
                player->GetGUID().ToString(), plotIndex, entry.HouseGuid.ToString(), entry.SettingsFlags);
            return SPELL_FAILED_DONT_REPORT;
        }

        _houseGuid = entry.HouseGuid;
        return SPELL_CAST_OK;
    }

    void HandleEnter(SpellEffIndex /*effIndex*/)
    {
        Player* player = GetHitPlayer();
        if (!player || _houseGuid.IsEmpty())
            return;

        // MapManager picks the interior instance from the house named here, for owners and visitors alike.
        player->SetHouseVisitTarget(_houseGuid);

        // Marked before the teleport, so leaving the plot's area trigger on the way does not clear the house's
        // editor state.
        Housing* housing = player->GetHousingByGuid(_houseGuid);
        if (housing)
            housing->SetInInterior(true);

        if (!player->TeleportTo(HOUSE_INTERIOR_MAP_ID, HOUSE_INTERIOR_ARRIVAL_X, HOUSE_INTERIOR_ARRIVAL_Y, HOUSE_INTERIOR_ARRIVAL_Z,
            HOUSE_INTERIOR_ARRIVAL_O, TELE_TO_SPELL))
        {
            // She is still on the plot, so the house is not entered after all.
            if (housing)
                housing->SetInInterior(false);
            player->ClearHouseVisitTarget();

            TC_LOG_ERROR("housing", "spell_housing_enter_house: the teleport of {} into house {} on map {} was refused",
                player->GetGUID().ToString(), _houseGuid.ToString(), HOUSE_INTERIOR_MAP_ID);
            return;
        }

        TC_LOG_DEBUG("housing", "spell_housing_enter_house: {} enters house {}", player->GetGUID().ToString(), _houseGuid.ToString());
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_housing_enter_house::CheckCast);
        OnEffectHitTarget += SpellEffectFn(spell_housing_enter_house::HandleEnter, EFFECT_0, SPELL_EFFECT_DUMMY);
    }

    ObjectGuid _houseGuid;
};

// 1234193 - Exit House
// Retail: the client casts Opening (1271364) on the door inside the house, the door opens, and the character casts
// Exit House on herself, instantly; then TRANSFER_PENDING and NEW_WORLD put her at her plot's arrival point,
// 902.6711, -542.7863, 1.9622 facing 4.5902157 for plot 13 (hbcd3 1455919-1456426). A visitor comes out on the plot
// of the house she visited, in that house's neighborhood. Its only effect, 343, does nothing in the core, and what it
// does for other spells is not known, so this script does the move for this spell alone.
class spell_housing_exit_house : public SpellScript
{
    // SPELL_START for Exit House carries cast time 0 (hbcd3 1456142), though its record gives it one.
    int32 CalcCastTime(int32 /*castTime*/) override
    {
        return 0;
    }

    void HandleExit(SpellEffIndex /*effIndex*/)
    {
        Player* player = GetHitPlayer();
        if (!player)
            return;

        HouseInteriorMap* interior = dynamic_cast<HouseInteriorMap*>(player->GetMap());
        if (!interior)
        {
            TC_LOG_DEBUG("housing", "spell_housing_exit_house: {} is not inside a house", player->GetGUID().ToString());
            return;
        }

        ObjectGuid houseGuid = interior->GetHouseGuid();
        Neighborhood const* neighborhood = nullptr;
        Neighborhood::PlotInfo const* plot = nullptr;
        for (Neighborhood const* candidate : sNeighborhoodMgr.GetAllNeighborhoods())
        {
            plot = candidate->GetPlotInfoByHouse(houseGuid);
            if (plot)
            {
                neighborhood = candidate;
                break;
            }
        }

        WorldLocation arrival;
        if (!neighborhood || !sHousingMgr.GetPlotArrival(neighborhood->GetNeighborhoodMapID(), plot->PlotIndex, arrival))
        {
            TC_LOG_ERROR("housing", "spell_housing_exit_house: house {} of the interior {} is on no known plot",
                houseGuid.ToString(), player->GetGUID().ToString());
            return;
        }

        // Into that neighborhood's instance of the district, so she comes out beside the house and not on the same plot
        // of whichever neighborhood she would otherwise be sent to. Neighborhood maps are loaded at startup and never
        // unloaded, and one made since is loaded once someone has entered it; when it is not loaded, the map change
        // picks the neighborhood as it does for any arrival, starting with the house she is leaving.
        Optional<uint32> instanceId;
        uint32 const neighborhoodInstanceId = uint32(neighborhood->GetGuid().GetCounter());
        if (sMapMgr->FindMap(arrival.GetMapId(), neighborhoodInstanceId))
            instanceId = neighborhoodInstanceId;

        if (!player->TeleportTo(arrival, TELE_TO_SPELL, instanceId))
        {
            TC_LOG_ERROR("housing", "spell_housing_exit_house: the teleport of {} out of house {} to plot {} on map {} was refused",
                player->GetGUID().ToString(), houseGuid.ToString(), plot->PlotIndex, arrival.GetMapId());
            return;
        }

        TC_LOG_DEBUG("housing", "spell_housing_exit_house: {} leaves house {} for plot {} on map {}",
            player->GetGUID().ToString(), houseGuid.ToString(), plot->PlotIndex, arrival.GetMapId());
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_housing_exit_house::HandleExit, EFFECT_0, SPELL_EFFECT_343);
    }
};

// 1233637 - Teleport Home
// Retail casts it on the character for ten seconds, with the plot's arrival point as the destination and the
// neighborhood as the cast's housing target (hbcd3 2044258, HousingGUID). Its teleport effect, effect 1, has no
// destination of its own. The core teleport would keep her in the instance she is in when she stands in another
// neighborhood of the same district, putting her on that neighborhood's copy of the plot, and from another map the map
// change would pick a neighborhood from her own houses and memberships. So this script teleports her into the instance of
// the neighborhood the cast names, as Exit House does, after checking again that she may still go there: the cast lasts
// ten seconds and access can change meanwhile.
class spell_housing_teleport_home : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellEffect({ { spellInfo->Id, EFFECT_1 } })
            && spellInfo->GetEffect(EFFECT_1).IsEffect(SPELL_EFFECT_TELEPORT_UNITS);
    }

    static Optional<uint8> FindPlotOfArrival(Neighborhood const* neighborhood, WorldLocation const& arrival)
    {
        for (uint8 plotIndex = 0; plotIndex < MAX_NEIGHBORHOOD_PLOTS; ++plotIndex)
        {
            WorldLocation plotArrival;
            if (sHousingMgr.GetPlotArrival(neighborhood->GetNeighborhoodMapID(), plotIndex, plotArrival)
                && plotArrival.GetMapId() == arrival.GetMapId() && plotArrival.GetExactDistSq(arrival) < 0.01f)
                return plotIndex;
        }
        return {};
    }

    void HandleTeleport(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);

        Player* player = GetHitPlayer();
        WorldLocation const* arrival = GetExplTargetDest();
        if (!player || !arrival)
            return;

        Neighborhood const* neighborhood = sNeighborhoodMgr.GetNeighborhood(GetSpell()->m_targets.GetHousingTargetGUID());
        if (!neighborhood)
        {
            TC_LOG_ERROR("housing", "spell_housing_teleport_home: the cast of {} names no known neighborhood ({})",
                player->GetGUID().ToString(), GetSpell()->m_targets.GetHousingTargetGUID().ToString());
            return;
        }

        Optional<uint8> plotIndex = FindPlotOfArrival(neighborhood, *arrival);
        if (!plotIndex)
        {
            TC_LOG_ERROR("housing", "spell_housing_teleport_home: the destination of {} is no plot of neighborhood {}",
                player->GetGUID().ToString(), neighborhood->GetGuid().ToString());
            return;
        }

        // The same checks as HandleHousingSvcsTeleportToPlot made when the cast began.
        bool const livesHere = neighborhood->IsMember(player->GetGUID()) || player->GetHousingForNeighborhood(neighborhood->GetGuid());
        if (!livesHere)
        {
            if (!neighborhood->IsPublic())
            {
                TC_LOG_DEBUG("housing", "spell_housing_teleport_home: {} may no longer go to private neighborhood {}",
                    player->GetGUID().ToString(), neighborhood->GetGuid().ToString());
                return;
            }

            Neighborhood::HouseEntry entry = neighborhood->CheckHouseEntry(player, *plotIndex, false);
            if (!entry.HouseGuid.IsEmpty() && !entry.Allowed)
            {
                TC_LOG_DEBUG("housing", "spell_housing_teleport_home: {} may no longer go to plot {} of neighborhood {} (settings 0x{:X})",
                    player->GetGUID().ToString(), *plotIndex, neighborhood->GetGuid().ToString(), entry.SettingsFlags);
                return;
            }
        }

        // The neighborhood's own instance. HandleHousingSvcsTeleportToPlot loaded it before the cast, and neighborhood maps
        // are not unloaded. Should it still be missing, the map change picks the neighborhood as it does for any arrival.
        Optional<uint32> instanceId;
        uint32 const neighborhoodInstanceId = uint32(neighborhood->GetGuid().GetCounter());
        if (sMapMgr->FindMap(arrival->GetMapId(), neighborhoodInstanceId))
            instanceId = neighborhoodInstanceId;
        else
            TC_LOG_ERROR("housing", "spell_housing_teleport_home: neighborhood {} has no map instance loaded, so {} arrives where the map "
                "change puts her", neighborhood->GetGuid().ToString(), player->GetGUID().ToString());

        // What the core teleport effect does besides the move: the effect's loading screen, and the options it would
        // pass for a character teleporting herself.
        if (uint32 customLoadingScreenId = GetEffectInfo(effIndex).MiscValue)
            if (arrival->GetMapId() != player->GetMapId() || !player->IsInDist2d(arrival, TELEPORT_MIN_LOAD_SCREEN_DISTANCE))
                player->SendDirectMessage(WorldPackets::Spells::CustomLoadScreen(GetSpellInfo()->Id, customLoadingScreenId).Write());

        TeleportToOptions options = TELE_TO_SPELL;
        if (arrival->GetMapId() == player->GetMapId())
        {
            options |= TELE_TO_NOT_LEAVE_COMBAT;
            if (player->GetTransGUID().IsEmpty())
                options |= TELE_TO_NOT_LEAVE_TRANSPORT;
        }

        if (!player->TeleportTo(*arrival, options, instanceId, GetSpellInfo()->Id))
        {
            TC_LOG_ERROR("housing", "spell_housing_teleport_home: the teleport of {} to plot {} of neighborhood {} was refused",
                player->GetGUID().ToString(), *plotIndex, neighborhood->GetGuid().ToString());
            return;
        }

        TC_LOG_DEBUG("housing", "spell_housing_teleport_home: {} goes to plot {} of neighborhood {} (instance {})",
            player->GetGUID().ToString(), *plotIndex, neighborhood->GetGuid().ToString(),
            instanceId ? std::to_string(*instanceId) : std::string("picked by the map change"));
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_housing_teleport_home::HandleTeleport, EFFECT_1, SPELL_EFFECT_TELEPORT_UNITS);
    }
};

void AddSC_housing_spell_scripts()
{
    RegisterSpellScript(spell_housing_enter_house);
    RegisterSpellScript(spell_housing_exit_house);
    RegisterSpellScript(spell_housing_skip_first_housing_tutorial);
    RegisterSpellScript(spell_housing_teleport_home);
}
