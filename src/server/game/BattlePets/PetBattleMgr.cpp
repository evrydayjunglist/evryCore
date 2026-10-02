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

#include "PetBattleMgr.h"
#include "BattlePetMgr.h"
#include "BattlePetPackets.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "GameTime.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SceneObject.h"
#include "WorldSession.h"
#include <algorithm>

// Observed on retail 12.1.0.69587: QUEUED resent 7.3 s after the join; AvgWaitTime 60 s in both frames.
static constexpr uint32 PET_BATTLE_QUEUE_STATUS_UPDATE_INTERVAL = 7000;
static constexpr uint64 PET_BATTLE_QUEUE_AVG_WAIT_SECS = 60;


namespace PetBattles
{

PetBattleMgr::PetBattleMgr() = default;
PetBattleMgr::~PetBattleMgr() = default;

PetBattleMgr* PetBattleMgr::instance()
{
    static PetBattleMgr instance;
    return &instance;
}

void PetBattleMgr::Initialize()
{
    TC_LOG_INFO("server.loading", ">> Loading Pet Battle data...");

    uint32 speciesAbilityCount = 0;
    uint32 abilityTurnCount = 0;
    uint32 turnEffectCount = 0;

    // Build speciesID -> abilities index from BattlePetSpeciesXAbility DB2
    for (BattlePetSpeciesXAbilityEntry const* entry : sBattlePetSpeciesXAbilityStore)
    {
        _speciesAbilities[entry->BattlePetSpeciesID].push_back(entry->BattlePetAbilityID);
        _speciesAbilitiesFull[entry->BattlePetSpeciesID].push_back(entry);
        ++speciesAbilityCount;
    }

    // Build abilityID -> turns index from BattlePetAbilityTurn DB2
    for (BattlePetAbilityTurnEntry const* entry : sBattlePetAbilityTurnStore)
    {
        _abilityTurns[entry->BattlePetAbilityID].push_back(entry->ID);
        _abilityTurnsFull[entry->BattlePetAbilityID].push_back(entry);
        ++abilityTurnCount;
    }

    // Sort turns by OrderIndex for correct multi-turn sequencing
    for (auto& [abilityID, turns] : _abilityTurnsFull)
    {
        std::sort(turns.begin(), turns.end(),
            [](BattlePetAbilityTurnEntry const* a, BattlePetAbilityTurnEntry const* b)
            {
                return a->OrderIndex < b->OrderIndex;
            });
    }

    // Build turnID -> effects index from BattlePetAbilityEffect DB2
    for (BattlePetAbilityEffectEntry const* entry : sBattlePetAbilityEffectStore)
    {
        _turnEffects[entry->BattlePetAbilityTurnID].push_back(entry->ID);
        _turnEffectsFull[entry->BattlePetAbilityTurnID].push_back(entry);
        ++turnEffectCount;
    }

    // Sort effects by OrderIndex for correct execution order
    for (auto& [turnID, effects] : _turnEffectsFull)
    {
        std::sort(effects.begin(), effects.end(),
            [](BattlePetAbilityEffectEntry const* a, BattlePetAbilityEffectEntry const* b)
            {
                return a->OrderIndex < b->OrderIndex;
            });
    }

    // Build breed quality multiplier map from BattlePetBreedQuality DB2
    for (BattlePetBreedQualityEntry const* entry : sBattlePetBreedQualityStore)
        _breedQualityMultipliers[entry->QualityEnum] = entry->StateMultiplier;

    // Build breed base stats map from BattlePetBreedState DB2
    // StateID: 18=MaxHP (Stamina), 19=Power, 20=Speed
    for (BattlePetBreedStateEntry const* entry : sBattlePetBreedStateStore)
    {
        auto& [hp, power, speed] = _breedBaseStats[entry->BattlePetBreedID];
        switch (entry->BattlePetStateID)
        {
            case BattlePets::STATE_STAT_STAMINA: hp = entry->Value; break;
            case BattlePets::STATE_STAT_POWER:   power = entry->Value; break;
            case BattlePets::STATE_STAT_SPEED:   speed = entry->Value; break;
            default: break;
        }
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} species abilities, {} ability turns, {} turn effects",
        speciesAbilityCount, abilityTurnCount, turnEffectCount);
    TC_LOG_INFO("server.loading", ">> Loaded {} breed quality entries, {} breed stat entries",
        uint32(_breedQualityMultipliers.size()), uint32(_breedBaseStats.size()));

    // Build the BattlePetEffectPropertiesID -> action type mapping
    BuildEffectActionMap();

