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

#include "WorldSession.h"
#include "AuthenticationPackets.h"
#include "BattlenetRpcErrorCodes.h"
#include "CharacterTemplateDataStore.h"
#include "ClientConfigPackets.h"
#include "DisableMgr.h"
#include "GameTime.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "RealmList.h"
#include "SystemPackets.h"
#include "Timezone.h"
#include "Timerunning.h"
#include "Util.h"
#include "World.h"

void WorldSession::SendAuthResponse(uint32 code, bool queued, uint32 queuePos)
{
    WorldPackets::Auth::AuthResponse response;
    response.Result = code;

    if (code == ERROR_OK)
    {
        response.SuccessInfo.emplace();

        response.SuccessInfo->ActiveExpansionLevel = GetExpansion();
        response.SuccessInfo->AccountExpansionLevel = GetAccountExpansion();
        response.SuccessInfo->Time = int32(GameTime::GetGameTime());

        // Send current home realm. Also there is no need to send it later in realm queries.
        if (std::shared_ptr<Realm const> currentRealm = sRealmList->GetCurrentRealm())
        {
            response.SuccessInfo->VirtualRealmAddress = currentRealm->Id.GetAddress();
            response.SuccessInfo->VirtualRealms.emplace_back(currentRealm->Id.GetAddress(), true, false, currentRealm->Name, currentRealm->NormalizedName);
        }

        if (HasPermission(rbac::RBAC_PERM_USE_CHARACTER_TEMPLATES))
            for (auto&& templ : sCharacterTemplateDataStore->GetCharacterTemplates())
                response.SuccessInfo->Templates.push_back(&templ.second);

        response.SuccessInfo->AvailableClasses = &sObjectMgr->GetRaceClassRequirements();

        // TEMPORARY - prevent creating characters in uncompletable zone
        // This has the side effect of disabling Exile's Reach choice clientside without actually forcing character templates
        response.SuccessInfo->ForceCharacterTemplate = DisableMgr::IsDisabledFor(DISABLE_TYPE_MAP, 2175 /*Exile's Reach*/, nullptr);
    }

    if (queued)
    {
        response.WaitInfo.emplace();
        response.WaitInfo->WaitCount = queuePos;
    }

    SendPacket(response.Write());
}

void WorldSession::SendAuthWaitQueue(uint32 position)
{
    if (position)
    {
        WorldPackets::Auth::WaitQueueUpdate waitQueueUpdate;
        waitQueueUpdate.WaitInfo.WaitCount = position;
        waitQueueUpdate.WaitInfo.WaitTime = 0;
        waitQueueUpdate.WaitInfo.HasFCM = false;
        SendPacket(waitQueueUpdate.Write());
    }
    else
        SendPacket(WorldPackets::Auth::WaitQueueFinish().Write());
}

void WorldSession::SendClientCacheVersion(uint32 version)
{
    WorldPackets::ClientConfig::ClientCacheVersion cache;
    cache.CacheVersion = version;

    SendPacket(cache.Write());
}

void WorldSession::SendSetTimeZoneInformation()
{
    Minutes timezoneOffset = Trinity::Timezone::GetSystemZoneOffset(false);
    std::string realTimezone = Trinity::Timezone::GetSystemZoneName();
    std::string_view clientSupportedTZ = Trinity::Timezone::FindClosestClientSupportedTimezone(realTimezone, timezoneOffset);

    WorldPackets::System::SetTimeZoneInformation packet;
    packet.ServerTimeTZ = clientSupportedTZ;
    packet.GameTimeTZ = clientSupportedTZ;
    packet.ServerRegionalTimeTZ = clientSupportedTZ;
    SendPacket(packet.Write());
}

