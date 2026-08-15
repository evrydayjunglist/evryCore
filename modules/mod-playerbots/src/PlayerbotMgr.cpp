/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "PlayerbotMgr.h"
#include "Config.h"
#include "Creature.h"
#include "GameTime.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotFactory.h"
#include "UnitDefines.h"
#include "World.h"
#include "WorldSession.h"

namespace
{
    constexpr float QUEST_SEARCH_RANGE = 40.0f;
    constexpr float COMBAT_SEARCH_RANGE = 150.0f;
    constexpr uint32 QUEST_CHAIN_PAUSE_MS = 750;
    constexpr uint32 QUEST_SEARCH_RETRY_MS = 5000;
}

PlayerbotMgr* PlayerbotMgr::instance()
{
    static PlayerbotMgr instance;
    return &instance;
}

PlayerbotMgr::~PlayerbotMgr() = default;

void PlayerbotMgr::Start()
{
    if (sConfigMgr->GetBoolDefault(PLAYERBOTS_REGENERATE_CHARACTERS, false))
    {
        uint32 deleted = PlayerbotFactory::DeleteAllBotCharacters();
        TC_LOG_INFO(PLAYERBOTS_LOG,
            "mod-playerbots: Playerbots.RegenerateCharacters is 1. Deleted {} bot character(s). Battlenet accounts were kept. "
            "Set Playerbots.RegenerateCharacters to 0, then start worldserver again. This process is stopping so those new bots are not created and wiped on the next boot.",
            deleted);
        World::StopNow(SHUTDOWN_EXIT_CODE);
        return;
    }

    if (!sConfigMgr->GetBoolDefault(PLAYERBOTS_ENABLE, false))
    {
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: disabled.");
        return;
    }

    int32 count = sConfigMgr->GetIntDefault(PLAYERBOTS_COUNT, 1);
    if (count < 1)
    {
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: count is {}; not starting bots.", count);
        return;
    }

    uint32 sessionsAtStartup = sWorld->GetActiveAndQueuedSessionCount();
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: starting {} bot(s). Active sessions at OnStartup: {}.",
        count, sessionsAtStartup);

    for (int32 i = 1; i <= count; ++i)
    {
        PlayerbotRecord bot;
        bot.Account.Index = uint32(i);
        if (!PlayerbotFactory::EnsureAccount(bot.Account) || !PlayerbotFactory::EnsureCharacter(bot.Account))
        {
            TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: bot {} was not created.", i);
            continue;
        }

        _accountIds.insert(bot.Account.AccountId);
        TryLogin(bot);
        _bots.push_back(std::move(bot));
    }
}

void PlayerbotMgr::Update(uint32 diff)
{
    for (PlayerbotRecord& bot : _bots)
    {
        UpdateLogin(bot);
        UpdateWorld(bot, diff);
    }
}

bool PlayerbotMgr::IsBotAccount(uint32 accountId) const
{
    return _accountIds.contains(accountId);
}

void PlayerbotMgr::OnBotLogin(Player* player)
{
    if (!player)
        return;

    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} is in the world. Cinematic skip, time sync, and walking run from WorldScript::OnUpdate.",
        player->GetName());
}

void PlayerbotMgr::TryLogin(PlayerbotRecord& bot)
{
    std::unique_ptr<WorldSession> session = PlayerbotFactory::MakeSession(bot.Account);
    TC_LOG_INFO(PLAYERBOTS_LOG,
        "mod-playerbots: constructed WorldSession for account {} with an empty socket. Calling World::AddSession.",
        bot.Account.AccountId);
    sWorld->AddSession(session.release());
}

