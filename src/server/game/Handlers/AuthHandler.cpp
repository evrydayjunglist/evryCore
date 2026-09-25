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
#include "BattlePayMgr.h"
#include "BattlenetRpcErrorCodes.h"
#include "CharacterTemplateDataStore.h"
#include "ClientConfigPackets.h"
#include "DisableMgr.h"
#include "GameTime.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "RealmList.h"
#include "SystemPackets.h"
#include "Timezone.h"
#include "Timerunning.h"
#include "Util.h"
#include "World.h"
#include <vector>

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
    // CatalogShop chrome on retail arrives with CommerceServerEnabled true beside shop2 MirrorVars.
    features.CommerceServerEnabled = battlePayEnabled;
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

    // Use Boost token: C_SharedCharacterServices.GetUpgradeDistributions()[11].amount
    // (Dist nest UnkInt4). Glue ActiveBoostType + TrialBoostType 11 match that boost type.
    // TrialBoostEnabled is also IsTrialBoostEnabled() (Try New Class / Enter World).
    // Retail unused-boost glue also had BoostEnabled true with ActiveBoostType 11.
    if (GetBattlePayMgr() && GetBattlePayMgr()->HasAvailableL80Distribution())
    {
        features.BoostEnabled = true;
        features.TrialBoostEnabled = true;
        features.ActiveBoostType = BattlePayMgr::GetL80BoostType();
        features.TrialBoostType = BattlePayMgr::GetL80BoostType();
    }

    SendPacket(features.Write());
    TC_LOG_INFO("server.worldserver",
        "BattlePay: glue screen TrialBoostEnabled={} BoostEnabled={} ActiveBoostType={} TrialBoostType={} account {}",
        features.TrialBoostEnabled, features.BoostEnabled, features.ActiveBoostType, features.TrialBoostType, GetAccountId());

    bool const catalogShopEnabled = battlePayEnabled && sWorld->getBoolConfig(CONFIG_BATTLE_PAY_SHOP2_ENABLED);
    constexpr std::string_view shop2LocalUrl = "https://localhost"sv;
    constexpr std::string_view shop2ClientId = "33dad602838b47bfa5ca03adaebac54c"sv;
    constexpr std::string_view shop2Pop = "b94aa31b-f910-4ec8-a180-3fb6bdac3215"sv;
    constexpr std::string_view shop2VcPlacement = "9aad42ec-a68c-487b-a300-b67518267a85"sv;
    constexpr std::string_view shop2VcSegment = "T2_WOW_US"sv;

    std::vector<WorldPackets::System::MirrorVarSingle> vars;
    vars.emplace_back("raidLockoutExtendEnabled"sv, "1"sv);
    vars.emplace_back("sellAllJunkEnabled"sv, "1"sv);
    vars.emplace_back("bypassItemLevelScalingCode"sv, "0"sv);
    vars.emplace_back("shop2Enabled"sv, catalogShopEnabled ? "1"sv : "0"sv);
    vars.emplace_back("bpayStoreEnable"sv, battlePayEnabled ? "1"sv : "0"sv);
    if (catalogShopEnabled)
    {
        vars.emplace_back("shop2HostUrlRequests"sv, shop2LocalUrl);
        vars.emplace_back("shop2HostUrlAuth"sv, shop2LocalUrl);
        vars.emplace_back("shop2ClientIdStr"sv, shop2ClientId);
        vars.emplace_back("shop2ClientRetriesEnabled"sv, "1"sv);
        vars.emplace_back("shop2PMTMaxTries"sv, "15"sv);
        vars.emplace_back("shop2SFMMaxTries"sv, "15"sv);
        vars.emplace_back("shop2SSOMaxTries"sv, "15"sv);
        vars.emplace_back("shop2PMTPerMilliseconds"sv, "1000"sv);
        vars.emplace_back("shop2SFMPerMilliseconds"sv, "1000"sv);
        vars.emplace_back("shop2SSOPerMilliseconds"sv, "1000"sv);
        vars.emplace_back("shop2DefaultCurrencyMaxTries"sv, "5"sv);
        vars.emplace_back("shop2DefaultCurrencyPerMilliseconds"sv, "1000"sv);
        vars.emplace_back("shop2DynamicBundleMaxTries"sv, "10"sv);
        vars.emplace_back("shop2DynamicBundlePerMilliseconds"sv, "1000"sv);
        vars.emplace_back("shop2OrderStatusMaxTries"sv, "3"sv);
        vars.emplace_back("shop2OrderStatusPerMilliseconds"sv, "1000"sv);
        vars.emplace_back("shop2VirtualCurrencyBalanceMaxTries"sv, "3"sv);
        vars.emplace_back("shop2VirtualCurrencyBalancePerMilliseconds"sv, "1000"sv);
        vars.emplace_back("shop2PendingOrderPollSeconds"sv, "5"sv);
        vars.emplace_back("shop2PendingOrderPollFileEnabled"sv, "1"sv);
        vars.emplace_back("shop2PendingOrderPollCheckoutEnabled"sv, "1"sv);
        vars.emplace_back("shop2TelemetryAllowed"sv, "0"sv);
        vars.emplace_back("shop2ClientErrorTelemetryAllowed"sv, "0"sv);
        vars.emplace_back("shop2UseConnectedRealmGameServiceRegionId"sv, "0"sv);
        vars.emplace_back("shop2AdditionalScopesStr"sv, ""sv);
        vars.emplace_back("shop2BlockedPlacements"sv, ""sv);
        vars.emplace_back("shop2POPStr"sv, shop2Pop);
        vars.emplace_back("shop2VCPlacementStr"sv, shop2VcPlacement);
        vars.emplace_back("shop2VCSegmentStr"sv, shop2VcSegment);
    }
    vars.emplace_back("recentAlliesEnabledClient"sv, "0"sv);
    vars.emplace_back("browserEnabled"sv, catalogShopEnabled ? "1"sv : "0"sv);
    // Housing services, sent only at the character screen (hbcd3 1442).
    vars.emplace_back("housingServiceEnabled"sv, "1"sv);
    vars.emplace_back("housingEnableBuyHouse"sv, sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_BUY_HOUSE) ? "1"sv : "0"sv);
    vars.emplace_back("housingEnableDeleteHouse"sv, sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_DELETE_HOUSE) ? "1"sv : "0"sv);
    vars.emplace_back("housingEnableMoveHouse"sv, sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_MOVE_HOUSE) ? "1"sv : "0"sv);
    vars.emplace_back("housingEnableCreateCharterNeighborhood"sv, sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_CREATE_CHARTER_NEIGHBORHOOD) ? "1"sv : "0"sv);
    vars.emplace_back("housingEnableCreateGuildNeighborhood"sv, sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_CREATE_GUILD_NEIGHBORHOOD) ? "1"sv : "0"sv);
    // Blueprints are new in 12.1 and no capture names these three; they come from agatho's housing branch, read from the 12.1
    // client, which gates C_HousingBlueprint.GetFeatureAvailability, GetImportAvailability and
    // GetExportAvailability on them.
    vars.emplace_back("housingBlueprintsEnabled"sv, "1"sv);
    vars.emplace_back("housingBlueprintImportEnabled"sv, "1"sv);
    vars.emplace_back("housingBlueprintExportEnabled"sv, "1"sv);
    AppendHousingMirrorVars(vars, false);

    WorldPackets::System::MirrorVars variables;
    variables.Variables = vars;
    SendPacket(variables.Write());

    if (catalogShopEnabled)
        GetBattlePayMgr()->SendCatalogShopObtainLicenses();

    // Char-select OnShow reads GetUpgradeDistributions before GET_PRODUCT_LIST returns.
    // Push Dist with glue so the Use Boost token is already counted.
    if (GetBattlePayMgr())
        GetBattlePayMgr()->SendAvailableL80Distributions();
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
