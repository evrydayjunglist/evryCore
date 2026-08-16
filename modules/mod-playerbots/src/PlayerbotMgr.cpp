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
#include "Corpse.h"
#include "Creature.h"
#include "GameObject.h"
#include "GameTime.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotFactory.h"
#include "SpellInfo.h"
#include "Unit.h"
#include "UnitDefines.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <limits>
#include <unordered_set>

namespace
{
    constexpr float QUEST_SEARCH_RANGE = 40.0f;
    constexpr float COMBAT_SEARCH_RANGE = 150.0f;
    constexpr float LOOT_SEARCH_RANGE = 10.0f;
    constexpr uint32 COMBAT_CAST_RETRY_MS = 100;
    constexpr uint32 QUEST_CHAIN_PAUSE_MS = 750;
    constexpr uint32 QUEST_SEARCH_RETRY_MS = 5000;
    constexpr uint32 VENDOR_RETRY_MS = 60000;
    constexpr uint32 RELEASE_WAIT_MS = 3000;
    constexpr uint32 GHOST_SETTLE_MS = 500;
    constexpr uint32 PACKET_RETRY_MS = 2000;
    constexpr uint32 CAMPED_WAIT_MS = 20000;
    constexpr uint32 GHOST_WAIT_LONG_MS = 180000;
    constexpr uint32 GHOST_GIVE_UP_MS = 300000;

    bool InInteractRange(Player const* player, Creature const* creature)
    {
        if (!player || !creature)
            return false;

        return player->IsWithinDistInMap(creature, creature->GetCombatReach() + 4.0f);
    }

    // True when the current walk is already a talk, or already standing in range to use, loot, or click.
    // A 40-yard ! may stop a long walk. It should not pull her off a click she can finish from here.
    bool CurrentWalkIsInReach(PlayerbotRecord const& bot, Player* player)
    {
        if (!player)
            return false;

        if (!bot.QuestTarget.NpcGuid.IsEmpty())
            return true;

        if (!bot.UseItemOnUnitTarget.CreatureGuid.IsEmpty())
        {
            Creature* creature = ObjectAccessor::GetCreature(*player, bot.UseItemOnUnitTarget.CreatureGuid);
            return InInteractRange(player, creature);
        }

        if (!bot.GameObjectTarget.GoGuid.IsEmpty())
            return player->GetGameObjectIfCanInteractWith(bot.GameObjectTarget.GoGuid) != nullptr;

        if (!bot.ItemLootTarget.GoGuid.IsEmpty())
            return player->GetGameObjectIfCanInteractWith(bot.ItemLootTarget.GoGuid) != nullptr;

        if (!bot.ItemLootTarget.CreatureGuid.IsEmpty())
        {
            Creature* creature = ObjectAccessor::GetCreature(*player, bot.ItemLootTarget.CreatureGuid);
            return InInteractRange(player, creature);
        }

        return false;
    }

    bool CurrentWalkIsFartherThan(PlayerbotRecord const& bot, Player* player, float range)
    {
        if (!player)
            return false;

        if (!bot.VendorTarget.NpcGuid.IsEmpty())
            return player->GetExactDist(bot.VendorTarget.Pos) > range;
        if (!bot.QuestTarget.NpcGuid.IsEmpty())
            return player->GetExactDist(bot.QuestTarget.Pos) > range;
        if (bot.GameObjectTarget.QuestId)
            return player->GetExactDist(bot.GameObjectTarget.Pos) > range;
        if (bot.UseItemOnUnitTarget.QuestId)
            return player->GetExactDist(bot.UseItemOnUnitTarget.Pos) > range;
        if (bot.CombatTarget.QuestId || !bot.CombatTarget.CreatureGuid.IsEmpty())
            return player->GetExactDist(bot.CombatTarget.Pos) > range;
        if (bot.ItemLootTarget.QuestId || bot.ItemLootTarget.LootCorpse)
            return player->GetExactDist(bot.ItemLootTarget.Pos) > range;
        return false;
    }

    bool CreatureIsHittingPlayer(Player const* player, ObjectGuid guid)
    {
        if (!player || guid.IsEmpty())
            return false;

        for (Unit* attacker : player->getAttackers())
        {
            if (attacker && attacker->GetGUID() == guid)
                return true;
        }

        return false;
    }

    // Stay on this fight after a close-in walk fails: in melee, or this creature is hitting her.
    bool KeepCombatAfterFailedWalk(Player* player, ObjectGuid creatureGuid)
    {
        if (!player || creatureGuid.IsEmpty())
            return false;

        if (CreatureIsHittingPlayer(player, creatureGuid))
            return true;

        Creature* creature = ObjectAccessor::GetCreature(*player, creatureGuid);
        return creature && player->IsWithinMeleeRange(creature);
    }

    void LogStayOnCombatWalkFail(Player const* player, ObjectGuid creatureGuid)
    {
        if (!player)
            return;

        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} close-in walk failed; staying on {}.",
            player->GetName(), creatureGuid.ToString());
    }

    void StopWalkToClick(PlayerbotRecord& bot, Player* player, ObjectGuid const& guid)
    {
        if (!bot.Walker.IsMoving() || !player)
            return;

        bot.Walker.Stop(player);
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} stopped walking to click {} from here.",
            player->GetName(), guid.ToString());
    }

    void ClearUnreachable(PlayerbotRecord& bot)
    {
        bot.UnreachableGuids.clear();
        bot.UnreachablePositions.clear();
    }

    void RememberFailedYellow(PlayerbotRecord& bot, Position const& pos)
    {
        bot.UnreachablePositions.push_back(pos);
    }

    PlayerbotClient::MapYellowFilter MakeMapYellowFilter(PlayerbotRecord const& bot, int32 questId, uint32 entry, bool keepQuest, Position const* skipPos)
    {
        PlayerbotClient::MapYellowFilter filter;
        filter.QuestId = questId;
        filter.Entry = entry;
        filter.KeepQuest = keepQuest;
        filter.SkipPos = skipPos;
        filter.SkipPositions = &bot.UnreachablePositions;
        return filter;
    }
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

void PlayerbotMgr::ReplyTeleportAcks(Player* player)
{
    if (!player || !player->GetSession())
        return;

    if (player->IsBeingTeleportedNear() && player->GetTeleportState() == TeleportState::WaitingForTeleportAck)
    {
        PlayerbotClient::QueueMoveTeleportAck(player);
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_MOVE_TELEPORT_ACK.", player->GetName());
        return;
    }

    if (player->GetTeleportState() == TeleportState::WaitingForWorldPortAck)
    {
        PlayerbotClient::QueueWorldPortResponse(player->GetSession());
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_WORLD_PORT_RESPONSE.", player->GetName());
    }
}