    // Build weather ability ID set using DB2 data (generic approach)
    // Weather abilities are identified by:
    // 1. Having effects with WEATHER_SET PropsID (effects that set weatherState on environment)
    // 2. Having aura effects whose sub-abilities set weather state
    // 3. Having BattlePetAbilityState entries that set weather-related states
    {
        // Step 1: Find PropsIDs classified as WEATHER_SET
        std::unordered_set<uint16> weatherPropsIDs;
        for (auto const& [propsID, action] : _effectActionMap)
            if (action == PET_BATTLE_EFFECT_ACTION_WEATHER_SET)
                weatherPropsIDs.insert(propsID);

        // Step 2: Find abilities containing effects with weather PropsIDs (traces effect → turn → ability)
        std::unordered_set<uint32> weatherTurnIDs;
        for (BattlePetAbilityEffectEntry const* effect : sBattlePetAbilityEffectStore)
            if (weatherPropsIDs.count(effect->BattlePetEffectPropertiesID))
                weatherTurnIDs.insert(effect->BattlePetAbilityTurnID);

        for (BattlePetAbilityTurnEntry const* turn : sBattlePetAbilityTurnStore)
            if (weatherTurnIDs.count(turn->ID))
                _weatherAbilityIDs.insert(turn->BattlePetAbilityID);

        // Step 3: For found weather abilities, also collect their AuraBattlePetAbilityIDs
        // (sub-abilities used as weather auras)
        std::unordered_set<uint32> allWeatherTurnIDs;
        for (BattlePetAbilityTurnEntry const* turn : sBattlePetAbilityTurnStore)
            if (_weatherAbilityIDs.count(turn->BattlePetAbilityID))
                allWeatherTurnIDs.insert(turn->ID);

        for (BattlePetAbilityEffectEntry const* effect : sBattlePetAbilityEffectStore)
            if (allWeatherTurnIDs.count(effect->BattlePetAbilityTurnID) && effect->AuraBattlePetAbilityID != 0)
                _weatherAbilityIDs.insert(effect->AuraBattlePetAbilityID);

        // Step 4: Reverse walk — find abilities whose AuraBattlePetAbilityID is in the weather set
        // This catches parent abilities that apply weather auras but don't have WEATHER_SET effects directly
        bool foundNew = true;
        while (foundNew)
        {
            foundNew = false;
            for (BattlePetAbilityEffectEntry const* effect : sBattlePetAbilityEffectStore)
            {
                if (effect->AuraBattlePetAbilityID == 0)
                    continue;
                if (!_weatherAbilityIDs.count(effect->AuraBattlePetAbilityID))
                    continue;
                // Find the parent ability of this effect
                BattlePetAbilityTurnEntry const* turn = sBattlePetAbilityTurnStore.LookupEntry(effect->BattlePetAbilityTurnID);
                if (!turn)
                    continue;
                if (_weatherAbilityIDs.insert(turn->BattlePetAbilityID).second)
                    foundNew = true;
            }
        }

        // Step 5: BattlePetAbilityState-based detection
        // Build a map of abilityID → set of stateIDs from BattlePetAbilityState DB2
        std::unordered_map<uint32, std::vector<std::pair<uint32, int32>>> abilityStates;
        for (BattlePetAbilityStateEntry const* entry : sBattlePetAbilityStateStore)
            abilityStates[entry->BattlePetAbilityID].push_back({ entry->BattlePetStateID, entry->Value });

        // Store BattlePetAbilityState entries for weather abilities (used at runtime for modifiers)
        for (uint32 abilID : _weatherAbilityIDs)
        {
            auto it = abilityStates.find(abilID);
            if (it != abilityStates.end())
            {
                _weatherAbilityStates[abilID] = it->second;
                for (auto const& [stateID, value] : it->second)
                    TC_LOG_DEBUG("server.loading", "  WeatherAbil {} has AbilityState: stateID={} value={}", abilID, stateID, value);
            }
        }

        TC_LOG_INFO("server.loading", ">> Found {} weather ability IDs", uint32(_weatherAbilityIDs.size()));
        for (uint32 abilID : _weatherAbilityIDs)
        {
            BattlePetAbilityEntry const* ability = sBattlePetAbilityStore.LookupEntry(abilID);
            TC_LOG_DEBUG("server.loading", "  WeatherAbility: ID={} name={}", abilID,
                ability ? ability->Name.Str[LOCALE_enUS] : "???");
        }
    }

    BuildAuraTargetMap();

    LoadNPCTeams();

    TC_LOG_INFO("server.loading", ">> Pet Battle system initialized");
}

