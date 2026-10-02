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
#include "BattlePetMgr.h"
#include "BattlePetPackets.h"
#include "Creature.h"
#include "DB2Stores.h"
#include "GridDefines.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "PetBattleMgr.h"
#include "Player.h"
#include "TemporarySummon.h"

void WorldSession::HandleBattlePetRequestJournal(WorldPackets::BattlePet::BattlePetRequestJournal& /*battlePetRequestJournal*/)
{
    GetBattlePetMgr()->SendJournal();
}

void WorldSession::HandleBattlePetRequestJournalLock(WorldPackets::BattlePet::BattlePetRequestJournalLock& /*battlePetRequestJournalLock*/)
{
    // Retail 12.1.0.69587 (three captures): the client sends this whenever the journal UI opens;
    // a session that already holds the lock (SMSG_BATTLE_PET_JOURNAL_LOCK_ACQUIRED was pushed at
    // login) gets NO reply -- no lock packet and no 10 KB journal resend.
    if (GetBattlePetMgr()->HasJournalLock())
        return;

    GetBattlePetMgr()->SendJournalLockStatus();

    if (GetBattlePetMgr()->HasJournalLock())
        GetBattlePetMgr()->SendJournal();
}

void WorldSession::HandleBattlePetSetBattleSlot(WorldPackets::BattlePet::BattlePetSetBattleSlot& battlePetSetBattleSlot)
{
    BattlePets::BattlePetMgr* petMgr = GetBattlePetMgr();

    BattlePets::BattlePet* pet = petMgr->GetPet(battlePetSetBattleSlot.PetGuid);
    if (!pet)
        return;

    WorldPackets::BattlePet::BattlePetSlot* targetSlot = petMgr->GetSlot(BattlePets::BattlePetSlot(battlePetSetBattleSlot.Slot));
    if (!targetSlot || targetSlot->Locked)
        return;

    // Find which slot this pet currently occupies (if any)
    WorldPackets::BattlePet::BattlePetSlot* sourceSlot = nullptr;
    for (uint8 i = 0; i < uint8(BattlePets::BattlePetSlot::Count); ++i)
    {
        WorldPackets::BattlePet::BattlePetSlot* slot = petMgr->GetSlot(BattlePets::BattlePetSlot(i));
        if (slot && slot->Pet.Guid == battlePetSetBattleSlot.PetGuid)
        {
            sourceSlot = slot;
            break;
        }
    }

    if (sourceSlot == targetSlot)
        return; // Already in that slot

    // Swap: move whatever is in the target slot to the source slot
    if (sourceSlot)
    {
        sourceSlot->Pet = targetSlot->Pet;
    }

    // Place the pet in the target slot
    targetSlot->Pet = pet->PacketInfo;
}

void WorldSession::HandleBattlePetModifyName(WorldPackets::BattlePet::BattlePetModifyName& battlePetModifyName)
{
    GetBattlePetMgr()->ModifyName(battlePetModifyName.PetGuid, battlePetModifyName.Name, std::move(battlePetModifyName.DeclinedNames));
}

void WorldSession::HandleQueryBattlePetName(WorldPackets::BattlePet::QueryBattlePetName& queryBattlePetName)
{
    WorldPackets::BattlePet::QueryBattlePetNameResponse response;
    response.BattlePetID = queryBattlePetName.BattlePetID;

    Creature* summonedBattlePet = ObjectAccessor::GetCreatureOrPetOrVehicle(*_player, queryBattlePetName.UnitGUID);
    if (!summonedBattlePet || !summonedBattlePet->IsSummon())
    {
        SendPacket(response.Write());
        return;
    }

    response.CreatureID = summonedBattlePet->GetEntry();
    response.Timestamp = summonedBattlePet->GetBattlePetCompanionNameTimestamp();

    Unit* petOwner = summonedBattlePet->ToTempSummon()->GetSummonerUnit();
    if (!petOwner->IsPlayer())
    {
        SendPacket(response.Write());
        return;
    }

    BattlePets::BattlePet const* battlePet = petOwner->ToPlayer()->GetSession()->GetBattlePetMgr()->GetPet(queryBattlePetName.BattlePetID);
    if (!battlePet)
    {
        SendPacket(response.Write());
        return;
    }

    response.Name = battlePet->PacketInfo.Name;
    if (battlePet->DeclinedName)
    {
        response.HasDeclined = true;
        response.DeclinedNames = *battlePet->DeclinedName;
    }

    response.Allow = !response.Name.empty();

    SendPacket(response.Write());
}

void WorldSession::HandleBattlePetDeletePet(WorldPackets::BattlePet::BattlePetDeletePet& battlePetDeletePet)
{
    GetBattlePetMgr()->RemovePet(battlePetDeletePet.PetGuid);
}

void WorldSession::HandleBattlePetSetFlags(WorldPackets::BattlePet::BattlePetSetFlags& battlePetSetFlags)
{
    if (!GetBattlePetMgr()->HasJournalLock())
        return;

    if (BattlePets::BattlePet* pet = GetBattlePetMgr()->GetPet(battlePetSetFlags.PetGuid))
    {
        if (battlePetSetFlags.ControlType == BattlePets::FLAGS_CONTROL_TYPE_APPLY)
            pet->PacketInfo.Flags |= battlePetSetFlags.Flags;
        else // FLAGS_CONTROL_TYPE_REMOVE
            pet->PacketInfo.Flags &= ~battlePetSetFlags.Flags;

        if (pet->SaveInfo != BattlePets::BATTLE_PET_NEW)
            pet->SaveInfo = BattlePets::BATTLE_PET_CHANGED;
    }
}

void WorldSession::HandleBattlePetClearFanfare(WorldPackets::BattlePet::BattlePetClearFanfare& battlePetClearFanfare)
{
    GetBattlePetMgr()->ClearFanfare(battlePetClearFanfare.PetGuid);
}

void WorldSession::HandleCageBattlePet(WorldPackets::BattlePet::CageBattlePet& cageBattlePet)
{
    GetBattlePetMgr()->CageBattlePet(cageBattlePet.PetGuid);
}

