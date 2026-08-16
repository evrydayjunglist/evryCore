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
#include "GameObject.h"
#include "GameTime.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotFactory.h"
#include "UnitDefines.h"
#include "World.h"
#include "WorldSession.h"
#include <limits>

namespace
{
    constexpr float QUEST_SEARCH_RANGE = 40.0f;
    constexpr float COMBAT_SEARCH_RANGE = 150.0f;
    constexpr float LOOT_SEARCH_RANGE = 10.0f;
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

    bot.QuestSearchFailed = false;

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
        bot.GameObjectTarget = {};
        bot.ItemLootTarget = {};
        bot.LootOpenSent = false;
        bot.UnreachableGuids.clear();
        bot.Walker.Reset();
    }

    if (bot.Walker.HasFailed())
        RecoverFailedWalk(bot, player);

    if (!bot.CombatTarget.CreatureGuid.IsEmpty())
    {
        if (UpdateCombat(bot, player))
            return;
    }

    if (bot.ItemLootTarget.QuestId && !bot.ItemLootTarget.CreatureGuid.IsEmpty() && bot.ItemLootTarget.LootCorpse)
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
                bot.UnreachableGuids.clear();

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

    if (bot.Walker.HasArrived() && bot.ItemLootTarget.QuestId && bot.ItemLootTarget.CreatureGuid.IsEmpty())
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
                bot.UnreachableGuids.clear();

            bot.QuestArriveWaitMs += diff;
            if (Optional<PlayerbotClient::ItemLootTarget> found = PlayerbotClient::FindLogIncompleteItemTarget(player, bot.UnreachableGuids))
            {
                if (!found->CreatureGuid.IsEmpty())
                {
                    BeginItemWork(bot, player, *found);
                    return;
                }
            }

            if (bot.QuestArriveWaitMs < QUEST_SEARCH_RETRY_MS)
                return;

            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} reached the item map marker but no spawned creature is there yet. Looking for other work.",
                player->GetName());
            int32 const skipQuestId = bot.ItemLootTarget.QuestId;
            uint32 const skipEntry = bot.ItemLootTarget.ItemId;
            ClearItemLoot(bot);
            bot.Walker.Reset();
            if (TryMapYellow(bot, player, skipQuestId, skipEntry))
                return;
        }
    }

    if (bot.Walker.IsMoving())
    {
        TryImmediateWorld(bot, player, true);
        return;
    }

    if (TryImmediateWorld(bot, player, false))
        return;

    if (TryMapYellow(bot, player))
        return;

    bot.QuestSearchEmptyMs += diff;
    if (bot.QuestSearchEmptyMs >= QUEST_SEARCH_RETRY_MS)
    {
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has no immediate work and no map yellow yet. Still looking (talk {:.0f} yards, kill {:.0f} yards).",
            player->GetName(), QUEST_SEARCH_RANGE, COMBAT_SEARCH_RANGE);
        bot.QuestSearchEmptyMs = 0;
    }
}

void PlayerbotMgr::RecoverFailedWalk(PlayerbotRecord& bot, Player* player)
{
    if (bot.GameObjectTarget.QuestId)
    {
        if (!bot.GameObjectTarget.GoGuid.IsEmpty())
            bot.UnreachableGuids.insert(bot.GameObjectTarget.GoGuid);
        bot.GameObjectTarget = {};
    }
    else if (bot.CombatTarget.QuestId || !bot.CombatTarget.CreatureGuid.IsEmpty())
    {
        if (!bot.CombatTarget.CreatureGuid.IsEmpty())
            bot.UnreachableGuids.insert(bot.CombatTarget.CreatureGuid);
        ClearCombat(bot, player);
    }
    else if (bot.ItemLootTarget.QuestId)
    {
        if (!bot.ItemLootTarget.CreatureGuid.IsEmpty())
            bot.UnreachableGuids.insert(bot.ItemLootTarget.CreatureGuid);
        ClearItemLoot(bot);
    }
    else if (!bot.QuestTarget.NpcGuid.IsEmpty())
        bot.UnreachableGuids.insert(bot.QuestTarget.NpcGuid);

    bot.QuestTarget = {};
    bot.Walker.Reset();
}