void PetBattleMgr::BuildEffectActionMap()
{
    // Classify every BattlePetEffectProperties entry into an abstract action type
    // based on its ParamLabel signatures and BattlePetVisualID.
    // This runs once at startup so string comparisons are acceptable.

    uint32 mappedCount = 0;
    uint32 unclassifiedCount = 0;
    std::vector<uint16> unclassifiedIDs;
    for (BattlePetEffectPropertiesEntry const* props : sBattlePetEffectPropertiesStore)
    {
        bool hasPoints = false, hasDuration = false, hasState = false;
        bool hasChance = false, hasPercentage = false;
        bool hasWeather = false, hasSwap = false, hasTrap = false;
        bool hasAnyLabel = false;
        for (uint8 i = 0; i < 6; ++i)
            if (props->ParamLabel[i] && props->ParamLabel[i][0])
            {
                hasAnyLabel = true;
                break;
            }

        // Case-insensitive substring search helper
        auto containsCI = [](std::string_view haystack, std::string_view needle) -> bool
        {
            if (needle.empty() || haystack.size() < needle.size())
                return needle.empty();
            for (size_t i = 0; i + needle.size() <= haystack.size(); ++i)
            {
                bool match = true;
                for (size_t j = 0; j < needle.size(); ++j)
                {
                    char a = haystack[i + j];
                    char b = needle[j];
                    if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
                    if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
                    if (a != b) { match = false; break; }
                }
                if (match) return true;
            }
            return false;
        };

        for (uint8 i = 0; i < 6; ++i)
        {
            if (!props->ParamLabel[i] || !props->ParamLabel[i][0])
                continue;
            std::string_view label(props->ParamLabel[i]);

            // Weather check first — any label containing "weather" (case-insensitive) wins,
            // since the State substring would otherwise swallow "WeatherStateID" etc.
            if (containsCI(label, "weather")) { hasWeather = true; continue; }

            if (label == "Points") hasPoints = true;
            else if (label == "Percentage") { hasPoints = true; hasPercentage = true; }
            else if (label == "Duration") hasDuration = true;
            else if (label.find("State") != std::string_view::npos) hasState = true;
            else if (label == "Chance") hasChance = true;
            else if (label == "SwapIndex" || label == "Swap") hasSwap = true;
            else if (label == "TrapAbility") hasTrap = true;
        }

        PetBattleAbilityEffectAction action;

        if (hasWeather)
            action = PET_BATTLE_EFFECT_ACTION_WEATHER_SET;
        else if (hasSwap)
            action = PET_BATTLE_EFFECT_ACTION_PET_SWAP;
        else if (hasTrap)
            action = PET_BATTLE_EFFECT_ACTION_CATCH;
        else if (hasDuration && hasPoints && props->BattlePetVisualID == 38)
            action = PET_BATTLE_EFFECT_ACTION_PERIODIC_DAMAGE;
        else if (hasDuration && hasPoints && props->BattlePetVisualID != 38)
            action = PET_BATTLE_EFFECT_ACTION_PERIODIC_HEAL;
        else if (hasDuration)
            action = PET_BATTLE_EFFECT_ACTION_APPLY_AURA;
        else if (hasPoints && hasPercentage && props->BattlePetVisualID == 38)
            action = PET_BATTLE_EFFECT_ACTION_DAMAGE_PERCENTAGE;
        else if (hasPoints && hasPercentage)
            action = PET_BATTLE_EFFECT_ACTION_HEAL_PERCENTAGE;
        else if (hasPoints && props->BattlePetVisualID == 38)
            action = PET_BATTLE_EFFECT_ACTION_DAMAGE;
        else if (hasPoints)
            action = PET_BATTLE_EFFECT_ACTION_HEAL;
        else if (hasState)
            action = PET_BATTLE_EFFECT_ACTION_SET_STATE;
        else if (hasChance)
            action = PET_BATTLE_EFFECT_ACTION_DAMAGE; // Conditional — evaluate at use site, fall through to damage
        else
        {
            // Unclassified: route to the ProcessEffect default branch (skip + log) instead of
            // defaulting to DAMAGE, so an effect we don't understand never fabricates damage.
            action = PET_BATTLE_EFFECT_ACTION_UNKNOWN;
            if (hasAnyLabel)
            {
                ++unclassifiedCount;
                if (unclassifiedIDs.size() < 32)
                    unclassifiedIDs.push_back(static_cast<uint16>(props->ID));
            }
        }

        _effectActionMap[static_cast<uint16>(props->ID)] = action;
        ++mappedCount;
    }

    // Surface entries that had labels but didn't match any classification rule
    // — these are the candidates for explicit mapping next.
    if (unclassifiedCount)
    {
        std::map<uint16, uint32> usageCounts;
        for (BattlePetAbilityEffectEntry const* entry : sBattlePetAbilityEffectStore)
            usageCounts[entry->BattlePetEffectPropertiesID]++;

        TC_LOG_WARN("server.loading", "PetBattle: {} EffectProperties entries fell through classifier "
            "(default = DAMAGE). Sample IDs follow:", unclassifiedCount);
        for (uint16 propsID : unclassifiedIDs)
        {
            BattlePetEffectPropertiesEntry const* p = sBattlePetEffectPropertiesStore.LookupEntry(propsID);
            std::string labels;
            if (p)
                for (uint8 i = 0; i < 6; ++i)
                    if (p->ParamLabel[i] && p->ParamLabel[i][0] != '\0')
                        labels += Trinity::StringFormat("[{}]={} ", i, p->ParamLabel[i]);
            TC_LOG_DEBUG("server.loading", "  Unclassified PropsID={} uses={} visual={} labels: {}",
                propsID, usageCounts.count(propsID) ? usageCounts[propsID] : 0,
                p ? p->BattlePetVisualID : 0, labels);
        }
    }

    // Log all mapped entries for debugging
    {
        std::map<uint16, uint32> usageCounts;
        for (BattlePetAbilityEffectEntry const* entry : sBattlePetAbilityEffectStore)
            usageCounts[entry->BattlePetEffectPropertiesID]++;

        for (auto const& [propsID, action] : _effectActionMap)
        {
            BattlePetEffectPropertiesEntry const* props = sBattlePetEffectPropertiesStore.LookupEntry(propsID);
            std::string labels;
            if (props)
                for (uint8 i = 0; i < 6; ++i)
                    if (props->ParamLabel[i] && props->ParamLabel[i][0] != '\0')
                        labels += Trinity::StringFormat("[{}]={} ", i, props->ParamLabel[i]);
            TC_LOG_DEBUG("server.loading", "  EffectMap: PropsID={:3d} -> action={:2d} uses={:3d} visual={} labels: {}",
                propsID, uint16(action), usageCounts.count(propsID) ? usageCounts[propsID] : 0,
                props ? props->BattlePetVisualID : 0, labels);
        }
    }

    TC_LOG_INFO("server.loading", ">> Mapped {} BattlePetEffectProperties entries to action types", mappedCount);
}

PetBattleAbilityEffectAction PetBattleMgr::GetEffectAction(uint16 propsID) const
{
    auto it = _effectActionMap.find(propsID);
    if (it != _effectActionMap.end())
        return it->second;
    return PET_BATTLE_EFFECT_ACTION_UNKNOWN; // Unmapped IDs: skip + log, never fabricate damage
}