void WorldSession::HandleBattlePetSummon(WorldPackets::BattlePet::BattlePetSummon& battlePetSummon)
{
    if (*_player->m_activePlayerData->SummonedBattlePetGUID != battlePetSummon.PetGuid)
        GetBattlePetMgr()->SummonPet(battlePetSummon.PetGuid);
    else
        GetBattlePetMgr()->DismissPet();
}

void WorldSession::HandleBattlePetUpdateNotify(WorldPackets::BattlePet::BattlePetUpdateNotify& battlePetUpdateNotify)
{
    GetBattlePetMgr()->UpdateBattlePetData(battlePetUpdateNotify.PetGuid);
}


// ============================================================================
// Pet Battle Combat Handlers
// ============================================================================

static WorldPackets::BattlePet::PetBattlePetUpdateInfo BuildPetUpdateInfo(
    PetBattles::PetBattlePetData const& petData, uint8 teamIdx, uint8 petIdx);

static void BuildPetBattlePlayerUpdate(WorldPackets::BattlePet::PetBattlePlayerUpdateInfo& update,
    PetBattles::PetBattleTeamData const& team, bool isWildTeam, PetBattles::PetBattle const* battle, uint8 teamIdx)
{
    update.CharacterGUID = team.PlayerGUID;
    update.TrapAbilityID = team.TrapAbilityID;
    update.TrapStatus = battle ? battle->GetTrapStatus(teamIdx) : team.TrapStatus;
    update.RoundTimeSecs = 0; // Initial update uses 0; round-level timer is in PvpMaxRoundTime
    update.FrontPet = team.FrontPetIndex;
    update.InputFlags = team.InputFlags;

    for (uint8 i = 0; i < team.PetCount; ++i)
    {
        WorldPackets::BattlePet::PetBattlePetUpdateInfo petInfo = BuildPetUpdateInfo(team.Pets[i], teamIdx, i);
        if (isWildTeam)
            petInfo.DisplayID = 0;
        update.Pets.push_back(std::move(petInfo));
    }
}

static void BuildPetBattleEnviros(std::array<WorldPackets::BattlePet::PetBattleEnviroInfo, 3>& enviros,
    PetBattles::PetBattle const* battle)
{
    for (uint8 i = 0; i < PetBattles::MAX_PET_BATTLE_ENVIRONMENTS; ++i)
    {
        PetBattles::PetBattleEnvironment const& env = battle->GetEnvironment(i);
        if (env.IsActive())
        {
            WorldPackets::BattlePet::PetBattleAuraInfo auraInfo;
            auraInfo.AbilityID = env.AbilityID;
            auraInfo.InstanceID = env.AuraInstanceID;
            auraInfo.RoundsRemaining = env.RemainingRounds;
            auraInfo.CurrentRound = env.CurrentRound;
            auraInfo.CasterPBOID = env.CasterTeam * PetBattles::MAX_PET_BATTLE_TEAM_SIZE
                + battle->GetTeam(env.CasterTeam).FrontPetIndex;
            enviros[i].Auras.push_back(auraInfo);

            // State values drive the client-side visual weather (Mod_HealingDealt, Add_FlatDamage, etc.)
            for (auto const& [stateID, stateValue] : env.States)
            {
                WorldPackets::BattlePet::PetBattleStateInfo stateInfo;
                stateInfo.StateID = stateID;
                stateInfo.StateValue = stateValue;
                enviros[i].States.push_back(stateInfo);
            }
        }
    }
}

// Retail FirstRound includes a PET_SWAP effect per team to position the active pet at the
// front. Without these, the client never animates the active pet into the combat slot.
static void EmitInitialFrontPetSwapEffects(std::vector<WorldPackets::BattlePet::PetBattleEffectInfo>& effects,
    PetBattles::PetBattle const* battle)
{
    for (uint8 t = 0; t < PetBattles::MAX_PET_BATTLE_PLAYERS; ++t)
    {
        PetBattles::PetBattleTeamData const& team = battle->GetTeam(t);
        int32 frontPBOID = static_cast<int32>(t * PetBattles::MAX_PET_BATTLE_TEAM_SIZE + team.FrontPetIndex);

        WorldPackets::BattlePet::PetBattleEffectInfo swapEffect;
        swapEffect.PetBattleEffectType = PetBattles::PET_BATTLE_EFFECT_PET_SWAP;
        swapEffect.CasterPBOID = frontPBOID;

        WorldPackets::BattlePet::PetBattleEffectTargetInfo swapTarget;
        swapTarget.Type = 0;
        swapTarget.Petx = frontPBOID;
        swapEffect.Targets.push_back(std::move(swapTarget));

        effects.push_back(std::move(swapEffect));
    }
}

static void BuildPetBattleRoundPlayerData(WorldPackets::BattlePet::PetBattleRoundPlayerData& roundData,
    PetBattles::PetBattleTeamData const& team, PetBattles::PetBattle const* battle, uint8 teamIdx)
{
    roundData.NextInputFlags = team.InputFlags;
    roundData.NextTrapStatus = battle ? static_cast<int8>(battle->GetTrapStatus(teamIdx)) : static_cast<int8>(team.TrapStatus);
    // RoundTimeSecs is 0 for PvE/NPC battles; only PvP uses a timer
    roundData.RoundTimeSecs = (battle && (battle->GetBattleType() == PetBattles::PET_BATTLE_TYPE_PVP || battle->GetBattleType() == PetBattles::PET_BATTLE_TYPE_LFPB))
        ? PetBattles::PET_BATTLE_MAX_ROUND_TIME : 0;
}