void WorldSession::SendFeatureSystemStatusGlueScreen()
{
    WorldPackets::System::FeatureSystemStatusGlueScreen features;
    Timerunning::State timerunning = sTimerunningMgr->GetState();
    features.TimerunningEnabled = timerunning.IsEnabled();
    features.ActiveTimerunningSeasonID = int32(timerunning.ActiveSeason);
    features.RemainingTimerunningSeasonSeconds = timerunning.RemainingSeconds;
    features.TimerunningConversionMaxSeasonID = -1;
    _timerunningStatusSent = true;
    _timerunningSeasonId = features.ActiveTimerunningSeasonID;
    _timerunningSeasonEnd = timerunning.EndTime;
    bool const battlePayEnabled = sWorld->getBoolConfig(CONFIG_BATTLE_PAY_ENABLED);
    features.BpayStoreAvailable = battlePayEnabled;
    features.BpayStoreDisabledByParentalControls = false;
    features.CharUndeleteEnabled = sWorld->getBoolConfig(CONFIG_FEATURE_SYSTEM_CHARACTER_UNDELETE_ENABLED);
    features.MaxCharactersOnThisRealm = sWorld->getIntConfig(CONFIG_CHARACTERS_PER_REALM);
    features.MinimumExpansionLevel = EXPANSION_CLASSIC;
    features.MaximumExpansionLevel = sWorld->getIntConfig(CONFIG_EXPANSION);

    features.EuropaTicketSystemStatus.emplace();
    features.EuropaTicketSystemStatus->ThrottleState.MaxTries = 10;
    features.EuropaTicketSystemStatus->ThrottleState.PerMilliseconds = 60000;
    features.EuropaTicketSystemStatus->ThrottleState.TryCount = 1;
    features.EuropaTicketSystemStatus->ThrottleState.LastResetTimeBeforeNow = 111111;
    features.EuropaTicketSystemStatus->TicketsEnabled = sWorld->getBoolConfig(CONFIG_SUPPORT_TICKETS_ENABLED);
    features.EuropaTicketSystemStatus->BugsEnabled = sWorld->getBoolConfig(CONFIG_SUPPORT_BUGS_ENABLED);
    features.EuropaTicketSystemStatus->ComplaintsEnabled = sWorld->getBoolConfig(CONFIG_SUPPORT_COMPLAINTS_ENABLED);
    features.EuropaTicketSystemStatus->SuggestionsEnabled = sWorld->getBoolConfig(CONFIG_SUPPORT_SUGGESTIONS_ENABLED);

    for (World::GameRule const& gameRule : sWorld->GetGameRules())
    {
        WorldPackets::System::GameRuleValuePair& rule = features.GameRules.emplace_back();
        rule.Rule = AsUnderlyingType(gameRule.Rule);
        std::visit([&]<typename T>(T value)
        {
            if constexpr (std::is_same_v<T, float>)
                rule.ValueF = value;
            else
                rule.Value = value;
        }, gameRule.Value);
    }

    features.AvailableGameModeIDs.push_back(8); // GameMode.db2, standard

    SendPacket(features.Write());

    std::vector<WorldPackets::System::MirrorVarSingle> vars =
    {
        { "raidLockoutExtendEnabled"sv, "1"sv },
        { "sellAllJunkEnabled"sv, "1"sv },
        { "bypassItemLevelScalingCode"sv, "0"sv },
        { "shop2Enabled"sv, "0"sv },
        { "bpayStoreEnable"sv, battlePayEnabled ? "1"sv : "0"sv },
        { "recentAlliesEnabledClient"sv, "0"sv },
        { "browserEnabled"sv, "0"sv },
        // Housing services, sent only at the character screen (hbcd3 1442).
        { "housingServiceEnabled"sv, "1"sv },
        { "housingEnableBuyHouse"sv, sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_BUY_HOUSE) ? "1"sv : "0"sv },
        { "housingEnableDeleteHouse"sv, sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_DELETE_HOUSE) ? "1"sv : "0"sv },
        { "housingEnableMoveHouse"sv, sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_MOVE_HOUSE) ? "1"sv : "0"sv },
        { "housingEnableCreateCharterNeighborhood"sv, sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_CREATE_CHARTER_NEIGHBORHOOD) ? "1"sv : "0"sv },
        { "housingEnableCreateGuildNeighborhood"sv, sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_CREATE_GUILD_NEIGHBORHOOD) ? "1"sv : "0"sv },
        // Blueprints are new in 12.1 and no capture names these three; they come from agatho's reading of the 12.1
        // client, which gates C_HousingBlueprint.GetFeatureAvailability, GetImportAvailability and
        // GetExportAvailability on them.
        { "housingBlueprintsEnabled"sv, "1"sv },
        { "housingBlueprintImportEnabled"sv, "1"sv },
        { "housingBlueprintExportEnabled"sv, "1"sv },
    };
    AppendHousingMirrorVars(vars, false);

    WorldPackets::System::MirrorVars variables;
    variables.Variables = vars;
    SendPacket(variables.Write());
}