AuraTargetType PetBattleMgr::GetAuraTarget(uint32 abilityID) const
{
    auto it = _auraTargetMap.find(abilityID);
    return it != _auraTargetMap.end() ? it->second : AURA_TARGET_AMBIGUOUS;
}

void PetBattleMgr::BuildAuraTargetMap()
{
    // Group BattlePetAbilityState entries by ability so we can score
    // each ability based on the semantics of the states it applies.
    std::unordered_map<uint32, std::vector<std::pair<uint32, int32>>> abilityStates;
    for (BattlePetAbilityStateEntry const* entry : sBattlePetAbilityStateStore)
        abilityStates[entry->BattlePetAbilityID].push_back({ entry->BattlePetStateID, entry->Value });

    // Helper: classify a single (stateID, value) pair as a self-buff or enemy-debuff signal.
    // Returns +1 for SELF, -1 for ENEMY, 0 for unknown / neutral.
    auto classifyState = [](uint32 stateID, int32 value) -> int
    {
        switch (stateID)
        {
            // Mechanic flags (1 = enable) — always applied to enemy
            case BattlePets::STATE_MECHANIC_IS_POISONED:
            case BattlePets::STATE_MECHANIC_IS_STUNNED:
            case BattlePets::STATE_MECHANIC_IS_BLIND:
                return value != 0 ? -1 : 0;

            // "Damage dealt" / "healing dealt" / flat-dealt / max-HP / crit / dodge:
            // positive = self-buff, negative = self-debuff (still applied to self).
            case BattlePets::STATE_MOD_DAMAGE_DEALT_PERCENT:
            case BattlePets::STATE_MOD_HEALING_DEALT_PERCENT:
            case BattlePets::STATE_MOD_HEALING_TAKEN_PERCENT:
            case BattlePets::STATE_ADD_FLAT_DAMAGE_DEALT:
            case BattlePets::STATE_MOD_PET_TYPE_DAMAGE_DEALT_PCT:
            case BattlePets::STATE_MOD_MAX_HEALTH_PERCENT:
            case BattlePets::STATE_STAT_CRIT_CHANCE:
            case BattlePets::STATE_STAT_DODGE:
                return +1;

            // "Damage taken" / flat-taken / per-type-taken — debuff applied to enemy.
            case BattlePets::STATE_MOD_DAMAGE_TAKEN_PERCENT:
            case BattlePets::STATE_ADD_FLAT_DAMAGE_TAKEN:
            case BattlePets::STATE_MOD_PET_TYPE_DAMAGE_TAKEN_PCT:
                return -1;

            // Speed / accuracy: positive = self-buff, negative = enemy-debuff.
            case BattlePets::STATE_MOD_SPEED_PERCENT:
            case BattlePets::STATE_STAT_ACCURACY:
                if (value > 0) return +1;
                if (value < 0) return -1;
                return 0;

            default:
                return 0;
        }
    };

    uint32 selfCount = 0, enemyCount = 0, ambiguousCount = 0, unscoredCount = 0;
    for (auto const& [abilityID, states] : abilityStates)
    {
        int score = 0;
        bool anySignal = false;
        for (auto const& [stateID, value] : states)
        {
            int s = classifyState(stateID, value);
            if (s != 0)
            {
                score += s;
                anySignal = true;
            }
        }

        AuraTargetType type;
        if (!anySignal) { type = AURA_TARGET_AMBIGUOUS; ++unscoredCount; }
        else if (score > 0) { type = AURA_TARGET_SELF; ++selfCount; }
        else if (score < 0) { type = AURA_TARGET_ENEMY; ++enemyCount; }
        else { type = AURA_TARGET_AMBIGUOUS; ++ambiguousCount; }

        _auraTargetMap[abilityID] = type;
    }

    TC_LOG_INFO("server.loading", ">> Built aura-target map: {} self-buffs, {} enemy-debuffs, "
        "{} mixed/unscored ({} no recognized state)",
        selfCount, enemyCount, ambiguousCount + unscoredCount, unscoredCount);
}

bool PetBattleMgr::IsWeatherAbility(uint32 abilityID) const
{
    return _weatherAbilityIDs.count(abilityID) > 0;
}

std::vector<std::pair<uint32, int32>> const* PetBattleMgr::GetWeatherAbilityStates(uint32 abilityID) const
{
    auto it = _weatherAbilityStates.find(abilityID);
    return it != _weatherAbilityStates.end() ? &it->second : nullptr;
}