void PlayerbotMgr::UpdateWorld(PlayerbotRecord& bot, uint32 diff)
{
    if (!bot.ContinueLoginCalled)
        return;

    WorldSession* session = sWorld->FindSession(bot.Account.AccountId);
    if (!session)
        return;

    Player* player = session->GetPlayer();
    if (!player)
        return;

    ReplyTeleportAcks(player);

    if (!player->IsInWorld())
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

    if (UpdateDeath(bot, player, diff))
        return;

    if (bot.VendorRetryMs)
    {
        if (bot.VendorRetryMs > diff)
            bot.VendorRetryMs -= diff;
        else
            bot.VendorRetryMs = 0;
    }

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
        bot.GameObjectTarget = {};
        bot.UseItemOnUnitTarget = {};
        bot.ItemLootTarget = {};
        bot.LootOpenSent = false;
        ClearVendor(bot);
        bot.LookedForOtherYellowOnFace = false;
        bot.UnreachableGuids.clear();
        bot.UnreachablePositions.clear();
        bot.Walker.Reset();
    }

    // Click from here before this tick's step. Do not heartbeat into a lip a player would already click over.
    if (bot.CombatTarget.CreatureGuid.IsEmpty() && bot.Walker.IsMoving())
    {
        if (Optional<PlayerbotClient::CombatTarget> attacker = PlayerbotClient::FindAttackerTarget(player))
        {
            BeginCombatTarget(bot, player, *attacker);
            return;
        }

        if (TryClickFromHere(bot, player))
            return;
    }

    if (bot.Walker.IsMoving())
        bot.Walker.Update(player, diff);

    int32 skipFailedQuestId = 0;
    uint32 skipFailedEntry = 0;
    int32 sameObjectiveQuestId = 0;
    uint32 sameObjectiveEntry = 0;
    Position sameObjectiveSkipPos;
    bool retrySameObjective = false;
    if (bot.Walker.HasFailed())
    {
        bool const keepCombat = KeepCombatAfterFailedWalk(player, bot.CombatTarget.CreatureGuid)
            && bot.GameObjectTarget.QuestId == 0
            && bot.UseItemOnUnitTarget.QuestId == 0
            && bot.ItemLootTarget.QuestId == 0
            && bot.QuestTarget.NpcGuid.IsEmpty()
            && bot.VendorTarget.NpcGuid.IsEmpty();

        if (bot.GameObjectTarget.QuestId)
        {
            skipFailedQuestId = bot.GameObjectTarget.QuestId;
            skipFailedEntry = bot.GameObjectTarget.GoEntry;
            sameObjectiveQuestId = skipFailedQuestId;
            sameObjectiveEntry = skipFailedEntry;
            sameObjectiveSkipPos = bot.GameObjectTarget.Pos;
            retrySameObjective = true;
        }
        else if (bot.UseItemOnUnitTarget.QuestId)
        {
            skipFailedQuestId = bot.UseItemOnUnitTarget.QuestId;
            skipFailedEntry = bot.UseItemOnUnitTarget.CreditEntry;
            sameObjectiveQuestId = skipFailedQuestId;
            sameObjectiveEntry = skipFailedEntry;
            sameObjectiveSkipPos = bot.UseItemOnUnitTarget.Pos;
            retrySameObjective = true;
        }
        else if (bot.CombatTarget.QuestId)
        {
            skipFailedQuestId = bot.CombatTarget.QuestId;
            skipFailedEntry = bot.CombatTarget.CreditEntry;
            if (!keepCombat)
            {
                sameObjectiveQuestId = skipFailedQuestId;
                sameObjectiveEntry = skipFailedEntry;
                sameObjectiveSkipPos = bot.CombatTarget.Pos;
                retrySameObjective = true;
            }
        }
        else if (bot.ItemLootTarget.QuestId)
        {
            skipFailedQuestId = bot.ItemLootTarget.QuestId;
            skipFailedEntry = bot.ItemLootTarget.ItemId;
            sameObjectiveQuestId = skipFailedQuestId;
            sameObjectiveEntry = skipFailedEntry;
            sameObjectiveSkipPos = bot.ItemLootTarget.Pos;
            retrySameObjective = true;
        }
        else if (bot.QuestTarget.QuestId)
            skipFailedQuestId = bot.QuestTarget.QuestId;

        RecoverFailedWalk(bot, player);
    }

    if (bot.Walker.IsMoving() && bot.Walker.StartedOnAFace() && !bot.LookedForOtherYellowOnFace)
    {
        bot.LookedForOtherYellowOnFace = true;
        if (TryLeaveFaceForOtherYellow(bot, player))
        {
            // Begin* clears the flag. Keep it: one look per hill, not one per Start.
            bot.LookedForOtherYellowOnFace = true;
            return;
        }
    }

    if (!bot.CombatTarget.CreatureGuid.IsEmpty())
    {
        if (UpdateCombat(bot, player, diff))
            return;
    }

    if (bot.ItemLootTarget.LootCorpse
        || (bot.ItemLootTarget.QuestId && !bot.ItemLootTarget.GoGuid.IsEmpty()))
    {
        if (UpdateItemLoot(bot, player, diff))
            return;
    }

    if (Optional<PlayerbotClient::CombatTarget> attacker = PlayerbotClient::FindAttackerTarget(player))
    {
        if (attacker->CreatureGuid != bot.CombatTarget.CreatureGuid)
            BeginCombatTarget(bot, player, *attacker);
        return;
    }

    if (bot.Walker.HasArrived() && !bot.QuestTarget.NpcGuid.IsEmpty())
    {
        bot.QuestArriveWaitMs += diff;
        if (PlayerbotClient::TryInteractQuest(player, bot.QuestTarget))
        {
            bot.QuestInteractQueued = true;
            bot.QuestInteractWaitMs = 0;
            return;
        }

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

        if (bot.QuestArriveWaitMs < 5000)
            return;

        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} arrived but cannot interact with {}. Looking for other work.",
            player->GetName(), bot.QuestTarget.NpcGuid.ToString());
        bot.UnreachableGuids.insert(bot.QuestTarget.NpcGuid);
        bot.QuestTarget = {};
        bot.Walker.Reset();
    }

    if (bot.Walker.HasArrived() && bot.GameObjectTarget.QuestId)
    {
        if (!PlayerbotClient::GameObjectTargetStillNeeded(player, bot.GameObjectTarget))
        {
            bot.GameObjectTarget = {};
            bot.Walker.Reset();
        }
        else if (!bot.GameObjectTarget.GoGuid.IsEmpty())
        {
            bot.QuestArriveWaitMs += diff;
            if (PlayerbotClient::TryUseGameObject(player, bot.GameObjectTarget))
            {
                bot.QuestInteractQueued = true;
                bot.QuestInteractWaitMs = 0;
                return;
            }

            GameObject* go = ObjectAccessor::GetGameObject(*player, bot.GameObjectTarget.GoGuid);
            if (go && go->isSpawned() && !go->IsWithinDistInMap(player))
            {
                float size = 1.0f;
                if (go->GetGOInfo())
                    size = go->GetGOInfo()->size < 1.0f ? 1.0f : go->GetGOInfo()->size;
                Position standPos;
                if (PlayerbotWalker::PickApproachPosition(player, go, size + 1.0f, standPos)
                    && bot.Walker.Start(player, standPos, bot.GameObjectTarget.StopDistance))
                {
                    bot.GameObjectTarget.Pos = standPos;
                    bot.QuestArriveWaitMs = 0;
                    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} is still short of {} and is walking the rest of the way.",
                        player->GetName(), bot.GameObjectTarget.GoGuid.ToString());
                    return;
                }
            }

            if (bot.QuestArriveWaitMs < 5000)
                return;

            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} arrived but cannot use {}. Looking for other work.",
                player->GetName(), bot.GameObjectTarget.GoGuid.ToString());
            bot.UnreachableGuids.insert(bot.GameObjectTarget.GoGuid);
            bot.GameObjectTarget = {};
            bot.Walker.Reset();
        }
        else
        {
            if (TryImmediateWorld(bot, player, false))
                return;

            bot.QuestArriveWaitMs += diff;
            if (Optional<PlayerbotClient::GameObjectTarget> found = PlayerbotClient::FindLogIncompleteGameObjectTarget(player, bot.UnreachableGuids))
            {
                if (!found->GoGuid.IsEmpty())
                {
                    BeginGameObjectTarget(bot, player, *found);
                    return;
                }
            }

            if (bot.QuestArriveWaitMs < QUEST_SEARCH_RETRY_MS)
                return;

            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} reached the gameobject map marker but no spawned object is there yet. Looking for other work.",
                player->GetName());
            int32 const skipQuestId = bot.GameObjectTarget.QuestId;
            uint32 const skipEntry = bot.GameObjectTarget.GoEntry;
            bot.GameObjectTarget = {};
            bot.Walker.Reset();
            if (TryMapYellow(bot, player, skipQuestId, skipEntry))
                return;
        }
    }

    if (bot.Walker.HasArrived() && bot.UseItemOnUnitTarget.QuestId)
    {
        if (!PlayerbotClient::UseItemOnUnitTargetStillNeeded(player, bot.UseItemOnUnitTarget))
        {
            bot.UseItemOnUnitTarget = {};
            bot.Walker.Reset();
        }
        else if (!bot.UseItemOnUnitTarget.CreatureGuid.IsEmpty())
        {
            bot.QuestArriveWaitMs += diff;
            if (PlayerbotClient::TryUseItemOnUnit(player, bot.UseItemOnUnitTarget))
            {
                bot.QuestInteractQueued = true;
                bot.QuestInteractWaitMs = 0;
                return;
            }

            Creature* creature = ObjectAccessor::GetCreature(*player, bot.UseItemOnUnitTarget.CreatureGuid);
            if (creature && creature->IsAlive() && !player->IsWithinDistInMap(creature, creature->GetCombatReach() + 4.0f))
            {
                Position standPos;
                float const standDistance = creature->GetCombatReach() + 1.0f;
                if (PlayerbotWalker::PickApproachPosition(player, creature, standDistance, standPos)
                    && bot.Walker.Start(player, standPos, bot.UseItemOnUnitTarget.StopDistance))
                {
                    bot.UseItemOnUnitTarget.Pos = standPos;
                    bot.QuestArriveWaitMs = 0;
                    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} is still short of {} and is walking the rest of the way.",
                        player->GetName(), bot.UseItemOnUnitTarget.CreatureGuid.ToString());
                    return;
                }
            }

            if (bot.QuestArriveWaitMs < 5000)
                return;

            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} arrived but cannot use the quest item on {}. Looking for other work.",
                player->GetName(), bot.UseItemOnUnitTarget.CreatureGuid.ToString());
            bot.UnreachableGuids.insert(bot.UseItemOnUnitTarget.CreatureGuid);
            bot.UseItemOnUnitTarget = {};
            bot.Walker.Reset();
        }
        else
        {
            if (TryImmediateWorld(bot, player, false))
                return;

            bot.QuestArriveWaitMs += diff;
            if (Optional<PlayerbotClient::UseItemOnUnitTarget> found = PlayerbotClient::FindLogIncompleteUseItemOnUnitTarget(player, bot.UnreachableGuids))
            {
                if (!found->CreatureGuid.IsEmpty())
                {
                    BeginUseItemOnUnitTarget(bot, player, *found);
                    return;
                }
            }

            if (bot.QuestArriveWaitMs < QUEST_SEARCH_RETRY_MS)
                return;

            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} reached the map marker but no spawned creature is there yet. Looking for other work.",
                player->GetName());
            int32 const skipQuestId = bot.UseItemOnUnitTarget.QuestId;
            uint32 const skipEntry = bot.UseItemOnUnitTarget.CreditEntry;
            bot.UseItemOnUnitTarget = {};
            bot.Walker.Reset();
            if (TryMapYellow(bot, player, skipQuestId, skipEntry))
                return;
        }
    }

    if (bot.Walker.HasArrived() && bot.CombatTarget.QuestId && bot.CombatTarget.CreatureGuid.IsEmpty())
    {
        if (!PlayerbotClient::CombatTargetStillNeeded(player, bot.CombatTarget))
        {
            ClearCombat(bot, player);
            bot.Walker.Reset();
        }
        else
        {
            if (TryImmediateWorld(bot, player, false))
                return;

            if (bot.QuestArriveWaitMs == 0)
                ClearUnreachable(bot);

            bot.QuestArriveWaitMs += diff;
            if (Optional<PlayerbotClient::CombatTarget> found = PlayerbotClient::FindLogIncompleteMonsterTarget(player, bot.UnreachableGuids))
            {
                if (!found->CreatureGuid.IsEmpty())
                {
                    BeginCombatTarget(bot, player, *found);
                    return;
                }
            }

            if (bot.QuestArriveWaitMs < QUEST_SEARCH_RETRY_MS)
                return;

            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} reached the kill map marker but no spawned creature is there yet. Looking for other work.",
                player->GetName());
            int32 const skipQuestId = bot.CombatTarget.QuestId;
            uint32 const skipEntry = bot.CombatTarget.CreditEntry;
            ClearCombat(bot, player);
            bot.Walker.Reset();
            if (TryMapYellow(bot, player, skipQuestId, skipEntry))
                return;
        }
    }

    if (bot.Walker.HasArrived() && bot.ItemLootTarget.QuestId
        && bot.ItemLootTarget.CreatureGuid.IsEmpty() && bot.ItemLootTarget.GoGuid.IsEmpty())
    {
        if (!PlayerbotClient::ItemLootTargetStillNeeded(player, bot.ItemLootTarget))
        {
            ClearItemLoot(bot);
            bot.Walker.Reset();
        }
        else
        {
            if (TryImmediateWorld(bot, player, false))
                return;

            if (bot.QuestArriveWaitMs == 0)
                ClearUnreachable(bot);

            bot.QuestArriveWaitMs += diff;
            if (Optional<PlayerbotClient::ItemLootTarget> found = PlayerbotClient::FindLogIncompleteItemTarget(player, bot.UnreachableGuids))
            {
                if (!found->CreatureGuid.IsEmpty() || !found->GoGuid.IsEmpty())
                {
                    BeginItemWork(bot, player, *found);
                    return;
                }
            }

            if (bot.QuestArriveWaitMs < QUEST_SEARCH_RETRY_MS)
                return;

            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} reached the item map marker but nothing to loot is there yet. Looking for other work.",
                player->GetName());
            int32 const skipQuestId = bot.ItemLootTarget.QuestId;
            uint32 const skipEntry = bot.ItemLootTarget.ItemId;
            ClearItemLoot(bot);
            bot.Walker.Reset();
            if (TryMapYellow(bot, player, skipQuestId, skipEntry))
                return;
        }
    }

    if (!bot.VendorTarget.NpcGuid.IsEmpty() && !bot.Walker.IsMoving())
    {
        if (UpdateVendor(bot, player, diff))
            return;
    }

    if (bot.Walker.IsMoving())
    {
        TryImmediateWorld(bot, player, true);
        if (bot.Walker.IsMoving()
            && bot.VendorTarget.NpcGuid.IsEmpty()
            && CurrentWalkIsFartherThan(bot, player, QUEST_SEARCH_RANGE)
            && TryBeginVendor(bot, player))
            return;
        return;
    }

    if (retrySameObjective
        && TrySameObjectiveYellow(bot, player, sameObjectiveQuestId, sameObjectiveEntry, sameObjectiveSkipPos, ObjectGuid::Empty))
        return;

    if (TryImmediateWorld(bot, player, false))
        return;

    if (TryBeginVendor(bot, player))
        return;

    if (TryMapYellow(bot, player, skipFailedQuestId, skipFailedEntry))
        return;

    bot.QuestSearchEmptyMs += diff;
    if (bot.QuestSearchEmptyMs >= QUEST_SEARCH_RETRY_MS)
    {
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has no immediate work and no map yellow yet. Still looking (talk {:.0f} yards, kill {:.0f} yards).",
            player->GetName(), QUEST_SEARCH_RANGE, COMBAT_SEARCH_RANGE);
        bot.QuestSearchEmptyMs = 0;
    }
}