static void BuildRoundCooldowns(std::vector<WorldPackets::BattlePet::PetBattleCooldownInfo>& cooldowns,
    PetBattles::PetBattle const* battle)
{
    for (uint8 t = 0; t < PetBattles::MAX_PET_BATTLE_PLAYERS; ++t)
    {
        PetBattles::PetBattleTeamData const& team = battle->GetTeam(t);
        for (uint8 i = 0; i < team.PetCount; ++i)
        {
            for (uint8 j = 0; j < PetBattles::MAX_PET_BATTLE_ABILITIES; ++j)
            {
                if (team.Pets[i].AbilityIDs[j] == 0)
                    continue;

                WorldPackets::BattlePet::PetBattleCooldownInfo cd;
                cd.AbilityID = team.Pets[i].AbilityIDs[j];
                cd.CooldownRemaining = team.Pets[i].AbilityCooldowns[j];
                cd.LockdownRemaining = team.Pets[i].AbilityLockdowns[j];
                cd.AbilityIndex = static_cast<int8>(j);
                cd.Pboid = t * PetBattles::MAX_PET_BATTLE_TEAM_SIZE + i;
                cooldowns.push_back(cd);
            }
        }
    }
}

static void BuildPetXDied(std::vector<int8>& petXDied, PetBattles::PetBattle const* battle)
{
    for (uint8 t = 0; t < PetBattles::MAX_PET_BATTLE_PLAYERS; ++t)
    {
        PetBattles::PetBattleTeamData const& team = battle->GetTeam(t);
        for (uint8 i = 0; i < team.PetCount; ++i)
        {
            if (battle->WasPetKilledThisRound(t, i))
                petXDied.push_back(static_cast<int8>(t * PetBattles::MAX_PET_BATTLE_TEAM_SIZE + i));
        }
    }
}

static WorldPackets::BattlePet::PetBattlePetUpdateInfo BuildPetUpdateInfo(
    PetBattles::PetBattlePetData const& petData, uint8 teamIdx, uint8 petIdx)
{
    WorldPackets::BattlePet::PetBattlePetUpdateInfo petInfo;
    petInfo.BattlePetGUID = petData.BattlePetGUID;
    petInfo.SpeciesID = petData.Species;
    petInfo.DisplayID = petData.DisplayID;
    petInfo.Level = petData.Level;
    petInfo.Xp = petData.Xp;
    petInfo.CurHealth = petData.Health;
    petInfo.MaxHealth = petData.MaxHealth;
    petInfo.Power = petData.Power;
    petInfo.Speed = petData.Speed;
    petInfo.NpcTeamMemberID = petData.NpcTeamMemberID;
    petInfo.BreedQuality = petData.Quality;
    petInfo.Slot = static_cast<int8>(petIdx);
    petInfo.CustomName = petData.CustomName;

    uint16 statusFlags = 0;
    if (petData.IsCaptured)
        statusFlags |= PetBattles::PET_BATTLE_PET_STATUS_TRAPPED;
    if (petData.IsStunned)
        statusFlags |= PetBattles::PET_BATTLE_PET_STATUS_STUNNED;
    if (petData.IsLockedByMultiTurn)
        statusFlags |= PetBattles::PET_BATTLE_PET_STATUS_SWAP_OUT_LOCKED;
    petInfo.StatusFlags = statusFlags;

    for (uint8 j = 0; j < PetBattles::MAX_PET_BATTLE_ABILITIES; ++j)
    {
        if (petData.AbilityIDs[j] == 0)
            continue;

        WorldPackets::BattlePet::PetBattleAbilityInfo ability;
        ability.AbilityID = petData.AbilityIDs[j];
        ability.CooldownRemaining = petData.AbilityCooldowns[j];
        ability.LockdownRemaining = petData.AbilityLockdowns[j];
        ability.AbilityIndex = static_cast<int8>(j);
        ability.Pboid = teamIdx * PetBattles::MAX_PET_BATTLE_TEAM_SIZE + petIdx;
        petInfo.Abilities.push_back(ability);
    }

    for (PetBattles::PetBattleAura const& aura : petData.Auras)
    {
        WorldPackets::BattlePet::PetBattleAuraInfo auraInfo;
        auraInfo.AbilityID = aura.AbilityID;
        auraInfo.InstanceID = aura.AuraInstanceID;
        auraInfo.RoundsRemaining = aura.RemainingRounds;
        auraInfo.CurrentRound = aura.CurrentRound;
        auraInfo.CasterPBOID = aura.CasterTeam * PetBattles::MAX_PET_BATTLE_TEAM_SIZE + aura.CasterPet;
        petInfo.Auras.push_back(auraInfo);
    }

    petInfo.States.push_back({ BattlePets::STATE_STAT_POWER, petData.Power }); // wire: final power, not the raw base
    petInfo.States.push_back({ BattlePets::STATE_STAT_STAMINA, petData.BaseStamina });
    petInfo.States.push_back({ BattlePets::STATE_STAT_SPEED, petData.BaseSpeed });
    petInfo.States.push_back({ 40, 5 }); // CritChance = 5%
    if (petData.PetType >= 0 && petData.PetType < PetBattles::PET_TYPE_COUNT)
    {
        // BattlePetState passive ids (Passive_Critter 42 .. Passive_Aquatic 51) are not in PetType order
        static constexpr uint32 passiveStateByPetType[PetBattles::PET_TYPE_COUNT] =
        {
            44, // Humanoid
            46, // Dragonkin
            45, // Flying
            50, // Undead
            42, // Critter
            49, // Magic
            47, // Elemental
            43, // Beast
            51, // Aquatic
            48  // Mechanical
        };
        petInfo.States.push_back({ passiveStateByPetType[petData.PetType], 1 });
    }

    return petInfo;
}