bool PlayerbotMgr::TryImmediateWorld(PlayerbotRecord& bot, Player* player, bool walking)
{
    Optional<PlayerbotClient::QuestTarget> talk = PlayerbotClient::FindNearbyQuestTarget(player, QUEST_SEARCH_RANGE, PlayerbotClient::QuestSearchKind::Talk);
    Optional<PlayerbotClient::ItemLootTarget> loot = PlayerbotClient::FindNearbyItemLootTarget(player, LOOT_SEARCH_RANGE, bot.UnreachableGuids);
    Optional<PlayerbotClient::GameObjectTarget> go = PlayerbotClient::FindNearbyGameObjectObjectiveTarget(player, LOOT_SEARCH_RANGE, bot.UnreachableGuids, walking);
    Optional<PlayerbotClient::CombatTarget> kill;
    if (!walking)
        kill = PlayerbotClient::FindNearbyMonsterObjectiveTarget(player, COMBAT_SEARCH_RANGE, bot.UnreachableGuids);

    if (talk && talk->NpcGuid == bot.QuestTarget.NpcGuid)
        talk.reset();
    if (loot && !bot.ItemLootTarget.CreatureGuid.IsEmpty() && loot->CreatureGuid == bot.ItemLootTarget.CreatureGuid)
        loot.reset();
    if (go && !bot.GameObjectTarget.GoGuid.IsEmpty() && go->GoGuid == bot.GameObjectTarget.GoGuid)
        go.reset();
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
    if (kill)
        consider(player->GetExactDist(kill->Pos), 4);

    if (!kind)
        return false;

    if (kind == 1)
        return BeginQuestTarget(bot, player, *talk);
    if (kind == 2)
        return BeginItemWork(bot, player, *loot);
    if (kind == 3)
        return BeginGameObjectTarget(bot, player, *go);
    return BeginCombatTarget(bot, player, *kill);
}

bool PlayerbotMgr::TryMapYellow(PlayerbotRecord& bot, Player* player, int32 skipQuestId, uint32 skipEntry)
{
    for (int32 attempt = 0; attempt < 4; ++attempt)
    {
        Optional<PlayerbotClient::GameObjectTarget> go = PlayerbotClient::FindLogIncompleteGameObjectTarget(player, bot.UnreachableGuids, skipQuestId, skipEntry);
        Optional<PlayerbotClient::CombatTarget> kill = PlayerbotClient::FindLogIncompleteMonsterTarget(player, bot.UnreachableGuids, skipQuestId, skipEntry);
        Optional<PlayerbotClient::ItemLootTarget> item = PlayerbotClient::FindLogIncompleteItemTarget(player, bot.UnreachableGuids, skipQuestId, skipEntry);
        Optional<PlayerbotClient::QuestTarget> turnIn = PlayerbotClient::FindLogCompleteTurnIn(player, skipQuestId);

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
        if (kill)
            consider(player->GetExactDist(kill->Pos), 2);
        if (item)
            consider(player->GetExactDist(item->Pos), 3);
        if (turnIn)
            consider(player->GetExactDist(turnIn->Pos), 4);

        if (!kind)
            return false;

        int32 nextSkipQuest = skipQuestId;
        uint32 nextSkipEntry = skipEntry;
        bool started = false;
        if (kind == 1)
        {
            nextSkipQuest = go->QuestId;
            nextSkipEntry = go->GoEntry;
            started = BeginGameObjectTarget(bot, player, *go);
        }
        else if (kind == 2)
        {
            nextSkipQuest = kill->QuestId;
            nextSkipEntry = kill->CreditEntry;
            started = BeginCombatTarget(bot, player, *kill);
        }
        else if (kind == 3)
        {
            nextSkipQuest = item->QuestId;
            nextSkipEntry = item->ItemId;
            started = BeginItemWork(bot, player, *item);
        }
        else
        {
            nextSkipQuest = turnIn->QuestId;
            nextSkipEntry = 0;
            started = BeginQuestTarget(bot, player, *turnIn);
        }

        if (started)
            return true;

        skipQuestId = nextSkipQuest;
        skipEntry = nextSkipEntry;
    }

    return false;
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
}