void PlayerbotMgr::BeginDeath(PlayerbotRecord& bot, Player* player)
{
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} died. Waiting to release spirit.", player->GetName());
    ClearLivingWork(bot, player);
    ClearDeath(bot);
    bot.Death = PlayerbotDeathWork::WaitToRelease;
    bot.HadSickness = PlayerbotClient::PlayerHasResurrectionSickness(player);
}

void PlayerbotMgr::ClearDeath(PlayerbotRecord& bot)
{
    bot.Death = PlayerbotDeathWork::None;
    bot.DeathWaitMs = 0;
    bot.GhostMs = 0;
    bot.CampedMs = 0;
    bot.HadSickness = false;
    bot.GhostSettled = false;
    bot.RepopSent = false;
    bot.ReclaimSent = false;
    bot.HealerSent = false;
    bot.SitSent = false;
    bot.SpiritReleasePos.Relocate(0.0f, 0.0f, 0.0f, 0.0f);
    bot.SpiritHealerGuid.Clear();
}

void PlayerbotMgr::ClearLivingWork(PlayerbotRecord& bot, Player* player)
{
    if (bot.Walker.IsMoving())
        bot.Walker.Stop(player);

    ClearCombat(bot, player);
    ClearItemLoot(bot);
    ClearVendor(bot);
    bot.VendorRetryMs = 0;
    bot.QuestInteractQueued = false;
    bot.QuestArriveWaitMs = 0;
    bot.QuestInteractWaitMs = 0;
    bot.QuestSearchEmptyMs = 0;
    bot.QuestTarget = {};
    bot.GameObjectTarget = {};
    bot.UseItemOnUnitTarget = {};
    bot.LookedForOtherYellowOnFace = false;
    ClearUnreachable(bot);
    bot.Walker.Reset();
}

bool PlayerbotMgr::UpdateSitRecover(PlayerbotRecord& bot, Player* player, uint32 diff)
{
    if (!player->GetSession())
        return false;

    if (player->IsInCombat() || PlayerbotClient::FindAttackerTarget(player))
    {
        if (player->IsSitState())
        {
            PlayerbotClient::QueueStandStateChange(player->GetSession(), UNIT_STAND_STATE_STAND);
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} stood up; combat started before health was full.", player->GetName());
        }
        ClearDeath(bot);
        return false;
    }

    if (player->GetHealth() >= player->GetMaxHealth())
    {
        if (player->IsSitState())
        {
            PlayerbotClient::QueueStandStateChange(player->GetSession(), UNIT_STAND_STATE_STAND);
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} health is full. Standing and returning to the living brain.", player->GetName());
        }
        ClearDeath(bot);
        return false;
    }

    if (!player->IsSitState())
    {
        bot.DeathWaitMs += diff;
        if (!bot.SitSent || bot.DeathWaitMs >= PACKET_RETRY_MS)
        {
            PlayerbotClient::QueueStandStateChange(player->GetSession(), UNIT_STAND_STATE_SIT);
            bot.SitSent = true;
            bot.DeathWaitMs = 0;
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_STAND_STATE_CHANGE sit until health is full.", player->GetName());
        }
    }

    return true;
}

bool PlayerbotMgr::BeginCorpseWalk(PlayerbotRecord& bot, Player* player)
{
    if (!player->HasCorpse() || player->GetCorpseLocation().GetMapId() != player->GetMapId())
        return false;

    Optional<Position> standPos = PlayerbotClient::PickCorpseStandPosition(player);
    if (!standPos)
        return false;

    if (bot.Walker.IsMoving())
        bot.Walker.Stop(player);

    bot.Walker.Reset();
    bot.Death = PlayerbotDeathWork::WalkToCorpse;
    bot.DeathWaitMs = 0;
    bot.CampedMs = 0;
    bot.ReclaimSent = false;
    bot.QuestArriveWaitMs = 0;

    bool const hot = PlayerbotClient::HostilesWouldAggroAt(player, player->GetCorpseLocation());
    if (hot)
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} the reclaim circle is hot. Ghost-walking to the side of the corpse.", player->GetName());
    else
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} ghost-walking to the corpse.", player->GetName());

    if (player->GetExactDist(*standPos) <= 0.25f)
    {
        bot.Death = PlayerbotDeathWork::WaitToReclaim;
        return true;
    }

    if (!bot.Walker.Start(player, *standPos, 0.25f))
        return false;

    return true;
}

bool PlayerbotMgr::BeginHealerWalk(PlayerbotRecord& bot, Player* player)
{
    if (bot.Walker.IsMoving())
        bot.Walker.Stop(player);

    bot.Walker.Reset();
    bot.Death = PlayerbotDeathWork::WalkToHealer;
    bot.DeathWaitMs = 0;
    bot.HealerSent = false;
    bot.QuestArriveWaitMs = 0;
    bot.SpiritHealerGuid.Clear();

    Position nearPos = bot.SpiritReleasePos;
    if (nearPos.GetPositionX() == 0.0f && nearPos.GetPositionY() == 0.0f)
        nearPos = player->GetPosition();

    Optional<PlayerbotClient::SpiritHealerTarget> healer = PlayerbotClient::FindSpiritHealer(player, nearPos);
    if (healer)
    {
        bot.SpiritHealerGuid = healer->NpcGuid;
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking to spirit healer {}.",
            player->GetName(), healer->NpcGuid.ToString());

        if (player->GetExactDist(healer->Pos) <= healer->StopDistance)
        {
            bot.Death = PlayerbotDeathWork::WaitToHeal;
            return true;
        }

        if (bot.Walker.Start(player, healer->Pos, healer->StopDistance))
            return true;

        bot.SpiritHealerGuid.Clear();
    }

    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking back to the graveyard for the spirit healer.", player->GetName());
    if (player->GetExactDist(nearPos) <= 2.0f)
        return true;

    return bot.Walker.Start(player, nearPos, 2.0f);
}