void PetBattleMgr::Update(uint32 diff)
{
    AbandonDepartedPlayers();

    for (auto itr = _pvpChallenges.begin(); itr != _pvpChallenges.end();)
    {
        itr->second.AgeMs += diff;
        if (itr->second.AgeMs >= PET_BATTLE_PVP_CHALLENGE_TIMEOUT)
            itr = _pvpChallenges.erase(itr);
        else
            ++itr;
    }

    // Update all active battles
    std::vector<uint32> finishedBattles;
    for (auto& [battleID, battle] : _activeBattles)
    {
        battle->Update(diff);
        if (battle->IsFinished())
            finishedBattles.push_back(battleID);
    }

    // Clean up finished battles after iteration
    for (uint32 battleID : finishedBattles)
        RemoveBattle(battleID);

    // Check PvP proposal timeout
    if (_pendingProposal)
    {
        _pendingProposal->ProposalTime += diff;
        if (_pendingProposal->ProposalTime >= PET_BATTLE_PVP_PROPOSAL_TIMEOUT)
        {
            // Timeout: notify both players, re-queue them
            for (ObjectGuid const& guid : { _pendingProposal->Player1, _pendingProposal->Player2 })
            {
                if (Player* player = ObjectAccessor::FindPlayer(guid))
                {
                    SendQueueStatus(player, PET_BATTLE_QUEUE_STATUS_PROPOSAL_TIMED_OUT);
                    Enqueue(guid);
                }
            }
            _pendingProposal.reset();
        }
    }

    // Retail (12.1.0.69587 capture): a queued client receives SMSG_PET_BATTLE_QUEUE_STATUS again
    // while it waits -- QUEUED with the elapsed ClientWaitTime (observed once, 7.3 s after the join).
    for (PvPQueueEntry& entry : _pvpQueue)
    {
        entry.UpdateTimer += diff;
        if (entry.UpdateTimer < PET_BATTLE_QUEUE_STATUS_UPDATE_INTERVAL)
            continue;
        entry.UpdateTimer = 0;
        if (Player* player = ObjectAccessor::FindPlayer(entry.PlayerGUID))
            SendQueueStatus(player, PET_BATTLE_QUEUE_STATUS_QUEUED, &entry);
    }

    // Try to match PvP queue players periodically (every 5 seconds)
    _queueMatchTimer += diff;
    if (_queueMatchTimer >= 5000)
    {
        _queueMatchTimer = 0;
        TryMatchPlayers();
    }
}

// ============================================================================
// Battle lifecycle
// ============================================================================

PetBattle* PetBattleMgr::CreateWildBattle(Player* player, ObjectGuid wildCreatureGUID)
{
    if (IsPlayerInBattle(player->GetGUID()))
        return nullptr;

    uint32 battleID = _nextBattleID++;
    auto battle = std::make_unique<PetBattle>();
    battle->SetBattleID(battleID);
    battle->InitWildBattle(player, wildCreatureGUID);

    // The 12.1 client only reads the battle packets when they carry the GUID of a SceneObject of
    // SceneType::PetBattle. A wild battle has one player, so the object is private to that player.
    if (SceneObject* sceneObject = SceneObject::CreatePetBattleSceneObject(player, player->GetPosition(), player->GetGUID()))
        battle->SetSceneObjectGUID(sceneObject->GetGUID());

    PetBattle* ptr = battle.get();
    _activeBattles[battleID] = std::move(battle);
    _playerToBattle[player->GetGUID()] = battleID;

    TC_LOG_DEBUG("server.loading", "PetBattleMgr: Created wild battle {} for player {}",
        battleID, player->GetGUID().ToString());

    return ptr;
}

PetBattle* PetBattleMgr::CreatePvPBattle(Player* player1, Player* player2)
{
    if (IsPlayerInBattle(player1->GetGUID()) || IsPlayerInBattle(player2->GetGUID()))
        return nullptr;

    uint32 battleID = _nextBattleID++;
    auto battle = std::make_unique<PetBattle>();
    battle->SetBattleID(battleID);
    battle->InitPvPBattle(player1, player2);

    if (battle->GetBattleState() == PET_BATTLE_STATE_CREATED_FAILED)
        return nullptr; // a team has no living pet

    // Both players must see the scene object, and a private object is only shown to its owner and
    // the owner's group, so this one is public. How retail limits it for a duel is not known.
    if (SceneObject* sceneObject = SceneObject::CreatePetBattleSceneObject(player1, player1->GetPosition(), ObjectGuid::Empty))
        battle->SetSceneObjectGUID(sceneObject->GetGUID());

    PetBattle* ptr = battle.get();
    _activeBattles[battleID] = std::move(battle);
    _playerToBattle[player1->GetGUID()] = battleID;
    _playerToBattle[player2->GetGUID()] = battleID;

    return ptr;
}

PetBattle* PetBattleMgr::CreateNPCBattle(Player* player, Creature* trainer)
{
    if (!player || !trainer)
        return nullptr;

    if (IsPlayerInBattle(player->GetGUID()))
        return nullptr;

    std::vector<NPCTeamPetInfo> const* npcTeam = GetNPCTeam(trainer->GetEntry());
    if (!npcTeam || npcTeam->empty())
        return nullptr;

    uint32 battleID = _nextBattleID++;
    auto battle = std::make_unique<PetBattle>();
    battle->SetBattleID(battleID);
    battle->InitNPCBattle(player, trainer, *npcTeam);

    // One player, so the scene object is private to that player (see CreateWildBattle).
    if (SceneObject* sceneObject = SceneObject::CreatePetBattleSceneObject(player, player->GetPosition(), player->GetGUID()))
        battle->SetSceneObjectGUID(sceneObject->GetGUID());

    PetBattle* ptr = battle.get();
    _activeBattles[battleID] = std::move(battle);
    _playerToBattle[player->GetGUID()] = battleID;

    TC_LOG_DEBUG("server.loading", "PetBattleMgr: Created NPC battle {} for player {} vs trainer {}",
        battleID, player->GetGUID().ToString(), trainer->GetEntry());

    return ptr;
}

void PetBattleMgr::RemoveBattle(uint32 battleID)
{
    auto it = _activeBattles.find(battleID);
    if (it == _activeBattles.end())
        return;

    PetBattle* battle = it->second.get();

    // Despawn the battle's scene object before tearing the battle down.
    battle->DespawnSceneObject();

    // Remove player mappings
    for (uint8 i = 0; i < MAX_PET_BATTLE_PLAYERS; ++i)
    {
        ObjectGuid playerGUID = battle->GetTeam(i).PlayerGUID;
        if (!playerGUID.IsEmpty())
            _playerToBattle.erase(playerGUID);
    }

    _activeBattles.erase(it);
}