bool PlayerbotMgr::UpdateCombat(PlayerbotRecord& bot, Player* player)
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
        if (bot.CombatTarget.ItemId)
        {
            PlayerbotClient::ItemLootTarget loot;
            loot.CreatureGuid = bot.CombatTarget.CreatureGuid;
            loot.Pos = bot.CombatTarget.Pos;
            loot.StopDistance = bot.CombatTarget.StopDistance;
            loot.QuestId = bot.CombatTarget.QuestId;
            loot.ItemId = bot.CombatTarget.ItemId;
            loot.CreatureEntry = bot.CombatTarget.CreditEntry;
            loot.LootCorpse = true;
            ClearCombat(bot, player);
            bot.ItemLootTarget = loot;
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

    if (player->IsWithinMeleeRange(creature))
    {
        if (player->GetVictim() == creature && player->HasUnitState(UNIT_STATE_MELEE_ATTACKING))
            return true;

        if (!bot.CombatSwingSent && PlayerbotClient::TryMeleeAttack(player, bot.CombatTarget.CreatureGuid))
            bot.CombatSwingSent = true;

        return true;
    }

    if (bot.CombatTarget.QuestId && !PlayerbotClient::CombatTargetStillNeeded(player, bot.CombatTarget))
    {
        ClearCombat(bot, player);
        bot.Walker.Reset();
        return false;
    }

    bot.CombatSwingSent = false;
    if (bot.Walker.IsMoving())
        return true;

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

bool PlayerbotMgr::BeginQuestTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::QuestTarget const& target)
{
    if (bot.Walker.IsMoving())
        bot.Walker.Stop(player);

    ClearCombat(bot, player);
    ClearItemLoot(bot);
    bot.QuestSearchEmptyMs = 0;
    bot.QuestTarget = target;
    bot.GameObjectTarget = {};
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
    bot.QuestSearchEmptyMs = 0;
    bot.QuestTarget = {};
    bot.GameObjectTarget = target;
    bot.CombatTarget = {};
    bot.Walker.Reset();

    if (!bot.GameObjectTarget.GoGuid.IsEmpty()
        && player->GetExactDist(bot.GameObjectTarget.Pos) <= bot.GameObjectTarget.StopDistance
        && PlayerbotClient::TryUseGameObject(player, bot.GameObjectTarget))
    {
        bot.QuestInteractQueued = true;
        bot.QuestInteractWaitMs = 0;
        return true;
    }

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

bool PlayerbotMgr::BeginCombatTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::CombatTarget const& target)
{
    if (bot.Walker.IsMoving())
        bot.Walker.Stop(player);

    ClearCombat(bot, player);
    ClearItemLoot(bot);
    bot.QuestSearchEmptyMs = 0;
    bot.QuestTarget = {};
    bot.GameObjectTarget = {};
    bot.CombatTarget = target;
    bot.CombatSwingSent = false;
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

    if (!player->IsWithinDistInMap(creature, creature->GetCombatReach() + 4.0f))
    {
        Position standPos;
        float const standDistance = creature->GetCombatReach() + 1.0f;
        if (!PlayerbotWalker::PickApproachPosition(player, creature, standDistance, standPos)
            || player->GetExactDist(standPos) <= bot.ItemLootTarget.StopDistance)
        {
            bot.UnreachableGuids.insert(bot.ItemLootTarget.CreatureGuid);
            ClearItemLoot(bot);
            bot.Walker.Reset();
            return false;
        }

        bot.ItemLootTarget.Pos = standPos;
        if (!bot.Walker.Start(player, standPos, bot.ItemLootTarget.StopDistance))
        {
            bot.UnreachableGuids.insert(bot.ItemLootTarget.CreatureGuid);
            ClearItemLoot(bot);
            bot.Walker.Reset();
            return false;
        }

        return true;
    }

    if (!bot.LootOpenSent)
    {
        if (!PlayerbotClient::TryOpenLoot(player, bot.ItemLootTarget.CreatureGuid))
        {
            bot.QuestArriveWaitMs += diff;
            if (bot.QuestArriveWaitMs >= QUEST_SEARCH_RETRY_MS)
            {
                TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot open loot on {}.",
                    player->GetName(), bot.ItemLootTarget.CreatureGuid.ToString());
                bot.UnreachableGuids.insert(bot.ItemLootTarget.CreatureGuid);
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

    bot.QuestArriveWaitMs += diff;
    if (PlayerbotClient::TryTakeQuestItemFromOpenLoot(player, bot.ItemLootTarget.CreatureGuid, bot.ItemLootTarget.ItemId))
    {
        bot.QuestInteractQueued = true;
        bot.QuestInteractWaitMs = 0;
        return true;
    }

    if (PlayerbotClient::HasOpenLootOn(player, bot.ItemLootTarget.CreatureGuid))
    {
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} opened loot on {} but the quest item is not in it. Skipping that corpse.",
            player->GetName(), bot.ItemLootTarget.CreatureGuid.ToString());
        PlayerbotClient::QueueLootRelease(player->GetSession(), bot.ItemLootTarget.CreatureGuid);
        bot.UnreachableGuids.insert(bot.ItemLootTarget.CreatureGuid);
        ClearItemLoot(bot);
        bot.Walker.Reset();
        return false;
    }

    if (bot.QuestArriveWaitMs >= QUEST_SEARCH_RETRY_MS)
    {
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} opened loot on {} but no loot window appeared. Skipping that corpse.",
            player->GetName(), bot.ItemLootTarget.CreatureGuid.ToString());
        bot.UnreachableGuids.insert(bot.ItemLootTarget.CreatureGuid);
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
    bot.QuestSearchEmptyMs = 0;
    bot.QuestTarget = {};
    bot.GameObjectTarget = {};
    bot.CombatTarget = {};
    bot.ItemLootTarget = target;
    bot.LootOpenSent = false;
    bot.Walker.Reset();

    if (!bot.ItemLootTarget.CreatureGuid.IsEmpty() && bot.ItemLootTarget.LootCorpse
        && player->GetExactDist(bot.ItemLootTarget.Pos) <= bot.ItemLootTarget.StopDistance)
    {
        bot.QuestArriveWaitMs = 0;
        return UpdateItemLoot(bot, player, 0);
    }

    if (!bot.ItemLootTarget.CreatureGuid.IsEmpty())
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking to {} for quest {} (loot).",
            player->GetName(), bot.ItemLootTarget.CreatureGuid.ToString(), bot.ItemLootTarget.QuestId);
    else
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking to the item map marker for quest {}.",
            player->GetName(), bot.ItemLootTarget.QuestId);

    bot.QuestArriveWaitMs = 0;
    if (!bot.Walker.Start(player, bot.ItemLootTarget.Pos, bot.ItemLootTarget.StopDistance))
    {
        if (!bot.ItemLootTarget.CreatureGuid.IsEmpty())
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