bool PlayerbotMgr::UpdateDeath(PlayerbotRecord& bot, Player* player, uint32 diff)
{
    if (!player)
        return false;

    if (player->IsAlive())
    {
        if (bot.Death == PlayerbotDeathWork::None)
            return false;

        if (bot.Death == PlayerbotDeathWork::SitRecover)
            return UpdateSitRecover(bot, player, diff);

        if (bot.Walker.IsMoving())
            bot.Walker.Stop(player);

        bot.Death = PlayerbotDeathWork::SitRecover;
        bot.SitSent = false;
        bot.DeathWaitMs = 0;
        bot.Walker.Reset();
        uint64 const maxHealth = player->GetMaxHealth() ? player->GetMaxHealth() : 1;
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} is alive at {:.0f}% health. Sitting to recover.",
            player->GetName(), 100.0f * float(player->GetHealth()) / float(maxHealth));
        return UpdateSitRecover(bot, player, diff);
    }

    if (bot.Death == PlayerbotDeathWork::None || bot.Death == PlayerbotDeathWork::SitRecover)
        BeginDeath(bot, player);

    bool const isGhost = player->HasPlayerFlag(PLAYER_FLAGS_GHOST);

    if (!isGhost)
    {
        bot.DeathWaitMs += diff;
        if (bot.RepopSent)
        {
            if (bot.DeathWaitMs >= PACKET_RETRY_MS)
            {
                bot.RepopSent = false;
                bot.DeathWaitMs = 0;
            }
            return true;
        }

        if (bot.DeathWaitMs < RELEASE_WAIT_MS)
            return true;

        PlayerbotClient::QueueRepopRequest(player->GetSession());
        bot.RepopSent = true;
        bot.DeathWaitMs = 0;
        bot.Death = PlayerbotDeathWork::WaitForGhost;
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_REPOP_REQUEST.", player->GetName());
        return true;
    }

    bot.GhostMs += diff;

    if (player->IsBeingTeleported())
        return true;

    if (!bot.GhostSettled)
    {
        bot.DeathWaitMs += diff;
        bot.Death = PlayerbotDeathWork::WaitForGhost;
        if (bot.DeathWaitMs < GHOST_SETTLE_MS)
            return true;

        bot.SpiritReleasePos = player->GetPosition();
        bot.GhostSettled = true;
        bot.DeathWaitMs = 0;
    }

    if (bot.Death == PlayerbotDeathWork::WaitForGhost)
    {
        if (bot.HadSickness || PlayerbotClient::PlayerHasResurrectionSickness(player))
        {
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} already has resurrection sickness. Using the spirit healer.", player->GetName());
            if (!BeginHealerWalk(bot, player))
            {
                bot.Death = PlayerbotDeathWork::WalkToHealer;
                bot.DeathWaitMs = 0;
            }
            return true;
        }

        if (!BeginCorpseWalk(bot, player))
        {
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot corpse walk. Using the spirit healer.", player->GetName());
            if (!BeginHealerWalk(bot, player))
            {
                bot.Death = PlayerbotDeathWork::WalkToHealer;
                bot.DeathWaitMs = 0;
            }
        }
        return true;
    }

    if (bot.Walker.IsMoving())
        bot.Walker.Update(player, diff);

    if (bot.Death == PlayerbotDeathWork::WalkToCorpse)
    {
        if (bot.Walker.HasFailed() || bot.GhostMs >= GHOST_GIVE_UP_MS)
        {
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} corpse walk failed or stuck. Using the spirit healer.", player->GetName());
            BeginHealerWalk(bot, player);
            return true;
        }

        if (bot.Walker.HasArrived() || bot.Walker.IsIdle())
        {
            bot.Death = PlayerbotDeathWork::WaitToReclaim;
            bot.DeathWaitMs = 0;
            bot.CampedMs = 0;
            bot.ReclaimSent = false;
        }
        return true;
    }

    if (bot.Death == PlayerbotDeathWork::WaitToReclaim)
    {
        if (bot.GhostMs >= GHOST_WAIT_LONG_MS)
        {
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has been a ghost a long time. Using the spirit healer.", player->GetName());
            BeginHealerWalk(bot, player);
            return true;
        }

        if (PlayerbotClient::HostilesWouldAggroAt(player, *player))
        {
            bot.CampedMs += diff;
            Optional<Position> side = PlayerbotClient::PickCorpseStandPosition(player);
            if (side && player->GetExactDist(*side) > 1.0f && !bot.Walker.IsMoving())
            {
                TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} the reclaim circle is still hot. Standing to the side.", player->GetName());
                if (bot.Walker.Start(player, *side, 0.25f))
                {
                    bot.Death = PlayerbotDeathWork::WalkToCorpse;
                    return true;
                }
            }

            if (bot.CampedMs >= CAMPED_WAIT_MS)
            {
                TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} body is camped. Using the spirit healer.", player->GetName());
                BeginHealerWalk(bot, player);
                return true;
            }
            return true;
        }

        bot.CampedMs = 0;

        if (!PlayerbotClient::IsWithinCorpseReclaimRange(player))
        {
            if (!bot.Walker.IsMoving())
            {
                Optional<Position> standPos = PlayerbotClient::PickCorpseStandPosition(player);
                if (standPos && bot.Walker.Start(player, *standPos, 0.25f))
                    bot.Death = PlayerbotDeathWork::WalkToCorpse;
            }
            return true;
        }

        if (bot.ReclaimSent)
        {
            bot.DeathWaitMs += diff;
            if (bot.DeathWaitMs >= PACKET_RETRY_MS)
            {
                bot.ReclaimSent = false;
                bot.DeathWaitMs = 0;
            }
            return true;
        }

        if (!PlayerbotClient::CorpseReclaimDelayFinished(player) || !PlayerbotClient::IsWithinCorpseReclaimRange(player))
            return true;

        Corpse* corpse = player->GetCorpse();
        if (!corpse)
        {
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has no corpse to reclaim. Using the spirit healer.", player->GetName());
            BeginHealerWalk(bot, player);
            return true;
        }

        PlayerbotClient::QueueReclaimCorpse(player->GetSession(), corpse->GetGUID());
        bot.ReclaimSent = true;
        bot.DeathWaitMs = 0;
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_RECLAIM_CORPSE.", player->GetName());
        return true;
    }

    if (bot.Death == PlayerbotDeathWork::WalkToHealer)
    {
        if (bot.Walker.HasFailed())
        {
            bot.Walker.Reset();
            bot.DeathWaitMs = 0;
        }

        if (bot.Walker.IsMoving())
            return true;

        if (!bot.SpiritHealerGuid.IsEmpty())
        {
            bot.Death = PlayerbotDeathWork::WaitToHeal;
            bot.DeathWaitMs = 0;
            bot.HealerSent = false;
            return true;
        }

        if (Optional<PlayerbotClient::SpiritHealerTarget> healer = PlayerbotClient::FindSpiritHealer(player, player->GetPosition()))
        {
            bot.SpiritHealerGuid = healer->NpcGuid;
            if (player->GetExactDist(healer->Pos) <= healer->StopDistance)
            {
                bot.Death = PlayerbotDeathWork::WaitToHeal;
                bot.DeathWaitMs = 0;
                bot.HealerSent = false;
                return true;
            }
            if (bot.Walker.Start(player, healer->Pos, healer->StopDistance))
                return true;
            bot.SpiritHealerGuid.Clear();
        }

        bot.DeathWaitMs += diff;
        if (bot.DeathWaitMs >= QUEST_SEARCH_RETRY_MS)
        {
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has not found a spirit healer yet. Still looking.", player->GetName());
            bot.DeathWaitMs = 0;
            BeginHealerWalk(bot, player);
        }
        return true;
    }

    if (bot.Death == PlayerbotDeathWork::WaitToHeal)
    {
        if (bot.HealerSent)
        {
            bot.DeathWaitMs += diff;
            if (bot.DeathWaitMs >= PACKET_RETRY_MS)
            {
                bot.HealerSent = false;
                bot.DeathWaitMs = 0;
            }
            return true;
        }

        if (PlayerbotClient::TrySpiritHealer(player, bot.SpiritHealerGuid))
        {
            bot.HealerSent = true;
            bot.DeathWaitMs = 0;
            return true;
        }

        bot.DeathWaitMs += diff;
        Creature* healer = ObjectAccessor::GetCreature(*player, bot.SpiritHealerGuid);
        if (healer && healer->IsAlive()
            && !player->IsWithinDistInMap(healer, healer->GetCombatReach() + 4.0f))
        {
            Position standPos;
            float const standDistance = healer->GetCombatReach() + 1.0f;
            if (PlayerbotWalker::PickApproachPosition(player, healer, standDistance, standPos)
                && bot.Walker.Start(player, standPos, 0.25f))
            {
                bot.Death = PlayerbotDeathWork::WalkToHealer;
                bot.DeathWaitMs = 0;
                return true;
            }
        }

        if (bot.DeathWaitMs >= QUEST_SEARCH_RETRY_MS)
        {
            bot.DeathWaitMs = 0;
            BeginHealerWalk(bot, player);
        }
        return true;
    }

    return true;
}

void PlayerbotMgr::RecoverFailedWalk(PlayerbotRecord& bot, Player* player)
{
    if (bot.GameObjectTarget.QuestId)
    {
        RememberFailedYellow(bot, bot.GameObjectTarget.Pos);
        if (!bot.GameObjectTarget.GoGuid.IsEmpty())
            bot.UnreachableGuids.insert(bot.GameObjectTarget.GoGuid);
        bot.GameObjectTarget = {};
    }
    else if (bot.UseItemOnUnitTarget.QuestId)
    {
        RememberFailedYellow(bot, bot.UseItemOnUnitTarget.Pos);
        if (!bot.UseItemOnUnitTarget.CreatureGuid.IsEmpty())
            bot.UnreachableGuids.insert(bot.UseItemOnUnitTarget.CreatureGuid);
        bot.UseItemOnUnitTarget = {};
    }
    else if (bot.CombatTarget.QuestId || !bot.CombatTarget.CreatureGuid.IsEmpty())
    {
        if (KeepCombatAfterFailedWalk(player, bot.CombatTarget.CreatureGuid))
            LogStayOnCombatWalkFail(player, bot.CombatTarget.CreatureGuid);
        else
        {
            RememberFailedYellow(bot, bot.CombatTarget.Pos);
            if (!bot.CombatTarget.CreatureGuid.IsEmpty())
                bot.UnreachableGuids.insert(bot.CombatTarget.CreatureGuid);
            ClearCombat(bot, player);
        }
    }
    else if (bot.ItemLootTarget.QuestId || bot.ItemLootTarget.LootCorpse)
    {
        RememberFailedYellow(bot, bot.ItemLootTarget.Pos);
        if (!bot.ItemLootTarget.GoGuid.IsEmpty())
            bot.UnreachableGuids.insert(bot.ItemLootTarget.GoGuid);
        else if (!bot.ItemLootTarget.CreatureGuid.IsEmpty())
            bot.UnreachableGuids.insert(bot.ItemLootTarget.CreatureGuid);
        ClearItemLoot(bot);
    }
    else if (!bot.VendorTarget.NpcGuid.IsEmpty())
    {
        bot.UnreachableGuids.insert(bot.VendorTarget.NpcGuid);
        ClearVendor(bot);
    }
    else if (!bot.QuestTarget.NpcGuid.IsEmpty())
        bot.UnreachableGuids.insert(bot.QuestTarget.NpcGuid);

    bot.QuestTarget = {};
    bot.LookedForOtherYellowOnFace = false;
    bot.Walker.Reset();
}