PetBattle* PetBattleMgr::GetBattleByID(uint32 battleID)
{
    auto it = _activeBattles.find(battleID);
    return it != _activeBattles.end() ? it->second.get() : nullptr;
}

PetBattle* PetBattleMgr::GetBattleByPlayer(ObjectGuid playerGUID)
{
    auto it = _playerToBattle.find(playerGUID);
    if (it == _playerToBattle.end())
        return nullptr;
    return GetBattleByID(it->second);
}

bool PetBattleMgr::IsPlayerInBattle(ObjectGuid playerGUID) const
{
    return _playerToBattle.contains(playerGUID);
}

bool PetBattleMgr::IsCreatureInBattle(ObjectGuid creatureGUID) const
{
    for (auto const& [battleID, battle] : _activeBattles)
        if (!battle->IsFinished() && battle->GetWildCreatureGUID() == creatureGUID)
            return true;
    return false;
}

// A participant who is no longer in the world on the battle's map (logged out, moved to another
// map, or in the middle of a teleport) cannot go on fighting; the battle ends with that team losing.
void PetBattleMgr::AbandonDepartedPlayers()
{
    std::vector<uint32> ended;
    for (auto& [battleID, battle] : _activeBattles)
    {
        if (battle->IsFinished())
            continue;

        for (uint8 t = 0; t < MAX_PET_BATTLE_PLAYERS; ++t)
        {
            ObjectGuid playerGUID = battle->GetTeam(t).PlayerGUID;
            if (playerGUID.IsEmpty())
                continue;

            Player* player = ObjectAccessor::FindPlayer(playerGUID);
            if (player && player->GetMapId() == battle->GetMapId() && player->GetInstanceId() == battle->GetInstanceId()
                && !player->IsBeingTeleported())
                continue;

            battle->Abandon(t);
            ended.push_back(battleID);
            break;
        }
    }

    for (uint32 battleID : ended)
        RemoveBattle(battleID);
}

void PetBattleMgr::OnPlayerLogout(Player* player)
{
    ObjectGuid playerGUID = player->GetGUID();

    if (PetBattle* battle = GetBattleByPlayer(playerGUID))
    {
        int8 teamIdx = battle->GetTeamIndex(playerGUID);
        if (teamIdx >= 0)
            battle->Abandon(uint8(teamIdx));
        RemoveBattle(battle->GetBattleID());
    }

    RemovePlayerFromQueue(playerGUID);

    for (auto itr = _pvpChallenges.begin(); itr != _pvpChallenges.end();)
    {
        if (itr->first == playerGUID || itr->second.Challenger == playerGUID)
            itr = _pvpChallenges.erase(itr);
        else
            ++itr;
    }
}

void PetBattleMgr::AddChallenge(ObjectGuid challenger, ObjectGuid target, WorldPackets::BattlePet::PetBattleLocation const& location)
{
    PvPChallenge& challenge = _pvpChallenges[target];
    challenge.Challenger = challenger;
    challenge.Location = location;
    challenge.AgeMs = 0;
}

bool PetBattleMgr::TakeChallenge(ObjectGuid target, ObjectGuid challenger, WorldPackets::BattlePet::PetBattleLocation& location)
{
    auto itr = _pvpChallenges.find(target);
    if (itr == _pvpChallenges.end() || itr->second.Challenger != challenger)
        return false;

    location = itr->second.Location;
    _pvpChallenges.erase(itr);
    return true;
}

// ============================================================================
// DB2 index map accessors
// ============================================================================

std::vector<uint32> const* PetBattleMgr::GetSpeciesAbilities(uint32 speciesID) const
{
    auto it = _speciesAbilities.find(speciesID);
    return it != _speciesAbilities.end() ? &it->second : nullptr;
}

std::vector<uint32> const* PetBattleMgr::GetAbilityTurns(uint32 abilityID) const
{
    auto it = _abilityTurns.find(abilityID);
    return it != _abilityTurns.end() ? &it->second : nullptr;
}

std::vector<uint32> const* PetBattleMgr::GetTurnEffects(uint32 turnID) const
{
    auto it = _turnEffects.find(turnID);
    return it != _turnEffects.end() ? &it->second : nullptr;
}

std::vector<BattlePetSpeciesXAbilityEntry const*> const* PetBattleMgr::GetSpeciesAbilitiesFull(uint32 speciesID) const
{
    auto it = _speciesAbilitiesFull.find(speciesID);
    return it != _speciesAbilitiesFull.end() ? &it->second : nullptr;
}

std::vector<BattlePetAbilityTurnEntry const*> const* PetBattleMgr::GetAbilityTurnsFull(uint32 abilityID) const
{
    auto it = _abilityTurnsFull.find(abilityID);
    return it != _abilityTurnsFull.end() ? &it->second : nullptr;
}

std::vector<BattlePetAbilityEffectEntry const*> const* PetBattleMgr::GetTurnEffectsFull(uint32 turnID) const
{
    auto it = _turnEffectsFull.find(turnID);
    return it != _turnEffectsFull.end() ? &it->second : nullptr;
}

float PetBattleMgr::GetBreedQualityMultiplier(uint8 quality) const
{
    auto it = _breedQualityMultipliers.find(quality);
    return it != _breedQualityMultipliers.end() ? it->second : 1.0f;
}