/*static*/ void WorldSession::AppendHousingMirrorVars(std::vector<WorldPackets::System::MirrorVarSingle>& vars, bool inWorld)
{
    // Retail sends these at the character screen (hbcd3 1442) and again in the world after login and after each map
    // change (hbcd3 167415, 245836, 356261, 1344902, 1456481), with the same values.
    vars.insert(vars.end(),
    {
        { "performHousingExpansionCheckClient"sv, "1"sv },
        { "housingExteriorTypeByNeighborhoodFactionRestriction"sv, "1"sv },
        { "housingExteriorLightsAllowed"sv, "1"sv },
        { "housingExteriorLightsRadiusMultiplier"sv, "0.500000"sv },
        { "housingBasicDecor_MaxPreviewLimit"sv, "100"sv },
        { "housingCatalog_CartSizeLimit"sv, "20"sv },
        { "housingExpertDecor_Scale_Indoor_Min"sv, "0.200000"sv },
        { "housingExpertDecor_Scale_Indoor_Max"sv, "2.000000"sv },
        { "housingExpertDecor_Scale_Outdoor_Min"sv, "0.200000"sv },
        { "housingExpertDecor_Scale_Outdoor_Max"sv, "2.000000"sv },
        { "housingDecorReportScreenshotFacingDotThreshold"sv, "0.500000"sv },
        { "housingDecorReportScreenshotDistanceThreshold"sv, "150.000000"sv },
        { "housingMarketEnabled"sv, "1"sv },
        { "housingMarketShopEnabled"sv, "1"sv },
        { "housingMarketCartFullRemoveEnabled"sv, "1"sv },
        { "housingMarketViewInStoreTelemThrottle"sv, "5"sv },
        { "housingMarketViewBundleTelemThrottle"sv, "10"sv },
        { "housingMarketAddToCartTelemThrottle"sv, "15"sv },
        { "housingMarketClearCartTelemThrottle"sv, "5"sv },
        { "housingMarketRemoveFromCartTelemThrottle"sv, "20"sv },
        { "housingMarketThrottleTimePeriodMs"sv, "10000"sv },
    });

    // Sent in the world only; the character screen list does not have it.
    if (inWorld)
        vars.emplace_back("minNeighborhoodGroupMembers"sv, "3"sv);
}

void WorldSession::SendHousingMirrorVars()
{
    std::vector<WorldPackets::System::MirrorVarSingle> vars;
    AppendHousingMirrorVars(vars, true);

    WorldPackets::System::MirrorVars variables;
    variables.Variables = vars;
    SendPacket(variables.Write());
}

void WorldSession::UpdateTimerunningSeason()
{
    if (!_timerunningStatusSent)
        return;

    Timerunning::State timerunning = sTimerunningMgr->GetState();
    if (GetPlayer() && !timerunning.CanEnterWorld(GetPlayer()->GetTimerunningSeasonId()))
    {
        KickPlayer("Timerunning season is no longer available; retaining seasonal character state");
        return;
    }
    if (_timerunningSeasonId == int32(timerunning.ActiveSeason) && _timerunningSeasonEnd == timerunning.EndTime)
        return;

    SendFeatureSystemStatusGlueScreen();
    if (GetPlayer() && GetPlayer()->IsInWorld())
        SendFeatureSystemStatus();
}