bool PlayerbotMgr::TryImmediateWorld(PlayerbotRecord& bot, Player* player, bool walking)
{
    Optional<PlayerbotClient::QuestTarget> talk;
    if (!walking || !CurrentWalkIsInReach(bot, player))
        talk = PlayerbotClient::FindNearbyQuestTarget(player, QUEST_SEARCH_RANGE, PlayerbotClient::QuestSearchKind::Talk, bot.UnreachableGuids);

    Optional<PlayerbotClient::ItemLootTarget> loot = PlayerbotClient::FindNearbyItemLootTarget(player, LOOT_SEARCH_RANGE, bot.UnreachableGuids, walking);
    Optional<PlayerbotClient::GameObjectTarget> go;
    Optional<PlayerbotClient::UseItemOnUnitTarget> useItem;
    Optional<PlayerbotClient::CombatTarget> kill;
    if (!walking)
    {
        go = PlayerbotClient::FindNearbyGameObjectObjectiveTarget(player, LOOT_SEARCH_RANGE, bot.UnreachableGuids, false);
        useItem = PlayerbotClient::FindNearbyUseItemOnUnitTarget(player, LOOT_SEARCH_RANGE, bot.UnreachableGuids, true);
        kill = PlayerbotClient::FindNearbyMonsterObjectiveTarget(player, COMBAT_SEARCH_RANGE, bot.UnreachableGuids);
    }

    if (talk && talk->NpcGuid == bot.QuestTarget.NpcGuid)
        talk.reset();
    if (loot && ((!bot.ItemLootTarget.CreatureGuid.IsEmpty() && loot->CreatureGuid == bot.ItemLootTarget.CreatureGuid)
        || (!bot.ItemLootTarget.GoGuid.IsEmpty() && loot->GoGuid == bot.ItemLootTarget.GoGuid)))
        loot.reset();
    if (go && !bot.GameObjectTarget.GoGuid.IsEmpty() && go->GoGuid == bot.GameObjectTarget.GoGuid)
        go.reset();
    if (useItem && !bot.UseItemOnUnitTarget.CreatureGuid.IsEmpty() && useItem->CreatureGuid == bot.UseItemOnUnitTarget.CreatureGuid)
        useItem.reset();
    if (kill && !bot.CombatTarget.CreatureGuid.IsEmpty() && kill->CreatureGuid == bot.CombatTarget.CreatureGuid)
        kill.reset();

    float bestDist = std::numeric_limits<float>::max();
    uint8 kind = 0;
    auto consider = [&](float dist, uint8 nextKind)
    {
        if (dist >= bestDist)
            return;
        bestDist = dist;
        kind = nextKind;
    };

    if (talk)
        consider(player->GetExactDist(talk->Pos), 1);
    if (loot)
        consider(player->GetExactDist(loot->Pos), 2);
    if (go)
        consider(player->GetExactDist(go->Pos), 3);
    if (useItem)
        consider(player->GetExactDist(useItem->Pos), 4);
    if (kill)
        consider(player->GetExactDist(kill->Pos), 5);

    if (!kind)
        return false;

    if (kind == 1)
        return BeginQuestTarget(bot, player, *talk);
    if (kind == 2)
        return BeginItemWork(bot, player, *loot);
    if (kind == 3)
        return BeginGameObjectTarget(bot, player, *go);
    if (kind == 4)
        return BeginUseItemOnUnitTarget(bot, player, *useItem);
    return BeginCombatTarget(bot, player, *kill);
}

bool PlayerbotMgr::TryClickFromHere(PlayerbotRecord& bot, Player* player)
{
    if (!player)
        return false;

    // Same interact helpers the arrive path uses. Do not require the 0.25-yard stand pin.

    if (!bot.QuestTarget.NpcGuid.IsEmpty())
    {
        if (!PlayerbotClient::TryInteractQuest(player, bot.QuestTarget))
            return false;

        StopWalkToClick(bot, player, bot.QuestTarget.NpcGuid);
        bot.QuestInteractQueued = true;
        bot.QuestInteractWaitMs = 0;
        return true;
    }

    if (!bot.GameObjectTarget.GoGuid.IsEmpty())
    {
        if (!PlayerbotClient::TryUseGameObject(player, bot.GameObjectTarget))
            return false;

        StopWalkToClick(bot, player, bot.GameObjectTarget.GoGuid);
        bot.QuestInteractQueued = true;
        bot.QuestInteractWaitMs = 0;
        return true;
    }

    if (!bot.UseItemOnUnitTarget.CreatureGuid.IsEmpty())
    {
        if (!PlayerbotClient::TryUseItemOnUnit(player, bot.UseItemOnUnitTarget))
            return false;

        StopWalkToClick(bot, player, bot.UseItemOnUnitTarget.CreatureGuid);
        bot.QuestInteractQueued = true;
        bot.QuestInteractWaitMs = 0;
        return true;
    }

    if (bot.ItemLootTarget.LootCorpse
        || (bot.ItemLootTarget.QuestId && !bot.ItemLootTarget.GoGuid.IsEmpty()))
    {
        bool canClick = false;
        ObjectGuid clickGuid;
        if (!bot.ItemLootTarget.GoGuid.IsEmpty())
        {
            clickGuid = bot.ItemLootTarget.GoGuid;
            canClick = player->GetGameObjectIfCanInteractWith(clickGuid) != nullptr;
        }
        else if (!bot.ItemLootTarget.CreatureGuid.IsEmpty())
        {
            clickGuid = bot.ItemLootTarget.CreatureGuid;
            Creature* creature = ObjectAccessor::GetCreature(*player, clickGuid);
            canClick = InInteractRange(player, creature);
        }

        if (!canClick)
            return false;

        StopWalkToClick(bot, player, clickGuid);
        bot.QuestArriveWaitMs = 0;
        return UpdateItemLoot(bot, player, 0);
    }

    if (!bot.VendorTarget.NpcGuid.IsEmpty())
    {
        if (!PlayerbotClient::TryOpenVendor(player, bot.VendorTarget.NpcGuid))
            return false;

        StopWalkToClick(bot, player, bot.VendorTarget.NpcGuid);
        bot.VendorListSent = true;
        bot.QuestArriveWaitMs = 0;
        return true;
    }

    return false;
}

bool PlayerbotMgr::TryMapYellow(PlayerbotRecord& bot, Player* player, int32 skipQuestId, uint32 skipEntry)
{
    PlayerbotClient::MapYellowFilter filter = MakeMapYellowFilter(bot, skipQuestId, skipEntry, false, nullptr);

    for (int32 attempt = 0; attempt < 5; ++attempt)
    {
        Optional<PlayerbotClient::GameObjectTarget> go = PlayerbotClient::FindLogIncompleteGameObjectTarget(player, bot.UnreachableGuids, filter);
        Optional<PlayerbotClient::UseItemOnUnitTarget> useItem = PlayerbotClient::FindLogIncompleteUseItemOnUnitTarget(player, bot.UnreachableGuids, filter);
        Optional<PlayerbotClient::CombatTarget> kill = PlayerbotClient::FindLogIncompleteMonsterTarget(player, bot.UnreachableGuids, filter);
        Optional<PlayerbotClient::ItemLootTarget> item = PlayerbotClient::FindLogIncompleteItemTarget(player, bot.UnreachableGuids, filter);
        Optional<PlayerbotClient::QuestTarget> turnIn = PlayerbotClient::FindLogCompleteTurnIn(player, bot.UnreachableGuids, skipQuestId);

        float bestDist = std::numeric_limits<float>::max();
        uint8 kind = 0;
        auto consider = [&](float dist, uint8 nextKind)
        {
            if (dist >= bestDist)
                return;
            bestDist = dist;
            kind = nextKind;
        };

        if (go)
            consider(player->GetExactDist(go->Pos), 1);
        if (useItem)
            consider(player->GetExactDist(useItem->Pos), 2);
        if (kill)
            consider(player->GetExactDist(kill->Pos), 3);
        if (item)
            consider(player->GetExactDist(item->Pos), 4);
        if (turnIn)
            consider(player->GetExactDist(turnIn->Pos), 5);

        if (!kind)
            return false;

        bool started = false;
        Position failedPos;
        if (kind == 1)
        {
            failedPos = go->Pos;
            started = BeginGameObjectTarget(bot, player, *go);
        }
        else if (kind == 2)
        {
            failedPos = useItem->Pos;
            started = BeginUseItemOnUnitTarget(bot, player, *useItem);
        }
        else if (kind == 3)
        {
            failedPos = kill->Pos;
            started = BeginCombatTarget(bot, player, *kill);
        }
        else if (kind == 4)
        {
            failedPos = item->Pos;
            started = BeginItemWork(bot, player, *item);
        }
        else
        {
            failedPos = turnIn->Pos;
            started = BeginQuestTarget(bot, player, *turnIn);
        }

        if (started)
            return true;

        RememberFailedYellow(bot, failedPos);
    }

    return false;
}

bool PlayerbotMgr::TrySameObjectiveYellow(PlayerbotRecord& bot, Player* player, int32 questId, uint32 entry, Position const& skipPos, ObjectGuid extraSkipGuid)
{
    if (!player || !questId)
        return false;

    std::unordered_set<ObjectGuid> skip = bot.UnreachableGuids;
    if (!extraSkipGuid.IsEmpty())
        skip.insert(extraSkipGuid);

    PlayerbotClient::MapYellowFilter filter = MakeMapYellowFilter(bot, questId, entry, true, &skipPos);

    Optional<PlayerbotClient::GameObjectTarget> go = PlayerbotClient::FindLogIncompleteGameObjectTarget(player, skip, filter);
    Optional<PlayerbotClient::UseItemOnUnitTarget> useItem = PlayerbotClient::FindLogIncompleteUseItemOnUnitTarget(player, skip, filter);
    Optional<PlayerbotClient::CombatTarget> kill = PlayerbotClient::FindLogIncompleteMonsterTarget(player, skip, filter);
    Optional<PlayerbotClient::ItemLootTarget> item = PlayerbotClient::FindLogIncompleteItemTarget(player, skip, filter);

    float bestDist = std::numeric_limits<float>::max();
    uint8 kind = 0;
    auto consider = [&](float dist, uint8 nextKind)
    {
        if (dist >= bestDist)
            return;
        bestDist = dist;
        kind = nextKind;
    };

    if (go)
        consider(player->GetExactDist(go->Pos), 1);
    if (useItem)
        consider(player->GetExactDist(useItem->Pos), 2);
    if (kill)
        consider(player->GetExactDist(kill->Pos), 3);
    if (item)
        consider(player->GetExactDist(item->Pos), 4);

    if (!kind)
        return false;

    ObjectGuid foundGuid;
    Position foundPos;
    if (kind == 1)
    {
        foundGuid = go->GoGuid;
        foundPos = go->Pos;
    }
    else if (kind == 2)
    {
        foundGuid = useItem->CreatureGuid;
        foundPos = useItem->Pos;
    }
    else if (kind == 3)
    {
        foundGuid = kill->CreatureGuid;
        foundPos = kill->Pos;
    }
    else
    {
        foundGuid = !item->GoGuid.IsEmpty() ? item->GoGuid : item->CreatureGuid;
        foundPos = item->Pos;
    }

    if (!extraSkipGuid.IsEmpty() && foundGuid == extraSkipGuid)
        return false;
    if (foundGuid.IsEmpty() && skipPos.GetExactDist(foundPos) <= 5.0f)
        return false;

    // Finder already skipped extraSkipGuid for this pick. Do not mark that spawn unreachable:
    // mmap's first step was steep; she has not failed the walk around it.
    if (!extraSkipGuid.IsEmpty())
    {
        char const* what = foundGuid.IsEmpty() ? "another yellow of the same objective" : "another spawn of the same objective";
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} this approach is a face. Walking to {}.",
            player->GetName(), what);
    }

    if (kind == 1)
        return BeginGameObjectTarget(bot, player, *go);
    if (kind == 2)
        return BeginUseItemOnUnitTarget(bot, player, *useItem);
    if (kind == 3)
        return BeginCombatTarget(bot, player, *kill);
    return BeginItemWork(bot, player, *item);
}