static void BuildRoundEffects(std::vector<WorldPackets::BattlePet::PetBattleEffectInfo>& effectList,
    PetBattles::PetBattle const* battle)
{
    for (PetBattles::PetBattleRoundEffect const& roundEffect : battle->GetRoundEffects())
    {
        WorldPackets::BattlePet::PetBattleEffectInfo effect;
        effect.AbilityEffectID = roundEffect.AbilityEffectID;
        effect.Flags = roundEffect.Flags;
        effect.SourceAuraInstanceID = 0;
        effect.TurnInstanceID = 0;
        // Wire offset 12 is the PetBattleEffectType — client switches on this to process effects
        // (SetHealth=0, AuraApply=1, PetSwap=4, SetState=6, etc.), NOT a sequential index
        effect.PetBattleEffectType = roundEffect.EffectType;
        if (roundEffect.SourceEnvSlot >= 0)
            effect.CasterPBOID = static_cast<int32>(PetBattles::PBOID_ENVIRONMENT_BASE + roundEffect.SourceEnvSlot);
        else
            effect.CasterPBOID = static_cast<int32>(roundEffect.SourceTeam * PetBattles::MAX_PET_BATTLE_TEAM_SIZE + roundEffect.SourcePet);
        effect.StackDepth = 0;

        WorldPackets::BattlePet::PetBattleEffectTargetInfo target;
        // Environment targets use PBOID_ENVIRONMENT_BASE + slot; pet targets use team * TEAM_SIZE + pet
        if (roundEffect.TargetEnvSlot >= 0)
            target.Petx = static_cast<int32>(PetBattles::PBOID_ENVIRONMENT_BASE + roundEffect.TargetEnvSlot);
        else
            target.Petx = static_cast<int32>(roundEffect.TargetTeam * PetBattles::MAX_PET_BATTLE_TEAM_SIZE + roundEffect.TargetPet);

        // Map effect type to target type and variable-length params
        // Target types: 0=none, 1=aura(4 i32), 2=state(2 i32), 3=health(1 i32),
        //               4=stat(1 i32), 5=trigger(1 i32), 6=cooldown(3 i32), 7=broadcast(1 i32)
        switch (roundEffect.EffectType)
        {
            case PetBattles::PET_BATTLE_EFFECT_SET_HEALTH:
            case PetBattles::PET_BATTLE_EFFECT_SET_MAX_HEALTH:
                target.Type = 3; // Health
                target.Params.push_back(roundEffect.Param1);
                break;
            case PetBattles::PET_BATTLE_EFFECT_AURA_APPLY:
            case PetBattles::PET_BATTLE_EFFECT_AURA_CANCEL:
            case PetBattles::PET_BATTLE_EFFECT_AURA_CHANGE:
            {
                // 12.1.0.69587 wire order (three captures, WPP ReadPetBattleEffectTarget agrees):
                // [AuraInstanceID, AuraAbilityID, RoundsRemaining, CurrentRound]
                target.Type = 1; // Aura: 4 params
                target.Params.push_back(roundEffect.Param1); // AuraInstanceID
                target.Params.push_back(roundEffect.Param2); // AuraAbilityID
                target.Params.push_back(roundEffect.Param3); // RoundsRemaining
                target.Params.push_back(roundEffect.Param4); // CurrentRound
                break;
            }
            case PetBattles::PET_BATTLE_EFFECT_SET_STATE:
                target.Type = 2; // State
                target.Params.push_back(roundEffect.Param1); // StateID
                target.Params.push_back(roundEffect.Param2); // StateValue
                break;
            case PetBattles::PET_BATTLE_EFFECT_STATUS_CHANGE:
            case PetBattles::PET_BATTLE_EFFECT_SET_SPEED:
            case PetBattles::PET_BATTLE_EFFECT_SET_POWER:
                target.Type = 4; // NewStatValue
                target.Params.push_back(roundEffect.Param1);
                break;
            case PetBattles::PET_BATTLE_EFFECT_TRIGGER_ABILITY:
                target.Type = 5; // TriggerAbilityID
                target.Params.push_back(roundEffect.Param1);
                break;
            case PetBattles::PET_BATTLE_EFFECT_ABILITY_CHANGE:
                target.Type = 6; // Cooldown
                target.Params.push_back(roundEffect.Param1); // ChangedAbilityID
                target.Params.push_back(roundEffect.Param2); // CooldownRemaining
                target.Params.push_back(0);                   // LockdownRemaining
                break;
            case PetBattles::PET_BATTLE_EFFECT_NPC_EMOTE:
                target.Type = 7; // BroadcastTextID
                target.Params.push_back(roundEffect.Param1);
                break;
            case PetBattles::PET_BATTLE_EFFECT_AURA_PROCESSING_BEGIN:
            case PetBattles::PET_BATTLE_EFFECT_AURA_PROCESSING_END:
                target.Type = 0; // No data — sentinel markers with PBOID 9
                break;
            case PetBattles::PET_BATTLE_EFFECT_REPLACE_PET:
            {
                // Target type 8: embedded PetBattlePetUpdateInfo (full pet data refresh)
                target.Type = 8;
                PetBattles::PetBattleTeamData const& targetTeam = battle->GetTeam(roundEffect.TargetTeam);
                if (roundEffect.TargetPet < targetTeam.PetCount)
                    target.EmbeddedPetUpdate = BuildPetUpdateInfo(targetTeam.Pets[roundEffect.TargetPet],
                        roundEffect.TargetTeam, roundEffect.TargetPet);
                break;
            }
            default:
                target.Type = 0; // No data
                break;
        }

        effect.Targets.push_back(target);
        effectList.push_back(std::move(effect));
    }
}

static void SendPetBattleRequestFailed(Player* player, PetBattles::PetBattleRequestFailReason reason)
{
    WorldPackets::BattlePet::PetBattleRequestFailed failed;
    failed.Reason = reason;
    player->SendDirectMessage(failed.Write());
}

static bool HasPetReadyToBattle(BattlePets::BattlePetMgr* petMgr)
{
    for (uint8 i = 0; i < uint8(BattlePets::BattlePetSlot::Count); ++i)
    {
        WorldPackets::BattlePet::BattlePetSlot* slot = petMgr->GetSlot(BattlePets::BattlePetSlot(i));
        if (!slot || slot->Locked || slot->Pet.Guid.IsEmpty())
            continue;

        if (BattlePets::BattlePet* pet = petMgr->GetPet(slot->Pet.Guid))
            if (pet->PacketInfo.Health > 0)
                return true;
    }
    return false;
}