void PlayerbotMgr::UpdateLogin(PlayerbotRecord& bot)
{
    WorldSession* session = sWorld->FindSession(bot.Account.AccountId);
    if (!session)
        return;

    if (session->IsInQueue())
        return;

    if (!bot.EnumQueued)
    {
        PlayerbotClient::QueueEnumCharacters(session);
        bot.EnumQueued = true;
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: account {} queued CMSG_ENUM_CHARACTERS.", bot.Account.AccountId);
        return;
    }

    if (!bot.LoginQueued)
    {
        if (!session->IsLegitCharacterForAccount(bot.Account.CharacterGuid))
            return;

        PlayerbotClient::QueuePlayerLogin(session, bot.Account.CharacterGuid);
        bot.LoginQueued = true;
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: account {} queued CMSG_PLAYER_LOGIN for {}.",
            bot.Account.AccountId, bot.Account.CharacterGuid.ToString());
        return;
    }

    if (bot.ContinueLoginCalled)
        return;

    if (!session->PlayerLoading())
        return;

    session->HandleContinuePlayerLogin();
    bot.ContinueLoginCalled = true;
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: account {} called HandleContinuePlayerLogin.", bot.Account.AccountId);
}

void PlayerbotMgr::ReplyTimeSync(WorldSession* session)
{
    uint32 sequenceIndex = 0;
    if (!session->GetOldestPendingTimeSyncCounter(sequenceIndex))
        return;

    uint32 const clientTime = GameTime::GetGameTimeMS();
    PlayerbotClient::QueueTimeSyncResponse(session, sequenceIndex, clientTime);
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: account {} queued CMSG_TIME_SYNC_RESPONSE for sequence {}.",
        session->GetAccountId(), sequenceIndex);
}