bool PlayerbotMgr::TryLeaveFaceForOtherYellow(PlayerbotRecord& bot, Player* player)
{
    if (!player)
        return false;

    if (KeepCombatAfterFailedWalk(player, bot.CombatTarget.CreatureGuid))
        return false;

    int32 questId = 0;
    uint32 entry = 0;
    Position skipPos;
    ObjectGuid skipGuid;

    if (bot.GameObjectTarget.QuestId)
    {
        questId = bot.GameObjectTarget.QuestId;
        entry = bot.GameObjectTarget.GoEntry;
        skipPos = bot.GameObjectTarget.Pos;
        skipGuid = bot.GameObjectTarget.GoGuid;
    }
    else if (bot.UseItemOnUnitTarget.QuestId)
    {
        questId = bot.UseItemOnUnitTarget.QuestId;
        entry = bot.UseItemOnUnitTarget.CreditEntry;
        skipPos = bot.UseItemOnUnitTarget.Pos;
        skipGuid = bot.UseItemOnUnitTarget.CreatureGuid;
    }
    else if (bot.CombatTarget.QuestId)
    {
        questId = bot.CombatTarget.QuestId;
        entry = bot.CombatTarget.CreditEntry;
        skipPos = bot.CombatTarget.Pos;
        skipGuid = bot.CombatTarget.CreatureGuid;
    }
    else if (bot.ItemLootTarget.QuestId)
    {
        questId = bot.ItemLootTarget.QuestId;
        entry = bot.ItemLootTarget.ItemId;
        skipPos = bot.ItemLootTarget.Pos;
        skipGuid = !bot.ItemLootTarget.GoGuid.IsEmpty() ? bot.ItemLootTarget.GoGuid : bot.ItemLootTarget.CreatureGuid;
    }
    else
        return false;

    return TrySameObjectiveYellow(bot, player, questId, entry, skipPos, skipGuid);
}

bool PlayerbotMgr::BeginItemWork(PlayerbotRecord& bot, Player* player, PlayerbotClient::ItemLootTarget const& target)
{
    if (!target.CreatureGuid.IsEmpty() && !target.LootCorpse)
        return BeginCombatTarget(bot, player, PlayerbotClient::CombatTargetFromItemLoot(target));
    return BeginItemLootTarget(bot, player, target);
}

void PlayerbotMgr::ClearCombat(PlayerbotRecord& bot, Player* player)
{
    if (player && player->GetSession() && (!bot.CombatTarget.CreatureGuid.IsEmpty() || player->HasUnitState(UNIT_STATE_MELEE_ATTACKING)))
        PlayerbotClient::QueueAttackStop(player->GetSession());

    bot.CombatTarget = {};
    bot.CombatSwingSent = false;
    bot.CombatCastSpellId = 0;
    bot.CombatCastPending = false;
    bot.CombatCastWaitMs = 0;
    bot.CombatFacingWait = false;
}

bool PlayerbotMgr::UpdateCombat(PlayerbotRecord& bot, Player* player, uint32 diff)
{
    Creature* creature = ObjectAccessor::GetCreature(*player, bot.CombatTarget.CreatureGuid);
    if (!creature)
    {
        ClearCombat(bot, player);
        bot.Walker.Reset();
        return false;
    }

    if (!creature->IsAlive())
    {
        if (player->isAllowedToLoot(creature))
        {
            Optional<PlayerbotClient::ItemLootTarget> loot = PlayerbotClient::MakeCorpseLootTarget(player, creature);
            ClearCombat(bot, player);
            if (!loot)
            {
                bot.UnreachableGuids.insert(creature->GetGUID());
                bot.Walker.Reset();
                return false;
            }

            bot.ItemLootTarget = *loot;
            bot.LootOpenSent = false;
            bot.Walker.Reset();
            return true;
        }

        ClearCombat(bot, player);
        bot.Walker.Reset();
        return false;
    }

    if (!player->IsValidAttackTarget(creature))
    {
        ClearCombat(bot, player);
        bot.Walker.Reset();
        return false;
    }

    bool const inMelee = player->IsWithinMeleeRange(creature);
    if (!inMelee)
        bot.CombatSwingSent = false;

    auto swingIfMelee = [&]()
    {
        if (!inMelee)
            return;

        if (bot.Walker.IsMoving())
            bot.Walker.Stop(player);

        if (player->GetVictim() == creature && player->HasUnitState(UNIT_STATE_MELEE_ATTACKING))
            return;

        bool const reswing = bot.CombatSwingSent;
        if (PlayerbotClient::TryMeleeAttack(player, bot.CombatTarget.CreatureGuid))
        {
            if (reswing)
                TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} re-queued CMSG_ATTACK_SWING on {} because auto-attack was not running.",
                    player->GetName(), bot.CombatTarget.CreatureGuid.ToString());
            bot.CombatSwingSent = true;
        }
    };

    auto failCloseInWalk = [&]()
    {
        if (KeepCombatAfterFailedWalk(player, bot.CombatTarget.CreatureGuid))
        {
            LogStayOnCombatWalkFail(player, bot.CombatTarget.CreatureGuid);
            bot.Walker.Reset();
            swingIfMelee();
            return true;
        }

        bot.UnreachableGuids.insert(bot.CombatTarget.CreatureGuid);
        ClearCombat(bot, player);
        bot.Walker.Reset();
        return false;
    };

    auto startMeleeApproach = [&]()
    {
        if (bot.Walker.IsMoving())
            return true;

        Position standPos;
        float const standDistance = creature->GetCombatReach() + 1.0f;
        if (!PlayerbotWalker::PickApproachPosition(player, creature, standDistance, standPos))
            return failCloseInWalk();

        bot.CombatTarget.Pos = standPos;
        if (player->GetExactDist(standPos) <= bot.CombatTarget.StopDistance)
            return failCloseInWalk();

        if (!bot.Walker.Start(player, standPos, bot.CombatTarget.StopDistance))
            return failCloseInWalk();

        return true;
    };

    // Facing packet from last tick has already been processed this world update.
    if (bot.CombatFacingWait)
        bot.CombatFacingWait = false;

    if (bot.CombatCastPending)
    {
        if (PlayerbotClient::CombatCastHasStarted(player, bot.CombatCastSpellId))
        {
            bot.CombatCastPending = false;
            bot.CombatCastWaitMs = 0;
        }
        else
        {
            bot.CombatCastWaitMs += diff;
            if (bot.CombatCastWaitMs < COMBAT_CAST_RETRY_MS)
            {
                swingIfMelee();
                return true;
            }

            bot.CombatCastPending = false;
            bot.CombatCastSpellId = 0;
            bot.CombatCastWaitMs = 0;
        }
    }

    PlayerbotClient::CombatSpellPick const pick = PlayerbotClient::PickCombatDamageSpell(player, creature);

    if (pick.Press)
    {
        if (bot.Walker.IsMoving())
        {
            bot.Walker.Stop(player);
            swingIfMelee();
            return true;
        }

        if (PlayerbotClient::TryCombatCast(player, bot.CombatTarget.CreatureGuid, pick.Press->Id))
        {
            bot.CombatCastSpellId = pick.Press->Id;
            bot.CombatCastPending = true;
            bot.CombatCastWaitMs = 0;
        }

        swingIfMelee();
        return true;
    }

    if (pick.Face)
    {
        if (bot.Walker.IsMoving())
        {
            bot.Walker.Stop(player);
            swingIfMelee();
            return true;
        }

        PlayerbotClient::QueueSetFacing(player, creature);
        bot.CombatFacingWait = true;
        swingIfMelee();
        return true;
    }

    if (pick.Approach && !PlayerbotClient::CombatSpellIsMelee(pick.Approach) && !pick.WalkCloser)
    {
        if (bot.Walker.IsMoving())
        {
            swingIfMelee();
            return true;
        }

        Position dest = creature->GetPosition();
        float const stop = std::max(1.0f, PlayerbotClient::CombatSpellMaxRange(player, creature, pick.Approach) - 1.0f);
        bot.CombatTarget.Pos = dest;
        if (player->GetExactDist(dest) <= stop)
        {
            swingIfMelee();
            return true;
        }

        if (!bot.Walker.Start(player, dest, stop))
            return failCloseInWalk();

        return true;
    }

    if (pick.KnownInRange && !pick.WalkCloser)
    {
        if (bot.Walker.IsMoving())
            bot.Walker.Stop(player);
        swingIfMelee();
        return true;
    }

    if (inMelee)
    {
        swingIfMelee();
        return true;
    }

    if (bot.CombatTarget.QuestId && !PlayerbotClient::CombatTargetStillNeeded(player, bot.CombatTarget))
    {
        ClearCombat(bot, player);
        bot.Walker.Reset();
        return false;
    }

    return startMeleeApproach();
}

bool PlayerbotMgr::BeginQuestTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::QuestTarget const& target)
{
    if (bot.Walker.IsMoving())
        bot.Walker.Stop(player);

    ClearCombat(bot, player);
    ClearItemLoot(bot);
    ClearVendor(bot);
    bot.QuestSearchEmptyMs = 0;
    bot.QuestTarget = target;
    bot.GameObjectTarget = {};
    bot.UseItemOnUnitTarget = {};
    bot.CombatTarget = {};
    bot.LookedForOtherYellowOnFace = false;
    bot.Walker.Reset();

    if (TryClickFromHere(bot, player))
        return true;

    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking to {} for quest {} ({}).",
        player->GetName(), bot.QuestTarget.NpcGuid.ToString(), bot.QuestTarget.QuestId,
        bot.QuestTarget.TurnIn ? "turn-in" : "accept");
    bot.QuestArriveWaitMs = 0;
    if (!bot.Walker.Start(player, bot.QuestTarget.Pos, bot.QuestTarget.StopDistance))
    {
        if (!bot.QuestTarget.NpcGuid.IsEmpty())
            bot.UnreachableGuids.insert(bot.QuestTarget.NpcGuid);
        bot.QuestTarget = {};
        bot.Walker.Reset();
        return false;
    }

    return true;
}