// What stops this player from starting a pet battle now, or PET_BATTLE_REQUEST_FAIL_OK.
static PetBattles::PetBattleRequestFailReason CheckCanStartPetBattle(Player* player)
{
    BattlePets::BattlePetMgr* petMgr = player->GetSession()->GetBattlePetMgr();
    if (!petMgr->IsBattlePetSystemEnabled())
        return PetBattles::PET_BATTLE_REQUEST_FAIL_INVALID_LOADOUT_NONE_SLOTTED;
    if (!player->IsAlive())
        return PetBattles::PET_BATTLE_REQUEST_FAIL_NOT_WHILE_DEAD;
    if (player->IsInCombat())
        return PetBattles::PET_BATTLE_REQUEST_FAIL_NOT_WHILE_IN_COMBAT;
    if (player->IsFlying() || player->IsInFlight())
        return PetBattles::PET_BATTLE_REQUEST_FAIL_NOT_WHILE_FLYING;
    if (player->GetTransport() || player->GetVehicle())
        return PetBattles::PET_BATTLE_REQUEST_FAIL_NOT_HERE_ON_TRANSPORT;
    if (sPetBattleMgr->IsPlayerInBattle(player->GetGUID()))
        return PetBattles::PET_BATTLE_REQUEST_FAIL_IN_BATTLE;
    if (!petMgr->HasJournalLock())
        return PetBattles::PET_BATTLE_REQUEST_FAIL_NO_JOURNAL_LOCK;
    if (!HasPetReadyToBattle(petMgr))
        return PetBattles::PET_BATTLE_REQUEST_FAIL_INVALID_LOADOUT_ALL_DEAD;
    return PetBattles::PET_BATTLE_REQUEST_FAIL_OK;
}

// The client picks the battle spot and the two standing spots and sends them with its request.
// They are only accepted close to the player who sent them.
static bool IsValidPetBattleLocation(Player const* player, WorldPackets::BattlePet::PetBattleLocation const& location)
{
    if (!std::isfinite(location.BattleFacing))
        return false;

    std::array<Position const*, 3> const spots = { &location.BattleOrigin, &location.PlayerPositions[0], &location.PlayerPositions[1] };
    for (Position const* spot : spots)
    {
        if (!Trinity::IsValidMapCoord(spot->GetPositionX(), spot->GetPositionY(), spot->GetPositionZ()))
            return false;
        if (player->GetExactDist(spot) > PetBattles::PET_BATTLE_LOCATION_MAX_OFFSET)
            return false;
    }
    return true;
}

// FINALIZE_LOCATION, INITIAL_UPDATE and FIRST_ROUND to every player in the battle, which starts it.
// Nobody is moved by the server: the client walks its own character to the spot it was given.
static void SendPetBattleOpening(PetBattles::PetBattle* battle, WorldPackets::BattlePet::PetBattleLocation location,
    uint32 npcCreatureID, uint32 npcDisplayID)
{
    bool const isPvp = battle->GetBattleType() == PetBattles::PET_BATTLE_TYPE_PVP || battle->GetBattleType() == PetBattles::PET_BATTLE_TYPE_LFPB;
    bool const isWild = battle->GetBattleType() == PetBattles::PET_BATTLE_TYPE_PVE;

    WorldPackets::BattlePet::PetBattleFinalizeLocation finalizeLocation;
    location.LocationResult = PetBattles::PET_BATTLE_REQUEST_FAIL_OK;
    finalizeLocation.Location = location;
    finalizeLocation.Write();

    WorldPackets::BattlePet::PetBattleInitialUpdate initialUpdate;
    initialUpdate.SceneObjectGUID = battle->GetSceneObjectGUID();
    BuildPetBattlePlayerUpdate(initialUpdate.Players[0], battle->GetTeam(PetBattles::PET_BATTLE_TEAM_1), false, battle, PetBattles::PET_BATTLE_TEAM_1);
    BuildPetBattlePlayerUpdate(initialUpdate.Players[1], battle->GetTeam(PetBattles::PET_BATTLE_TEAM_2), isWild, battle, PetBattles::PET_BATTLE_TEAM_2);
    BuildPetBattleEnviros(initialUpdate.Enviros, battle);
    initialUpdate.CurRound = battle->GetCurrentRound();
    initialUpdate.CurPetBattleState = static_cast<int8>(battle->GetBattleState());
    initialUpdate.NpcCreatureID = npcCreatureID;
    initialUpdate.NpcDisplayID = npcDisplayID;
    initialUpdate.InitialWildPetGUID = battle->GetWildCreatureGUID();
    initialUpdate.IsPVP = isPvp;
    initialUpdate.CanAwardXP = battle->CanAwardXP();
    initialUpdate.ForfeitPenalty = isPvp ? 10 : 0;
    initialUpdate.WaitingForFrontPetsMaxSecs = 30;
    initialUpdate.PvpMaxRoundTime = PetBattles::PET_BATTLE_MAX_ROUND_TIME;
    initialUpdate.Write();

    for (uint8 t = 0; t < PetBattles::MAX_PET_BATTLE_PLAYERS; ++t)
    {
        if (Player* player = battle->GetPlayerForTeam(t))
        {
            player->SendDirectMessage(finalizeLocation.GetRawPacket());
            player->SendDirectMessage(initialUpdate.GetRawPacket());
        }
    }

    battle->Start();

    // Retail sends FIRST_ROUND about a second later, after the intro; it unlocks the ability bar.
    WorldPackets::BattlePet::PetBattleFirstRound firstRound;
    firstRound.SceneObjectGUID = battle->GetSceneObjectGUID();
    firstRound.CurRound = 0;
    firstRound.NextPetBattleState = static_cast<int8>(PetBattles::PET_BATTLE_STATE_ROUND_IN_PROGRESS);
    for (uint8 i = 0; i < PetBattles::MAX_PET_BATTLE_PLAYERS; ++i)
        BuildPetBattleRoundPlayerData(firstRound.Players[i], battle->GetTeam(i), battle, i);
    EmitInitialFrontPetSwapEffects(firstRound.Effects, battle);
    firstRound.Write();

    for (uint8 t = 0; t < PetBattles::MAX_PET_BATTLE_PLAYERS; ++t)
        if (Player* player = battle->GetPlayerForTeam(t))
            player->SendDirectMessage(firstRound.GetRawPacket());
}