void PlayerbotMgr::UpdateWorld(PlayerbotRecord& bot, uint32 diff)
{
    if (!bot.ContinueLoginCalled)
        return;

    WorldSession* session = sWorld->FindSession(bot.Account.AccountId);
    if (!session)
        return;

    Player* player = session->GetPlayer();
    if (!player || !player->IsInWorld())
        return;

    ReplyTimeSync(session);

    if (!bot.CinematicSkipped)
    {
        PlayerbotClient::QueueCompleteCinematic(session);
        bot.CinematicSkipped = true;
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_COMPLETE_CINEMATIC.", player->GetName());
    }

    if (!bot.InitMoverQueued)
    {
        PlayerbotClient::QueueMoveInitActiveMoverComplete(session, GameTime::GetGameTimeMS());
        bot.InitMoverQueued = true;
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_MOVE_INIT_ACTIVE_MOVER_COMPLETE.", player->GetName());
    }

    if (!player->IsAlive())
    {
        if (!bot.QuestSearchFailed)
        {
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} died. The bot is standing still.", player->GetName());
            ClearCombat(bot, player);
            bot.QuestSearchFailed = true;
        }
        return;
    }

    if (bot.Walker.IsMoving())
        bot.Walker.Update(player, diff);

    if (bot.QuestInteractQueued)
    {
        bot.QuestInteractWaitMs += diff;
        if (bot.QuestInteractWaitMs < QUEST_CHAIN_PAUSE_MS)
            return;

        bot.QuestInteractQueued = false;
        bot.QuestInteractWaitMs = 0;
        bot.QuestArriveWaitMs = 0;
        bot.QuestSearchEmptyMs = 0;
        bot.QuestTarget = {};
        bot.UnreachableGuids.clear();
        bot.Walker.Reset();
    }

    if (bot.Walker.IsMoving())
        return;

    if (bot.Walker.HasFailed())
    {
        if (bot.CombatTarget.CreatureGuid.IsEmpty())
            return;

        bot.UnreachableGuids.insert(bot.CombatTarget.CreatureGuid);
        ClearCombat(bot, player);
        bot.Walker.Reset();
    }

    if (!bot.CombatTarget.CreatureGuid.IsEmpty())
    {
        if (UpdateCombat(bot, player))
            return;
    }

    if (bot.Walker.HasArrived() && !bot.QuestTarget.NpcGuid.IsEmpty())
    {
        bot.QuestArriveWaitMs += diff;
        if (PlayerbotClient::TryInteractQuest(player, bot.QuestTarget))
        {
            bot.QuestInteractQueued = true;
            bot.QuestInteractWaitMs = 0;
        }
        else
        {
            Creature* creature = ObjectAccessor::GetCreature(*player, bot.QuestTarget.NpcGuid);
            if (creature && creature->IsAlive()
                && !player->IsWithinDistInMap(creature, creature->GetCombatReach() + 4.0f))
            {
                Position standPos;
                float const standDistance = creature->GetCombatReach() + 1.0f;
                if (PlayerbotWalker::PickApproachPosition(player, creature, standDistance, standPos)
                    && bot.Walker.Start(player, standPos, bot.QuestTarget.StopDistance))
                {
                    bot.QuestTarget.Pos = standPos;
                    bot.QuestArriveWaitMs = 0;
                    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} is still short of {} and is walking the rest of the way.",
                        player->GetName(), bot.QuestTarget.NpcGuid.ToString());
                    return;
                }
            }

            if (bot.QuestArriveWaitMs >= 5000)
            {
                TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} arrived but cannot interact with {}.",
                    player->GetName(), bot.QuestTarget.NpcGuid.ToString());
                bot.QuestSearchFailed = true;
            }
        }
        return;
    }

    if (bot.QuestSearchFailed)
        return;

    if (Optional<PlayerbotClient::QuestTarget> turnIn = PlayerbotClient::FindNearbyQuestTarget(player, QUEST_SEARCH_RANGE, PlayerbotClient::QuestSearchKind::TurnIn))
    {
        BeginQuestTarget(bot, player, *turnIn);
        return;
    }

    if (Optional<PlayerbotClient::QuestTarget> logTurnIn = PlayerbotClient::FindLogCompleteTurnIn(player))
    {
        BeginQuestTarget(bot, player, *logTurnIn);
        return;
    }

    if (PlayerbotClient::HasLogCompleteTurnInOnThisMap(player))
    {
        bot.QuestSearchEmptyMs += diff;
        if (bot.QuestSearchEmptyMs >= QUEST_SEARCH_RETRY_MS)
        {
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has a finished quest on this map but no living ender to walk to. The quest is not skipped.",
                player->GetName());
            bot.QuestSearchFailed = true;
        }
        return;
    }

    if (Optional<PlayerbotClient::CombatTarget> kill = PlayerbotClient::FindNearbyMonsterObjectiveTarget(player, COMBAT_SEARCH_RANGE, bot.UnreachableGuids))
    {
        bot.QuestSearchEmptyMs = 0;
        bot.QuestTarget = {};
        bot.CombatTarget = *kill;
        bot.CombatSwingSent = false;

        Creature* creature = ObjectAccessor::GetCreature(*player, bot.CombatTarget.CreatureGuid);
        if (creature && player->IsWithinMeleeRange(creature) && PlayerbotClient::TryMeleeAttack(player, bot.CombatTarget.CreatureGuid))
        {
            bot.CombatSwingSent = true;
            return;
        }

        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking to {} for quest {} (kill).",
            player->GetName(), bot.CombatTarget.CreatureGuid.ToString(), bot.CombatTarget.QuestId);
        bot.QuestArriveWaitMs = 0;
        if (!bot.Walker.Start(player, bot.CombatTarget.Pos, bot.CombatTarget.StopDistance))
        {
            bot.UnreachableGuids.insert(bot.CombatTarget.CreatureGuid);
            ClearCombat(bot, player);
            bot.Walker.Reset();
        }
        return;
    }

    if (Optional<PlayerbotClient::QuestTarget> accept = PlayerbotClient::FindNearbyQuestTarget(player, QUEST_SEARCH_RANGE, PlayerbotClient::QuestSearchKind::Accept))
    {
        BeginQuestTarget(bot, player, *accept);
        return;
    }

    bot.QuestSearchEmptyMs += diff;
    if (bot.QuestSearchEmptyMs >= QUEST_SEARCH_RETRY_MS)
    {
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has no nearby turn-in, log turn-in, kill target, or quest to accept (talk {:.0f} yards, kill {:.0f} yards). The quest is not skipped.",
            player->GetName(), QUEST_SEARCH_RANGE, COMBAT_SEARCH_RANGE);
        bot.QuestSearchFailed = true;
    }
}