void PetBattleMgr::GetBreedBaseStats(uint32 breedID, int32& outHP, int32& outPower, int32& outSpeed) const
{
    auto it = _breedBaseStats.find(breedID);
    if (it != _breedBaseStats.end())
    {
        auto const& [hp, power, speed] = it->second;
        outHP = hp;
        outPower = power;
        outSpeed = speed;
    }
    else
    {
        outHP = 100;
        outPower = 10;
        outSpeed = 10;
    }
}

// ============================================================================
// NPC Teams
// ============================================================================

void PetBattleMgr::LoadNPCTeams()
{
    _npcTeams.clear();

    QueryResult result = WorldDatabase.Query("SELECT npcEntry, slot, speciesId, level, breedId, quality, ability1, ability2, ability3, npcTeamMemberID, creatureId FROM battle_pet_npc_team ORDER BY npcEntry, slot");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 NPC pet battle teams. DB table `battle_pet_npc_team` is empty or missing.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();
        uint32 npcEntry = fields[0].GetUInt32();

        NPCTeamPetInfo pet;
        pet.SpeciesID = fields[2].GetUInt32();
        pet.Level = fields[3].GetUInt16();
        pet.BreedID = fields[4].GetUInt16();
        pet.Quality = fields[5].GetUInt8();
        pet.AbilityIDs[0] = fields[6].GetUInt32();
        pet.AbilityIDs[1] = fields[7].GetUInt32();
        pet.AbilityIDs[2] = fields[8].GetUInt32();
        pet.NpcTeamMemberID = fields[9].GetInt32();
        pet.CreatureID = fields[10].GetUInt32();

        _npcTeams[npcEntry].push_back(pet);
        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} NPC pet battle team members for {} trainers",
        count, uint32(_npcTeams.size()));
}

std::vector<NPCTeamPetInfo> const* PetBattleMgr::GetNPCTeam(uint32 npcEntry) const
{
    auto it = _npcTeams.find(npcEntry);
    return it != _npcTeams.end() ? &it->second : nullptr;
}

// ============================================================================
// PvP Queue
// ============================================================================

void PetBattleMgr::Enqueue(ObjectGuid playerGUID)
{
    PvPQueueEntry entry;
    entry.PlayerGUID = playerGUID;
    entry.TicketId = _nextQueueTicketId++;
    entry.EnqueueTime = GameTime::GetGameTime();
    _pvpQueue.push_back(entry);
}

// One packet shape for every queue outcome: the ticket names the requester (and the queue entry
// when there is one); the wait times ride along only while the player is actually queued.
void PetBattleMgr::SendQueueStatus(Player* player, uint32 status, PvPQueueEntry const* entry) const
{
    WorldPackets::BattlePet::PetBattleQueueStatus packet;
    packet.Status = status;
    packet.Ticket.RequesterGuid = player->GetGUID();
    packet.Ticket.Type = WorldPackets::LFG::RideType::PetBattle;
    if (entry)
    {
        packet.Ticket.Id = entry->TicketId;
        packet.Ticket.Time = entry->EnqueueTime;
        packet.ClientWaitTime = uint64(std::max<time_t>(0, GameTime::GetGameTime() - entry->EnqueueTime));
        packet.AvgWaitTime = PET_BATTLE_QUEUE_AVG_WAIT_SECS;
    }
    else
        packet.Ticket.Time = GameTime::GetGameTime();

    player->SendDirectMessage(packet.Write());
}

void PetBattleMgr::JoinQueue(ObjectGuid playerGUID)
{
    // Don't add if already in queue
    for (auto const& entry : _pvpQueue)
    {
        if (entry.PlayerGUID == playerGUID)
        {
            if (Player* player = ObjectAccessor::FindPlayer(playerGUID))
            {
                SendQueueStatus(player, PET_BATTLE_QUEUE_STATUS_ALREADY_QUEUED);
            }
            return;
        }
    }

    // Don't add if already in a battle
    if (IsPlayerInBattle(playerGUID))
    {
        if (Player* player = ObjectAccessor::FindPlayer(playerGUID))
        {
            SendQueueStatus(player, PET_BATTLE_QUEUE_STATUS_JOIN_FAILED);
        }
        return;
    }

    // Check player has alive slotted pets and journal lock
    if (Player* player = ObjectAccessor::FindPlayer(playerGUID))
    {
        BattlePets::BattlePetMgr* petMgr = player->GetSession()->GetBattlePetMgr();
        if (!petMgr->HasJournalLock())
        {
            SendQueueStatus(player, PET_BATTLE_QUEUE_STATUS_JOIN_FAILED_JOURNAL_LOCK);
            return;
        }

        bool hasPet = false;
        for (uint8 i = 0; i < uint8(BattlePets::BattlePetSlot::Count); ++i)
        {
            WorldPackets::BattlePet::BattlePetSlot* slot = petMgr->GetSlot(BattlePets::BattlePetSlot(i));
            if (slot && !slot->Locked && !slot->Pet.Guid.IsEmpty())
            {
                BattlePets::BattlePet* pet = petMgr->GetPet(slot->Pet.Guid);
                if (pet && pet->PacketInfo.Health > 0)
                {
                    hasPet = true;
                    break;
                }
            }
        }

        if (!hasPet)
        {
            SendQueueStatus(player, PET_BATTLE_QUEUE_STATUS_JOIN_FAILED_SLOTS);
            return;
        }
    }

    // A queued match in retail puts each player in the battle where they stand, with the opponent
    // far away. That is not built yet, so an eligible player is refused here rather than queued
    // into a match that would have to move the two players to each other.
    if (Player* player = ObjectAccessor::FindPlayer(playerGUID))
        SendQueueStatus(player, PET_BATTLE_QUEUE_STATUS_JOIN_FAILED);
}