void WorldSession::HandlePetBattleRequestWild(WorldPackets::BattlePet::PetBattleRequestWild& petBattleRequestWild)
{
    Player* player = GetPlayer();

    PetBattles::PetBattleRequestFailReason reason = CheckCanStartPetBattle(player);
    if (reason != PetBattles::PET_BATTLE_REQUEST_FAIL_OK)
    {
        SendPetBattleRequestFailed(player, reason);
        return;
    }

    Creature* creature = ObjectAccessor::GetCreature(*player, petBattleRequestWild.TargetGUID);
    if (!creature || !creature->IsWildBattlePet() || !creature->IsAlive()
        || !BattlePets::BattlePetMgr::GetBattlePetSpeciesByCreature(creature->GetEntry()))
    {
        SendPetBattleRequestFailed(player, PetBattles::PET_BATTLE_REQUEST_FAIL_TARGET_INVALID);
        return;
    }

    if (!player->IsWithinDistInMap(creature, PetBattles::PET_BATTLE_LOCATION_MAX_OFFSET) || !player->IsWithinLOSInMap(creature))
    {
        SendPetBattleRequestFailed(player, PetBattles::PET_BATTLE_REQUEST_FAIL_TARGET_OUT_OF_RANGE);
        return;
    }

    // Someone else is already fighting this pet.
    if (sPetBattleMgr->IsCreatureInBattle(creature->GetGUID()))
    {
        SendPetBattleRequestFailed(player, PetBattles::PET_BATTLE_REQUEST_FAIL_WILD_PET_TAPPED);
        return;
    }

    if (!IsValidPetBattleLocation(player, petBattleRequestWild.Location))
    {
        SendPetBattleRequestFailed(player, PetBattles::PET_BATTLE_REQUEST_FAIL_NOT_HERE);
        return;
    }

    PetBattles::PetBattle* battle = sPetBattleMgr->CreateWildBattle(player, creature->GetGUID());
    if (!battle)
    {
        SendPetBattleRequestFailed(player, PetBattles::PET_BATTLE_REQUEST_FAIL_IN_BATTLE);
        return;
    }

    // The wild pet stops where it is and cannot be attacked until the battle ends.
    creature->SetUnitFlag(UNIT_FLAG_PACIFIED);
    creature->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
    creature->GetMotionMaster()->MoveIdle();

    SendPetBattleOpening(battle, petBattleRequestWild.Location, 0, 0);
}