void PlayerbotMgr::ClearCombat(PlayerbotRecord& bot, Player* player)
{
    if (player && player->GetSession() && (!bot.CombatTarget.CreatureGuid.IsEmpty() || player->HasUnitState(UNIT_STATE_MELEE_ATTACKING)))
        PlayerbotClient::QueueAttackStop(player->GetSession());

    bot.CombatTarget = {};
    bot.CombatSwingSent = false;
}

bool PlayerbotMgr::UpdateCombat(PlayerbotRecord& bot, Player* player)
{
    Creature* creature = ObjectAccessor::GetCreature(*player, bot.CombatTarget.CreatureGuid);
    if (!creature || !creature->IsAlive() || !player->IsValidAttackTarget(creature)
        || !PlayerbotClient::CombatTargetStillNeeded(player, bot.CombatTarget))
    {
        ClearCombat(bot, player);
        bot.Walker.Reset();
        return false;
    }

    if (!player->IsWithinMeleeRange(creature))
    {
        bot.CombatSwingSent = false;
        Position standPos;
        float const standDistance = creature->GetCombatReach() + 1.0f;
        if (!PlayerbotWalker::PickApproachPosition(player, creature, standDistance, standPos))
        {
            bot.UnreachableGuids.insert(bot.CombatTarget.CreatureGuid);
            ClearCombat(bot, player);
            bot.Walker.Reset();
            return false;
        }

        bot.CombatTarget.Pos = standPos;
        if (player->GetExactDist(standPos) <= bot.CombatTarget.StopDistance)
        {
            bot.UnreachableGuids.insert(bot.CombatTarget.CreatureGuid);
            ClearCombat(bot, player);
            bot.Walker.Reset();
            return false;
        }

        if (!bot.Walker.Start(player, standPos, bot.CombatTarget.StopDistance))
        {
            bot.UnreachableGuids.insert(bot.CombatTarget.CreatureGuid);
            ClearCombat(bot, player);
            bot.Walker.Reset();
            return false;
        }

        return true;
    }

    if (player->GetVictim() == creature && player->HasUnitState(UNIT_STATE_MELEE_ATTACKING))
        return true;

    if (!bot.CombatSwingSent && PlayerbotClient::TryMeleeAttack(player, bot.CombatTarget.CreatureGuid))
        bot.CombatSwingSent = true;

    return true;
}

bool PlayerbotMgr::BeginQuestTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::QuestTarget const& target)
{
    ClearCombat(bot, player);
    bot.QuestSearchEmptyMs = 0;
    bot.QuestTarget = target;
    bot.CombatTarget = {};
    bot.Walker.Reset();

    if (player->GetExactDist(bot.QuestTarget.Pos) <= bot.QuestTarget.StopDistance
        && PlayerbotClient::TryInteractQuest(player, bot.QuestTarget))
    {
        bot.QuestInteractQueued = true;
        bot.QuestInteractWaitMs = 0;
        return true;
    }

    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking to {} for quest {} ({}).",
        player->GetName(), bot.QuestTarget.NpcGuid.ToString(), bot.QuestTarget.QuestId,
        bot.QuestTarget.TurnIn ? "turn-in" : "accept");
    bot.QuestArriveWaitMs = 0;
    bot.Walker.Start(player, bot.QuestTarget.Pos, bot.QuestTarget.StopDistance);
    return true;
}