bool PlayerbotMgr::BeginGameObjectTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::GameObjectTarget const& target)
{
    if (bot.Walker.IsMoving())
        bot.Walker.Stop(player);

    ClearCombat(bot, player);
    ClearItemLoot(bot);
    ClearVendor(bot);
    bot.QuestSearchEmptyMs = 0;
    bot.QuestTarget = {};
    bot.GameObjectTarget = target;
    bot.UseItemOnUnitTarget = {};
    bot.CombatTarget = {};
    bot.LookedForOtherYellowOnFace = false;
    bot.Walker.Reset();

    if (TryClickFromHere(bot, player))
        return true;

    if (!bot.GameObjectTarget.GoGuid.IsEmpty())
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking to {} for quest {} (use).",
            player->GetName(), bot.GameObjectTarget.GoGuid.ToString(), bot.GameObjectTarget.QuestId);
    else
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking to the gameobject map marker for quest {}.",
            player->GetName(), bot.GameObjectTarget.QuestId);

    bot.QuestArriveWaitMs = 0;
    if (!bot.Walker.Start(player, bot.GameObjectTarget.Pos, bot.GameObjectTarget.StopDistance))
    {
        if (!bot.GameObjectTarget.GoGuid.IsEmpty())
            bot.UnreachableGuids.insert(bot.GameObjectTarget.GoGuid);
        else
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has no walkable path to the gameobject map marker for quest {}. Looking for other work.",
                player->GetName(), bot.GameObjectTarget.QuestId);
        bot.GameObjectTarget = {};
        bot.Walker.Reset();
        return false;
    }

    return true;
}

bool PlayerbotMgr::BeginUseItemOnUnitTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::UseItemOnUnitTarget const& target)
{
    if (bot.Walker.IsMoving())
        bot.Walker.Stop(player);

    ClearCombat(bot, player);
    ClearItemLoot(bot);
    ClearVendor(bot);
    bot.QuestSearchEmptyMs = 0;
    bot.QuestTarget = {};
    bot.GameObjectTarget = {};
    bot.UseItemOnUnitTarget = target;
    bot.CombatTarget = {};
    bot.LookedForOtherYellowOnFace = false;
    bot.Walker.Reset();

    if (TryClickFromHere(bot, player))
        return true;

    if (!bot.UseItemOnUnitTarget.CreatureGuid.IsEmpty())
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking to {} for quest {} (use item).",
            player->GetName(), bot.UseItemOnUnitTarget.CreatureGuid.ToString(), bot.UseItemOnUnitTarget.QuestId);
    else
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking to the map marker for quest {} (use item).",
            player->GetName(), bot.UseItemOnUnitTarget.QuestId);

    bot.QuestArriveWaitMs = 0;
    if (!bot.Walker.Start(player, bot.UseItemOnUnitTarget.Pos, bot.UseItemOnUnitTarget.StopDistance))
    {
        if (!bot.UseItemOnUnitTarget.CreatureGuid.IsEmpty())
            bot.UnreachableGuids.insert(bot.UseItemOnUnitTarget.CreatureGuid);
        else
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has no walkable path to the map marker for quest {}. Looking for other work.",
                player->GetName(), bot.UseItemOnUnitTarget.QuestId);
        bot.UseItemOnUnitTarget = {};
        bot.Walker.Reset();
        return false;
    }

    return true;
}

bool PlayerbotMgr::BeginCombatTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::CombatTarget const& target)
{
    if (bot.Walker.IsMoving())
        bot.Walker.Stop(player);

    ClearCombat(bot, player);
    ClearItemLoot(bot);
    ClearVendor(bot);
    bot.QuestSearchEmptyMs = 0;
    bot.QuestTarget = {};
    bot.GameObjectTarget = {};
    bot.UseItemOnUnitTarget = {};
    bot.CombatTarget = target;
    bot.CombatSwingSent = false;
    bot.CombatCastSpellId = 0;
    bot.CombatCastPending = false;
    bot.CombatCastWaitMs = 0;
    bot.CombatFacingWait = false;
    bot.LookedForOtherYellowOnFace = false;
    bot.Walker.Reset();

    if (!bot.CombatTarget.CreatureGuid.IsEmpty())
    {
        Creature* creature = ObjectAccessor::GetCreature(*player, bot.CombatTarget.CreatureGuid);
        if (creature && player->IsWithinMeleeRange(creature) && PlayerbotClient::TryMeleeAttack(player, bot.CombatTarget.CreatureGuid))
        {
            bot.CombatSwingSent = true;
            return true;
        }

        if (bot.CombatTarget.QuestId)
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking to {} for quest {} ({}).",
                player->GetName(), bot.CombatTarget.CreatureGuid.ToString(), bot.CombatTarget.QuestId,
                bot.CombatTarget.ItemId ? "item" : "kill");
        else
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking to {} (fight).",
                player->GetName(), bot.CombatTarget.CreatureGuid.ToString());
    }
    else
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking to the kill map marker for quest {}.",
            player->GetName(), bot.CombatTarget.QuestId);

    bot.QuestArriveWaitMs = 0;
    if (!bot.Walker.Start(player, bot.CombatTarget.Pos, bot.CombatTarget.StopDistance))
    {
        if (KeepCombatAfterFailedWalk(player, bot.CombatTarget.CreatureGuid))
        {
            LogStayOnCombatWalkFail(player, bot.CombatTarget.CreatureGuid);
            bot.Walker.Reset();
            Creature* creature = ObjectAccessor::GetCreature(*player, bot.CombatTarget.CreatureGuid);
            if (creature && player->IsWithinMeleeRange(creature) && PlayerbotClient::TryMeleeAttack(player, bot.CombatTarget.CreatureGuid))
                bot.CombatSwingSent = true;
            return true;
        }

        if (!bot.CombatTarget.CreatureGuid.IsEmpty())
            bot.UnreachableGuids.insert(bot.CombatTarget.CreatureGuid);
        else
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has no walkable path to the kill map marker for quest {}. Looking for other work.",
                player->GetName(), bot.CombatTarget.QuestId);
        ClearCombat(bot, player);
        bot.Walker.Reset();
        return false;
    }

    return true;
}

void PlayerbotMgr::ClearItemLoot(PlayerbotRecord& bot)
{
    bot.ItemLootTarget = {};
    bot.LootOpenSent = false;
}

bool PlayerbotMgr::UpdateItemLoot(PlayerbotRecord& bot, Player* player, uint32 diff)
{
    if (!PlayerbotClient::ItemLootTargetStillNeeded(player, bot.ItemLootTarget))
    {
        ClearItemLoot(bot);
        bot.Walker.Reset();
        return false;
    }

    ObjectGuid lootOwner;
    char const* skipKind = "corpse";
    if (!bot.ItemLootTarget.GoGuid.IsEmpty())
    {
        skipKind = "object";
        GameObject* go = ObjectAccessor::GetGameObject(*player, bot.ItemLootTarget.GoGuid);
        if (!go || !go->isSpawned())
        {
            bot.UnreachableGuids.insert(bot.ItemLootTarget.GoGuid);
            ClearItemLoot(bot);
            bot.Walker.Reset();
            return false;
        }

        lootOwner = bot.ItemLootTarget.GoGuid;
        if (!player->GetGameObjectIfCanInteractWith(lootOwner))
        {
            if (bot.Walker.IsMoving())
                return true;

            float size = 1.0f;
            if (go->GetGOInfo())
                size = go->GetGOInfo()->size < 1.0f ? 1.0f : go->GetGOInfo()->size;
            Position standPos;
            if (!PlayerbotWalker::PickApproachPosition(player, go, size + 1.0f, standPos)
                || player->GetExactDist(standPos) <= bot.ItemLootTarget.StopDistance)
            {
                bot.UnreachableGuids.insert(lootOwner);
                ClearItemLoot(bot);
                bot.Walker.Reset();
                return false;
            }

            bot.ItemLootTarget.Pos = standPos;
            if (!bot.Walker.Start(player, standPos, bot.ItemLootTarget.StopDistance))
            {
                bot.UnreachableGuids.insert(lootOwner);
                ClearItemLoot(bot);
                bot.Walker.Reset();
                return false;
            }

            return true;
        }

        StopWalkToClick(bot, player, lootOwner);

        if (!bot.LootOpenSent)
        {
            PlayerbotClient::GameObjectTarget use;
            use.GoGuid = lootOwner;
            use.QuestId = bot.ItemLootTarget.QuestId;
            if (!PlayerbotClient::TryUseGameObject(player, use))
            {
                bot.QuestArriveWaitMs += diff;
                if (bot.QuestArriveWaitMs >= QUEST_SEARCH_RETRY_MS)
                {
                    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot use {}.",
                        player->GetName(), lootOwner.ToString());
                    bot.UnreachableGuids.insert(lootOwner);
                    ClearItemLoot(bot);
                    bot.Walker.Reset();
                    return false;
                }
                return true;
            }

            bot.LootOpenSent = true;
            bot.QuestArriveWaitMs = 0;
            return true;
        }
    }
    else
    {
        Creature* creature = ObjectAccessor::GetCreature(*player, bot.ItemLootTarget.CreatureGuid);
        if (!creature)
        {
            bot.UnreachableGuids.insert(bot.ItemLootTarget.CreatureGuid);
            ClearItemLoot(bot);
            bot.Walker.Reset();
            return false;
        }

        if (creature->IsAlive())
        {
            BeginCombatTarget(bot, player, PlayerbotClient::CombatTargetFromItemLoot(bot.ItemLootTarget));
            return true;
        }

        lootOwner = bot.ItemLootTarget.CreatureGuid;
        if (!player->IsWithinDistInMap(creature, creature->GetCombatReach() + 4.0f))
        {
            if (bot.Walker.IsMoving())
                return true;

            Position standPos;
            float const standDistance = creature->GetCombatReach() + 1.0f;
            if (!PlayerbotWalker::PickApproachPosition(player, creature, standDistance, standPos)
                || player->GetExactDist(standPos) <= bot.ItemLootTarget.StopDistance)
            {
                bot.UnreachableGuids.insert(lootOwner);
                ClearItemLoot(bot);
                bot.Walker.Reset();
                return false;
            }

            bot.ItemLootTarget.Pos = standPos;
            if (!bot.Walker.Start(player, standPos, bot.ItemLootTarget.StopDistance))
            {
                bot.UnreachableGuids.insert(lootOwner);
                ClearItemLoot(bot);
                bot.Walker.Reset();
                return false;
            }

            return true;
        }

        StopWalkToClick(bot, player, lootOwner);

        if (!bot.LootOpenSent)
        {
            if (!PlayerbotClient::TryOpenLoot(player, lootOwner))
            {
                bot.QuestArriveWaitMs += diff;
                if (bot.QuestArriveWaitMs >= QUEST_SEARCH_RETRY_MS)
                {
                    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot open loot on {}.",
                        player->GetName(), lootOwner.ToString());
                    bot.UnreachableGuids.insert(lootOwner);
                    ClearItemLoot(bot);
                    bot.Walker.Reset();
                    return false;
                }
                return true;
            }

            bot.LootOpenSent = true;
            bot.QuestArriveWaitMs = 0;
            return true;
        }
    }

    bot.QuestArriveWaitMs += diff;
    if (!bot.ItemLootTarget.GoGuid.IsEmpty())
    {
        if (PlayerbotClient::TryTakeQuestItemFromOpenLoot(player, lootOwner, bot.ItemLootTarget.ItemId))
        {
            bot.QuestInteractQueued = true;
            bot.QuestInteractWaitMs = 0;
            return true;
        }

        if (PlayerbotClient::HasOpenLootOn(player, lootOwner))
        {
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} opened loot on {} but the quest item is not in it. Skipping that {}.",
                player->GetName(), lootOwner.ToString(), skipKind);
            PlayerbotClient::QueueLootRelease(player->GetSession(), lootOwner);
            bot.UnreachableGuids.insert(lootOwner);
            ClearItemLoot(bot);
            bot.Walker.Reset();
            return false;
        }
    }
    else if (PlayerbotClient::TryTakeAllFromOpenLoot(player, lootOwner))
    {
        bot.UnreachableGuids.insert(lootOwner);
        ClearItemLoot(bot);
        bot.Walker.Reset();
        return true;
    }

    if (bot.QuestArriveWaitMs >= QUEST_SEARCH_RETRY_MS)
    {
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} opened loot on {} but no loot window appeared. Skipping that {}.",
            player->GetName(), lootOwner.ToString(), skipKind);
        bot.UnreachableGuids.insert(lootOwner);
        ClearItemLoot(bot);
        bot.Walker.Reset();
        return false;
    }

    return true;
}