void PetBattleMgr::LeaveQueue(ObjectGuid playerGUID)
{
    RemovePlayerFromQueue(playerGUID);

    if (Player* player = ObjectAccessor::FindPlayer(playerGUID))
    {
        SendQueueStatus(player, PET_BATTLE_QUEUE_STATUS_REMOVED);
    }
}

void PetBattleMgr::RemovePlayerFromQueue(ObjectGuid playerGUID)
{
    auto it = std::find_if(_pvpQueue.begin(), _pvpQueue.end(),
        [playerGUID](PvPQueueEntry const& entry) { return entry.PlayerGUID == playerGUID; });

    if (it != _pvpQueue.end())
        _pvpQueue.erase(it);

    // Cancel pending proposal if this player was in one
    if (_pendingProposal)
    {
        if (_pendingProposal->Player1 == playerGUID || _pendingProposal->Player2 == playerGUID)
        {
            ObjectGuid otherGUID = (_pendingProposal->Player1 == playerGUID) ?
                _pendingProposal->Player2 : _pendingProposal->Player1;

            // Back into the queue for the other player
            if (Player* other = ObjectAccessor::FindPlayer(otherGUID))
            {
                SendQueueStatus(other, PET_BATTLE_QUEUE_STATUS_MATCH_OPPONENT_DECLINED);
                Enqueue(otherGUID);
            }

            _pendingProposal.reset();
        }
    }
}

void PetBattleMgr::HandleProposalResult(ObjectGuid playerGUID, bool accepted)
{
    if (!_pendingProposal)
        return;

    if (_pendingProposal->Player1 == playerGUID)
        _pendingProposal->Player1Accepted = accepted;
    else if (_pendingProposal->Player2 == playerGUID)
        _pendingProposal->Player2Accepted = accepted;
    else
        return;

    if (!accepted)
    {
        // Send declined status to the declining player
        if (Player* player = ObjectAccessor::FindPlayer(playerGUID))
        {
            SendQueueStatus(player, PET_BATTLE_QUEUE_STATUS_MATCH_DECLINED);
        }

        // Notify the other player and re-queue them
        ObjectGuid otherGUID = (_pendingProposal->Player1 == playerGUID) ?
            _pendingProposal->Player2 : _pendingProposal->Player1;

        if (Player* other = ObjectAccessor::FindPlayer(otherGUID))
        {
            SendQueueStatus(other, PET_BATTLE_QUEUE_STATUS_MATCH_OPPONENT_DECLINED);
            Enqueue(otherGUID);
        }

        _pendingProposal.reset();
        return;
    }

    // Send accepted status to this player
    if (Player* player = ObjectAccessor::FindPlayer(playerGUID))
    {
        SendQueueStatus(player, PET_BATTLE_QUEUE_STATUS_MATCH_ACCEPTED);
    }

    // Check if both accepted
    // No queued battle is started yet: it has to open at each player's own location (see JoinQueue),
    // and JoinQueue refuses players until that exists, so no proposal can reach this point.
    if (_pendingProposal->Player1Accepted && _pendingProposal->Player2Accepted)
        _pendingProposal.reset();
}

void PetBattleMgr::TryMatchPlayers()
{
    if (_pendingProposal)
        return; // Already have a pending proposal

    if (_pvpQueue.size() < 2)
        return;

    // Match first two players in queue
    PvPQueueEntry p1 = _pvpQueue[0];
    PvPQueueEntry p2 = _pvpQueue[1];

    // Remove from queue
    _pvpQueue.erase(_pvpQueue.begin(), _pvpQueue.begin() + 2);

    // Validate both players still exist and aren't in battle
    Player* player1 = ObjectAccessor::FindPlayer(p1.PlayerGUID);
    Player* player2 = ObjectAccessor::FindPlayer(p2.PlayerGUID);

    if (!player1 || !player2 ||
        IsPlayerInBattle(p1.PlayerGUID) || IsPlayerInBattle(p2.PlayerGUID))
    {
        // Re-add valid players
        if (player1 && !IsPlayerInBattle(p1.PlayerGUID))
            _pvpQueue.push_back(p1);
        if (player2 && !IsPlayerInBattle(p2.PlayerGUID))
            _pvpQueue.push_back(p2);
        return;
    }

    // Create proposal
    _pendingProposal = std::make_unique<PvPMatchProposal>();
    _pendingProposal->Player1 = p1.PlayerGUID;
    _pendingProposal->Player2 = p2.PlayerGUID;
    _pendingProposal->ProposalTime = 0;

    // Send matchmaking then proposal status to both players
    for (Player* matchPlayer : { player1, player2 })
    {
        SendQueueStatus(matchPlayer, PET_BATTLE_QUEUE_STATUS_MATCHMAKING);
    }

    WorldPackets::BattlePet::PetBattleQueueProposeMatch proposeMatch;
    player1->SendDirectMessage(proposeMatch.Write());
    player2->SendDirectMessage(proposeMatch.Write());

    TC_LOG_DEBUG("server.loading", "PetBattleMgr: Proposed PvP match between {} and {}",
        p1.PlayerGUID.ToString(), p2.PlayerGUID.ToString());
}

} // namespace PetBattles
