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
    constexpr uint32 RELEASE_WAIT_MS = 3000;
    constexpr uint32 GHOST_SETTLE_MS = 500;
    constexpr uint32 PACKET_RETRY_MS = 2000;
    constexpr uint32 CAMPED_WAIT_MS = 20000;
    constexpr uint32 GHOST_WAIT_LONG_MS = 180000;
    constexpr uint32 GHOST_GIVE_UP_MS = 300000;
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

    if (UpdateDeath(bot, player, diff))
        return;

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

    int32 skipFailedQuestId = 0;
    uint32 skipFailedEntry = 0;
    if (bot.Walker.HasFailed())
    {
        if (bot.GameObjectTarget.QuestId)
        {
            skipFailedQuestId = bot.GameObjectTarget.QuestId;
            skipFailedEntry = bot.GameObjectTarget.GoEntry;
        }
        else if (bot.CombatTarget.QuestId)
        {
            skipFailedQuestId = bot.CombatTarget.QuestId;
            skipFailedEntry = bot.CombatTarget.CreditEntry;
        }
        else if (bot.ItemLootTarget.QuestId)
        {
            skipFailedQuestId = bot.ItemLootTarget.QuestId;
            skipFailedEntry = bot.ItemLootTarget.ItemId;
        }
        else if (bot.QuestTarget.QuestId)
            skipFailedQuestId = bot.QuestTarget.QuestId;

        RecoverFailedWalk(bot, player);
    }

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
    bot.QuestInteractQueued = false;
    bot.QuestArriveWaitMs = 0;
    bot.QuestInteractWaitMs = 0;
    bot.QuestSearchEmptyMs = 0;
    bot.QuestTarget = {};
    bot.GameObjectTarget = {};
    bot.UnreachableGuids.clear();
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