bool PlayerbotMgr::BeginItemLootTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::ItemLootTarget const& target)
{
    if (bot.Walker.IsMoving())
        bot.Walker.Stop(player);

    ClearCombat(bot, player);
    ClearVendor(bot);
    bot.QuestSearchEmptyMs = 0;
    bot.QuestTarget = {};
    bot.GameObjectTarget = {};
    bot.UseItemOnUnitTarget = {};
    bot.CombatTarget = {};
    bot.ItemLootTarget = target;
    bot.LootOpenSent = false;
    bot.LookedForOtherYellowOnFace = false;
    bot.Walker.Reset();

    bool canClick = false;
    if (!bot.ItemLootTarget.GoGuid.IsEmpty())
        canClick = player->GetGameObjectIfCanInteractWith(bot.ItemLootTarget.GoGuid) != nullptr;
    else if (!bot.ItemLootTarget.CreatureGuid.IsEmpty() && bot.ItemLootTarget.LootCorpse)
    {
        Creature* creature = ObjectAccessor::GetCreature(*player, bot.ItemLootTarget.CreatureGuid);
        canClick = InInteractRange(player, creature);
    }

    if (canClick)
    {
        bot.QuestArriveWaitMs = 0;
        return UpdateItemLoot(bot, player, 0);
    }

    if (!bot.ItemLootTarget.GoGuid.IsEmpty())
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking to {} for quest {} (use).",
            player->GetName(), bot.ItemLootTarget.GoGuid.ToString(), bot.ItemLootTarget.QuestId);
    else if (!bot.ItemLootTarget.CreatureGuid.IsEmpty() && bot.ItemLootTarget.QuestId)
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking to {} for quest {} (loot).",
            player->GetName(), bot.ItemLootTarget.CreatureGuid.ToString(), bot.ItemLootTarget.QuestId);
    else if (!bot.ItemLootTarget.CreatureGuid.IsEmpty())
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking to loot {}.",
            player->GetName(), bot.ItemLootTarget.CreatureGuid.ToString());
    else
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking to the item map marker for quest {}.",
            player->GetName(), bot.ItemLootTarget.QuestId);

    bot.QuestArriveWaitMs = 0;
    if (!bot.Walker.Start(player, bot.ItemLootTarget.Pos, bot.ItemLootTarget.StopDistance))
    {
        if (!bot.ItemLootTarget.GoGuid.IsEmpty())
            bot.UnreachableGuids.insert(bot.ItemLootTarget.GoGuid);
        else if (!bot.ItemLootTarget.CreatureGuid.IsEmpty())
            bot.UnreachableGuids.insert(bot.ItemLootTarget.CreatureGuid);
        else
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has no walkable path to the item map marker for quest {}. Looking for other work.",
                player->GetName(), bot.ItemLootTarget.QuestId);
        ClearItemLoot(bot);
        bot.Walker.Reset();
        return false;
    }

    return true;
}

void PlayerbotMgr::ClearVendor(PlayerbotRecord& bot)
{
    bot.VendorTarget = {};
    bot.VendorListSent = false;
    bot.VendorActed = false;
}

bool PlayerbotMgr::TryBeginVendor(PlayerbotRecord& bot, Player* player)
{
    if (bot.VendorRetryMs)
        return false;
    if (!bot.VendorTarget.NpcGuid.IsEmpty())
        return false;
    if (!PlayerbotClient::NeedsVendor(player))
        return false;

    bool const preferRepair = PlayerbotClient::EquippedGearNeedsRepair(player);
    Optional<PlayerbotClient::VendorTarget> found = PlayerbotClient::FindNearestVendor(player, bot.UnreachableGuids, preferRepair);
    if (!found)
    {
        bot.VendorRetryMs = VENDOR_RETRY_MS;
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} needs a vendor but none is reachable on this map yet.",
            player->GetName());
        return false;
    }

    return BeginVendorTarget(bot, player, *found);
}

bool PlayerbotMgr::BeginVendorTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::VendorTarget const& target)
{
    if (bot.Walker.IsMoving())
        bot.Walker.Stop(player);

    ClearCombat(bot, player);
    ClearItemLoot(bot);
    bot.QuestSearchEmptyMs = 0;
    bot.QuestTarget = {};
    bot.GameObjectTarget = {};
    bot.UseItemOnUnitTarget = {};
    bot.CombatTarget = {};
    bot.VendorTarget = target;
    bot.VendorListSent = false;
    bot.VendorActed = false;
    bot.LookedForOtherYellowOnFace = false;
    bot.Walker.Reset();

    if (TryClickFromHere(bot, player))
        return true;

    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking to vendor {}{}.",
        player->GetName(), bot.VendorTarget.NpcGuid.ToString(),
        bot.VendorTarget.CanRepair ? " (can repair)" : "");
    bot.QuestArriveWaitMs = 0;
    if (!bot.Walker.Start(player, bot.VendorTarget.Pos, bot.VendorTarget.StopDistance))
    {
        bot.UnreachableGuids.insert(bot.VendorTarget.NpcGuid);
        ClearVendor(bot);
        bot.Walker.Reset();
        return false;
    }

    return true;
}

bool PlayerbotMgr::UpdateVendor(PlayerbotRecord& bot, Player* player, uint32 diff)
{
    if (bot.VendorTarget.NpcGuid.IsEmpty() || !player->GetSession())
    {
        ClearVendor(bot);
        bot.Walker.Reset();
        return false;
    }

    if (!bot.VendorListSent)
    {
        bot.QuestArriveWaitMs += diff;
        if (PlayerbotClient::TryOpenVendor(player, bot.VendorTarget.NpcGuid))
        {
            bot.VendorListSent = true;
            bot.QuestArriveWaitMs = 0;
            return true;
        }

        Creature* creature = ObjectAccessor::GetCreature(*player, bot.VendorTarget.NpcGuid);
        if (creature && creature->IsAlive()
            && !player->IsWithinDistInMap(creature, creature->GetCombatReach() + 4.0f))
        {
            Position standPos;
            float const standDistance = creature->GetCombatReach() + 1.0f;
            if (PlayerbotWalker::PickApproachPosition(player, creature, standDistance, standPos)
                && bot.Walker.Start(player, standPos, bot.VendorTarget.StopDistance))
            {
                bot.VendorTarget.Pos = standPos;
                bot.QuestArriveWaitMs = 0;
                TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} is still short of vendor {} and is walking the rest of the way.",
                    player->GetName(), bot.VendorTarget.NpcGuid.ToString());
                return true;
            }
        }

        if (bot.QuestArriveWaitMs < 5000)
            return true;

        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} arrived but cannot open the shop at {}. Looking for other work.",
            player->GetName(), bot.VendorTarget.NpcGuid.ToString());
        bot.UnreachableGuids.insert(bot.VendorTarget.NpcGuid);
        ClearVendor(bot);
        bot.Walker.Reset();
        return false;
    }

    bot.QuestArriveWaitMs += diff;
    if (!bot.VendorActed)
    {
        if (bot.QuestArriveWaitMs < QUEST_CHAIN_PAUSE_MS)
            return true;

        bool const repair = PlayerbotClient::EquippedGearNeedsRepair(player);
        if (!PlayerbotClient::TryVendorTrade(player, bot.VendorTarget.NpcGuid, repair))
        {
            bot.UnreachableGuids.insert(bot.VendorTarget.NpcGuid);
            ClearVendor(bot);
            bot.Walker.Reset();
            return false;
        }

        bot.VendorActed = true;
        bot.QuestArriveWaitMs = 0;
        return true;
    }

    if (bot.QuestArriveWaitMs < QUEST_CHAIN_PAUSE_MS)
        return true;

    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} finished at the vendor. Returning to questing.",
        player->GetName());
    ClearVendor(bot);
    if (PlayerbotClient::NeedsVendor(player))
        bot.VendorRetryMs = VENDOR_RETRY_MS;
    bot.Walker.Reset();
    return false;
}