void WorldSession::StartNPCPetBattle(Creature* trainer)
{
    Player* player = GetPlayer();
    if (!trainer)
        return;

    PetBattles::PetBattleRequestFailReason reason = CheckCanStartPetBattle(player);
    if (reason != PetBattles::PET_BATTLE_REQUEST_FAIL_OK)
    {
        SendPetBattleRequestFailed(player, reason);
        return;
    }

    if (!sPetBattleMgr->GetNPCTeam(trainer->GetEntry()))
    {
        SendPetBattleRequestFailed(player, PetBattles::PET_BATTLE_REQUEST_FAIL_NOT_A_TRAINER);
        return;
    }

    PetBattles::PetBattle* battle = sPetBattleMgr->CreateNPCBattle(player, trainer);
    if (!battle)
    {
        SendPetBattleRequestFailed(player, PetBattles::PET_BATTLE_REQUEST_FAIL_IN_BATTLE);
        return;
    }

    // A tamer battle starts from gossip, so there is no client location. The battle takes place
    // between the player and the tamer where they stand; the tamer is shared and is not moved.
    WorldPackets::BattlePet::PetBattleLocation location;
    location.BattleOrigin.Relocate((player->GetPositionX() + trainer->GetPositionX()) / 2.0f,
        (player->GetPositionY() + trainer->GetPositionY()) / 2.0f,
        (player->GetPositionZ() + trainer->GetPositionZ()) / 2.0f);
    location.BattleFacing = player->GetAbsoluteAngle(trainer);
    location.PlayerPositions[0].Relocate(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
    location.PlayerPositions[1].Relocate(trainer->GetPositionX(), trainer->GetPositionY(), trainer->GetPositionZ());

    SendPetBattleOpening(battle, location, trainer->GetEntry(), trainer->GetDisplayId());
}

static void SendRoundResult(PetBattles::PetBattle* battle)
{
    WorldPackets::BattlePet::PetBattleRoundResult roundResult;
    roundResult.SceneObjectGUID = battle->GetSceneObjectGUID();
    roundResult.CurRound = battle->GetCurrentRound();
    roundResult.NextPetBattleState = static_cast<int8>(battle->GetBattleState());
    for (uint8 i = 0; i < PetBattles::MAX_PET_BATTLE_PLAYERS; ++i)
        BuildPetBattleRoundPlayerData(roundResult.Players[i], battle->GetTeam(i), battle, i);
    BuildRoundEffects(roundResult.Effects, battle);
    BuildRoundCooldowns(roundResult.Cooldowns, battle);
    BuildPetXDied(roundResult.PetXDied, battle);
    roundResult.Write();

    for (uint8 t = 0; t < PetBattles::MAX_PET_BATTLE_PLAYERS; ++t)
        if (Player* player = battle->GetPlayerForTeam(t))
            player->SendDirectMessage(roundResult.GetRawPacket());
}

// Quitting ends the battle at once, whether it comes as CMSG_PET_BATTLE_QUIT_NOTIFY or as a quit move.
static void QuitPetBattle(Player* player, PetBattles::PetBattle* battle)
{
    int8 teamIdx = battle->GetTeamIndex(player->GetGUID());
    if (teamIdx < 0 || battle->IsFinished())
        return;

    uint32 battleID = battle->GetBattleID();
    bool const finalRoundSent = battle->IsFinalRound() && !battle->HasPendingFinishDelay();
    if (!battle->IsFinalRound())
        battle->Forfeit(uint8(teamIdx));

    if (!finalRoundSent)
        battle->SendFinalRoundPacket(true);
    battle->CompleteBattle();
    sPetBattleMgr->RemoveBattle(battleID);
}

void WorldSession::HandlePetBattleInput(WorldPackets::BattlePet::PetBattleInput& petBattleInput)
{
    Player* player = GetPlayer();

    PetBattles::PetBattle* battle = sPetBattleMgr->GetBattleByPlayer(player->GetGUID());
    if (!battle)
        return;

    int8 teamIdx = battle->GetTeamIndex(player->GetGUID());
    if (teamIdx < 0)
        return;

    if (petBattleInput.MoveType == PetBattles::PET_BATTLE_MOVE_QUIT)
    {
        QuitPetBattle(player, battle);
        return;
    }

    // Only accept input during active round — not during pet replacement, final round, or finished
    if (battle->GetBattleState() != PetBattles::PET_BATTLE_STATE_ROUND_IN_PROGRESS)
        return;

    if (petBattleInput.MoveType < 0 || petBattleInput.MoveType > static_cast<int32>(PetBattles::PET_BATTLE_MOVE_PASS))
        return;

    // One move per team and round.
    if (battle->GetTeam(teamIdx).HasInputThisRound)
        return;

    if (!battle->SubmitInput(uint8(teamIdx), PetBattles::PetBattleMoveType(petBattleInput.MoveType),
        uint32(petBattleInput.AbilityID), petBattleInput.NewFrontPetIndex))
        return;

    // The wild pet or the tamer moves once the player has committed. If the move it picked is
    // not allowed this round, it passes, so the round never waits on the computer.
    if (battle->GetBattleType() == PetBattles::PET_BATTLE_TYPE_PVE || battle->GetBattleType() == PetBattles::PET_BATTLE_TYPE_NPC)
    {
        if (battle->GetBattleType() == PetBattles::PET_BATTLE_TYPE_PVE)
            battle->GenerateWildTeamInput();
        else
            battle->GenerateNPCTeamInput();

        if (!battle->GetTeam(PetBattles::PET_BATTLE_TEAM_2).HasInputThisRound)
            battle->SubmitInput(PetBattles::PET_BATTLE_TEAM_2, PetBattles::PET_BATTLE_MOVE_PASS, 0, -1);
    }

    if (battle->BothTeamsReady())
    {
        battle->ProcessRound();

        // The round result always goes out so the client plays the round; FinalRound follows
        // from PetBattle::Update after the death or capture animation.
        SendRoundResult(battle);
    }
}

void WorldSession::HandlePetBattleReplaceFrontPet(WorldPackets::BattlePet::PetBattleReplaceFrontPet& petBattleReplaceFrontPet)
{
    Player* player = GetPlayer();

    PetBattles::PetBattle* battle = sPetBattleMgr->GetBattleByPlayer(player->GetGUID());
    if (!battle)
        return;

    int8 teamIdx = battle->GetTeamIndex(player->GetGUID());
    int8 newPetIdx = petBattleReplaceFrontPet.FrontPetIndex;
    if (teamIdx < 0 || !battle->CanReplaceFrontPet(uint8(teamIdx), newPetIdx))
        return;

    PetBattles::PetBattleTeamData& team = battle->GetTeam(teamIdx);
    bool const replacingDeadPet = battle->GetBattleState() == PetBattles::PET_BATTLE_STATE_WAITING_FOR_FRONT_PET;

    team.FrontPetIndex = newPetIdx;
    battle->ClearNeedsFrontPetSwap(uint8(teamIdx));

    // The next round starts once no team is still choosing a replacement.
    if (replacingDeadPet)
    {
        team.HasInputThisRound = false;
        bool anyStillChoosing = false;
        for (uint8 i = 0; i < PetBattles::MAX_PET_BATTLE_PLAYERS; ++i)
            anyStillChoosing |= battle->NeedsFrontPetSwap(i);
        if (anyStillChoosing)
            return;
        battle->RefreshInputFlags();
        battle->SetBattleState(PetBattles::PET_BATTLE_STATE_ROUND_IN_PROGRESS);
    }

    WorldPackets::BattlePet::PetBattleReplacementsMade replacements;
    replacements.SceneObjectGUID = battle->GetSceneObjectGUID();
    replacements.CurRound = battle->GetCurrentRound();
    replacements.NextPetBattleState = static_cast<int8>(PetBattles::PET_BATTLE_STATE_ROUND_IN_PROGRESS);
    for (uint8 i = 0; i < PetBattles::MAX_PET_BATTLE_PLAYERS; ++i)
        BuildPetBattleRoundPlayerData(replacements.Players[i], battle->GetTeam(i), battle, i);

    // A pet swap effect for every team's front pet so the client updates the pets and ability bars.
    EmitInitialFrontPetSwapEffects(replacements.Effects, battle);
    replacements.Write();

    for (uint8 t = 0; t < PetBattles::MAX_PET_BATTLE_PLAYERS; ++t)
        if (Player* p = battle->GetPlayerForTeam(t))
            p->SendDirectMessage(replacements.GetRawPacket());
}

void WorldSession::HandlePetBattleQuitNotify(WorldPackets::BattlePet::PetBattleQuitNotify& /*petBattleQuitNotify*/)
{
    Player* player = GetPlayer();

    if (PetBattles::PetBattle* battle = sPetBattleMgr->GetBattleByPlayer(player->GetGUID()))
        QuitPetBattle(player, battle);
}

void WorldSession::HandlePetBattleFinalNotify(WorldPackets::BattlePet::PetBattleFinalNotify& /*petBattleFinalNotify*/)
{
    Player* player = GetPlayer();

    PetBattles::PetBattle* battle = sPetBattleMgr->GetBattleByPlayer(player->GetGUID());
    if (!battle || !battle->IsFinalRound())
        return;

    // Ignore if FinalRound packet hasn't been sent yet (death animation still playing)
    if (battle->HasPendingFinishDelay())
        return;

    // Transition FINAL_ROUND → FINISHED (sends Finished packet, syncs health, sends journal)
    battle->CompleteBattle();

    // Remove the battle from tracking maps so player can start a new one
    sPetBattleMgr->RemoveBattle(battle->GetBattleID());
}

// What stops these two players from fighting a pet duel now, checked for the challenger and again
// when the challenge is answered.
static PetBattles::PetBattleRequestFailReason CheckCanDuel(Player* challenger, Player* target)
{
    if (!target || target == challenger || !target->IsInWorld() || target->GetMap() != challenger->GetMap()
        || !challenger->IsWithinDistInMap(target, PetBattles::PET_BATTLE_PVP_CHALLENGE_RANGE))
        return PetBattles::PET_BATTLE_REQUEST_FAIL_TARGET_OUT_OF_RANGE;

    if (CheckCanStartPetBattle(target) != PetBattles::PET_BATTLE_REQUEST_FAIL_OK)
        return PetBattles::PET_BATTLE_REQUEST_FAIL_OPPONENT_NOT_AVAILABLE;

    return CheckCanStartPetBattle(challenger);
}

void WorldSession::HandlePetBattleRequestPVP(WorldPackets::BattlePet::PetBattleRequestPVP& petBattleRequestPVP)
{
    Player* player = GetPlayer();

    Player* target = ObjectAccessor::GetPlayer(*player, petBattleRequestPVP.TargetGUID);
    PetBattles::PetBattleRequestFailReason reason = CheckCanDuel(player, target);
    if (reason != PetBattles::PET_BATTLE_REQUEST_FAIL_OK)
    {
        SendPetBattleRequestFailed(player, reason);
        return;
    }

    if (!IsValidPetBattleLocation(player, petBattleRequestPVP.Location))
    {
        SendPetBattleRequestFailed(player, PetBattles::PET_BATTLE_REQUEST_FAIL_NOT_HERE);
        return;
    }

    sPetBattleMgr->AddChallenge(player->GetGUID(), target->GetGUID(), petBattleRequestPVP.Location);

    WorldPackets::BattlePet::PetBattlePVPChallenge challenge;
    challenge.ChallengerGUID = player->GetGUID();
    challenge.Location = petBattleRequestPVP.Location;
    target->SendDirectMessage(challenge.Write());
}

void WorldSession::HandleJoinPetBattleQueue(WorldPackets::BattlePet::JoinPetBattleQueue& /*joinPetBattleQueue*/)
{
    // PetBattleMgr::JoinQueue checks the player and answers with one SMSG_PET_BATTLE_QUEUE_STATUS.
    sPetBattleMgr->JoinQueue(GetPlayer()->GetGUID());
}

void WorldSession::HandleLeavePetBattleQueue(WorldPackets::BattlePet::LeavePetBattleQueue& /*leavePetBattleQueue*/)
{
    sPetBattleMgr->LeaveQueue(GetPlayer()->GetGUID());  // LeaveQueue sends REMOVED itself
}

void WorldSession::HandlePetBattleQueueProposeMatchResult(WorldPackets::BattlePet::PetBattleQueueProposeMatchResult& petBattleQueueProposeMatchResult)
{
    sPetBattleMgr->HandleProposalResult(GetPlayer()->GetGUID(), petBattleQueueProposeMatchResult.Accepted);
}

// The challenged player's answer to SMSG_PET_BATTLE_PVP_CHALLENGE. Only a challenge this player
// actually received from TargetGUID, and that has not lapsed, can be accepted or declined.
void WorldSession::HandlePetBattleRequestUpdate(WorldPackets::BattlePet::PetBattleRequestUpdate& petBattleRequestUpdate)
{
    Player* player = GetPlayer();

    WorldPackets::BattlePet::PetBattleLocation location;
    if (!sPetBattleMgr->TakeChallenge(player->GetGUID(), petBattleRequestUpdate.TargetGUID, location))
    {
        // The challenger withdrawing their own challenge.
        if (petBattleRequestUpdate.Canceled)
            sPetBattleMgr->TakeChallenge(petBattleRequestUpdate.TargetGUID, player->GetGUID(), location);
        return;
    }

    Player* challenger = ObjectAccessor::GetPlayer(*player, petBattleRequestUpdate.TargetGUID);
    if (petBattleRequestUpdate.Canceled)
    {
        if (challenger)
            SendPetBattleRequestFailed(challenger, PetBattles::PET_BATTLE_REQUEST_FAIL_DECLINED);
        return;
    }

    if (!challenger)
    {
        SendPetBattleRequestFailed(player, PetBattles::PET_BATTLE_REQUEST_FAIL_OPPONENT_NOT_AVAILABLE);
        return;
    }

    PetBattles::PetBattleRequestFailReason reason = CheckCanDuel(challenger, player);
    if (reason == PetBattles::PET_BATTLE_REQUEST_FAIL_OK && !IsValidPetBattleLocation(challenger, location))
        reason = PetBattles::PET_BATTLE_REQUEST_FAIL_NOT_HERE;
    if (reason != PetBattles::PET_BATTLE_REQUEST_FAIL_OK)
    {
        SendPetBattleRequestFailed(challenger, reason);
        SendPetBattleRequestFailed(player, reason);
        return;
    }

    PetBattles::PetBattle* battle = sPetBattleMgr->CreatePvPBattle(challenger, player);
    if (!battle)
    {
        SendPetBattleRequestFailed(challenger, PetBattles::PET_BATTLE_REQUEST_FAIL_INVALID_LOADOUT_ALL_DEAD);
        SendPetBattleRequestFailed(player, PetBattles::PET_BATTLE_REQUEST_FAIL_INVALID_LOADOUT_ALL_DEAD);
        return;
    }

    SendPetBattleOpening(battle, location, 0, 0);
}

void WorldSession::HandlePetBattleScriptErrorNotify(WorldPackets::BattlePet::PetBattleScriptErrorNotify& /*petBattleScriptErrorNotify*/)
{
    // Client reports script error - log only
    TC_LOG_DEBUG("server.loading", "PetBattleHandler: Client reported pet battle script error");
}

void WorldSession::HandlePetBattleWildLocationFail(WorldPackets::BattlePet::PetBattleWildLocationFail& /*petBattleWildLocationFail*/)
{
    // Client reports wild battle location failure - log only
    TC_LOG_DEBUG("server.loading", "PetBattleHandler: Client reported wild pet battle location failure");
}
