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

#ifndef EVRY_MOD_PLAYERBOT_MGR_H
#define EVRY_MOD_PLAYERBOT_MGR_H

#include "PlayerbotClient.h"
#include "PlayerbotBridge.h"
#include "CommandablePlayerState.h"
#include "PlayerbotCoordinatorLease.h"
#include "PlayerbotCoordinatorPresence.h"
#include "PlayerbotInvitePolicy.h"
#include "PlayerbotMapPass.h"
#include "PlayerbotMovement.h"
#include "PlayerbotServerMovement.h"
#include "PlayerbotSessionPresence.h"
#include "PlayerbotTickStats.h"
#include "PlayerbotWalkMapper.h"
#include "Playerbots.h"
#include "ObjectGuid.h"
#include "Position.h"
#include <memory>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <vector>

class Map;
class Player;
class WorldPacket;
class WorldSession;

enum class PlayerbotDeathWork
{
    None,
    WaitToRelease,
    WaitForGhost,
    WalkToCorpse,
    WaitToReclaim,
    WalkToHealer,
    WaitToHeal,
    SitRecover
};

enum class PlayerbotLoginRefusal
{
    None,
    WorldClosed,
    AccountBanned,
    SecurityLimit,
    CharacterBanned
};

struct PlayerbotRecord
{
    PlayerbotAccount Account;
    bool SessionQueued = false;
    bool SessionSeen = false;
    // Time left before she may log in again after losing her session or having a login refused.
    uint32 LoginWaitMs = 0;
    // The refusal already written to the log, so a refusal that repeats after every wait is logged once.
    PlayerbotLoginRefusal LoggedLoginRefusal = PlayerbotLoginRefusal::None;
    bool CoordinatorLogoutRequested = false;
    bool CoordinatorLogoutKickSent = false;
    bool EnumQueued = false;
    bool LoginQueued = false;
    bool ContinueLoginCalled = false;
    bool CinematicSkipped = false;
    bool InitMoverQueued = false;
    bool QuestInteractQueued = false;
    uint32 QuestArriveWaitMs = 0;
    uint32 QuestInteractWaitMs = 0;
    uint32 QuestSearchEmptyMs = 0;
    bool CombatSwingSent = false;
    uint32 CombatCastSpellId = 0;
    bool CombatCastPending = false;
    uint32 CombatCastWaitMs = 0;
    bool CombatFacingWait = false;
    bool UseItemCastPending = false;
    bool UseItemCastSeenGcd = false;
    bool UseItemFacingWait = false;
    uint32 UseItemCastSpellId = 0;
    uint32 UseItemCastWaitMs = 0;
    bool LootOpenSent = false;
    uint32 LootOpenWaitMs = 0;
    PlayerbotDeathWork Death = PlayerbotDeathWork::None;
    uint32 DeathWaitMs = 0;
    uint32 GhostMs = 0;
    uint32 CampedMs = 0;
    bool HadSickness = false;
    bool GhostSettled = false;
    bool RepopSent = false;
    bool ReclaimSent = false;
    bool HealerSent = false;
    bool SitSent = false;
    // Food or drink she used during this rest: the item she is waiting on, and the ones that did not start eating.
    uint32 RestItemEntry = 0;
    uint32 RestItemSpellId = 0;
    uint32 RestItemWaitMs = 0;
    std::unordered_set<uint32> RestItemsRefused;
    Position SpiritReleasePos;
    ObjectGuid SpiritHealerGuid;
    PlayerbotClient::QuestTarget QuestTarget;
    PlayerbotClient::CombatTarget CombatTarget;
    PlayerbotClient::GameObjectTarget GameObjectTarget;
    PlayerbotClient::UseItemOnUnitTarget UseItemOnUnitTarget;
    PlayerbotClient::ItemLootTarget ItemLootTarget;
    PlayerbotClient::VendorTarget VendorTarget;
    bool VendorListSent = false;
    bool VendorActed = false;
    uint32 VendorRetryMs = 0;
    // A finished quest whose rewards did not fit in her bags. She goes to a vendor to make room and then back to turn
    // it in. VendorSoldForRoom: she has already sold other items for it at this vendor.
    uint32 RoomForQuestId = 0;
    bool VendorSoldForRoom = false;
    std::unordered_set<ObjectGuid> UnreachableGuids;
    std::vector<Position> UnreachablePositions;
    std::vector<PlayerbotClient::EmptyMarker> EmptyMarkers;
    // Where she stood when reaching a map marker last wiped her skip list. Reaching a marker wipes it again only once
    // she has come somewhere new, so standing on a marker does not wipe it every few seconds.
    Position SkipsForgottenAt;
    bool SkipsForgotten = false;
    // She could not step anywhere from where she stood. She starts no new work until this has run out.
    uint32 StuckFeetWaitMs = 0;
    // Where and since when (game time) she has kept failing to step anywhere. Long enough there, she uses her
    // Hearthstone. A place more than a few yards away, or a teleport, starts it again.
    WorldLocation StuckFeetPlace;
    uint32 StuckFeetSinceMs = 0;
    // She has said she has no Hearthstone ready at this place, so she does not say it every few seconds.
    bool StuckFeetSaidNoHearth = false;
    // How long she has stood with nothing to do while targets were on her skip list.
    uint32 IdleWithSkipsMs = 0;
    // Looking around for new work is most of what a bot with nothing to do costs. After a look she waits before the
    // next one, idle or walking; the idle time between looks still counts toward the idle waits.
    uint32 NextLookMs = 0;
    uint32 NextWalkLookMs = 0;
    uint32 SinceLookMs = 0;
    // Where she already started walking the rest of the way to this target.
    ObjectGuid StillShortGuid;
    std::vector<Position> StillShortFeet;
    bool LookedForOtherYellowOnFace = false;
    // The quest reward she chose to put on, and how long she has waited for it to reach her bags.
    uint32 WearItemId = 0;
    uint32 WearWaitMs = 0;
    // She is running from a fight she is losing: where she started running and how many stretches she has run.
    bool Fleeing = false;
    Position FleeStart;
    uint32 FleeLegs = 0;
    // Running did not shake them off. She fights this fight out before she runs again.
    bool FleeGaveUp = false;
    // Her client's view of the root: the server's root packet sets it and its unroot packet clears it.
    bool ServerRooted = false;
    bool HeldInPlaceLogged = false;
    // Replies the server is waiting for while it moves her to another place or map.
    PlayerbotServerReply TeleportReply;
    PlayerbotServerReply SuspendTokenReply;
    PlayerbotServerReply WorldPortReply;
    uint32 UnansweredTeleportMs = 0;
    // The party invite window she has open: who sent it and how long she has had it.
    ObjectGuid InviteFrom;
    uint32 InviteOpenMs = 0;
    // The party leader she follows, where her walk after them was aimed, and how long to wait before she tries again
    // after that walk failed.
    ObjectGuid FollowLeader;
    Position FollowAim;
    uint32 FollowRetryMs = 0;
    CommandablePlayerState Command;
    Position CommandDestination;
    bool CommandMovePending = false;
    bool OriginalControlRestorePending = false;
    PlayerbotWalker Walker;
    // With Playerbots.MapThreadBrains on: the world thread found her standing on a map, not commanded, and past login, so
    // her map's thread runs her brain on its next update. Written only by the world thread.
    bool BrainOnMapThread = false;
};

struct CommandableRtsSession
{
    ObjectGuid Commander;
    ObjectGuid Group;
    uint32 MapId = 0;
    uint32 InstanceId = 0;
    std::unordered_set<ObjectGuid> Subjects;
};

class PlayerbotMgr
{
public:
    static PlayerbotMgr* instance();

    PlayerbotMgr() = default;
    ~PlayerbotMgr();

    void Start();
    void Stop();
    void Update(uint32 diff);
    // The map's own thread, at the end of its update: the brains of the bots standing on it, with
    // Playerbots.MapThreadBrains on.
    void UpdateMap(Map* map, uint32 diff);
    void OnMapDestroyed(Map* map);
    bool IsBotAccount(uint32 accountId) const;
    void OnBotLogin(Player* player);
    void OnPlayerLogout(Player* player);
    void OnPlayerMapChanged(Player* player);
    // Any thread: keeps the movement orders, time sync requests, and party invites a bot's client must answer, until
    // OnUpdate answers them.
    void OnSocketlessSessionPacketSend(WorldSession* session, WorldPacket const& packet);
    // Maps the ground this player can walk from where her client last put her, by a bot's walk rules, and writes a
    // picture next to the server logs. False, with the reason in message, when she cannot be mapped now.
    bool StartWalkMap(Player* subject, float radius, ObjectGuid requester, std::string& message);
    // The last report of what the bot brains cost the world tick, in the words written to the log.
    std::string DescribeTickStats() const;

private:
    friend class CommandablePlayerService;

    CommandableRtsEnterResult EnterRts(Player* commander);
    CommandablePlayerResult SubmitCommand(CommandablePlayerRequest const& request);
    CommandablePlayerResult ExitRts(ObjectGuid commander);
    std::vector<CommandablePlayerSnapshot> GetRtsSubjects(ObjectGuid commander) const;
    void UpdateRts(uint32 diff);
    void UpdateCommanded(PlayerbotRecord& runtime, Player* player, uint32 diff, bool managedBot);
    void ValidateRtsSessions();
    void InvalidateRtsSession(ObjectGuid commander, char const* reason);
    void InvalidateRtsSubject(ObjectGuid commander, ObjectGuid subject, char const* reason);
    void InvalidateAllRts(char const* reason);
    PlayerbotRecord* FindManagedBot(ObjectGuid subject);
    PlayerbotRecord const* FindManagedBot(ObjectGuid subject) const;
    PlayerbotRecord* FindCommandRuntime(ObjectGuid subject);
    PlayerbotRecord const* FindCommandRuntime(ObjectGuid subject) const;
    CommandableRtsSession* FindRtsSession(ObjectGuid commander);
    CommandableRtsSession const* FindRtsSession(ObjectGuid commander) const;
    bool IsOriginalCharacter(ObjectGuid subject) const;
    bool HasValidRtsClaim(ObjectGuid subject) const;
    void QuiesceForCommand(PlayerbotRecord& runtime, Player* player, bool originalCharacter);
    void ResumeCoordinatorLogoutAfterRelease();
    void UpdateBridge(uint32 diff);
    std::string HandleBridgeRequest(uint64 connectionId, std::string const& payload);
    void BeginCoordinatorLogout();
    bool UpdateCoordinatorLogout(PlayerbotRecord& bot);
    void ResetBotSession(PlayerbotRecord& bot);
    bool UpdateSessionPresence(PlayerbotRecord& bot, uint32 diff);
    bool TryLogin(PlayerbotRecord& bot);
    void UpdateBot(PlayerbotRecord& bot, uint32 diff);
    void ReportTickStats(PlayerbotTickReport const& report);
    void ReportSlowUpdate(PlayerbotRecord const& bot, Player* player, uint64 botMicros, PlayerbotMapPass& pass);
    static uint64 MapPassKey(uint32 mapId, uint32 instanceId);
    void UpdateLogin(PlayerbotRecord& bot);
    void UpdateWorld(PlayerbotRecord& bot, uint32 diff);
    // Everything her client does once she has a player: answering the server, then her brain once she is on a map.
    void UpdateBrain(PlayerbotRecord& bot, WorldSession* session, Player* player, uint32 diff, bool answerInvite);
    void AnswerServerMovement(PlayerbotRecord& bot, Player* player, uint32 diff);
    void AnswerServerOrder(PlayerbotRecord& bot, Player* player, PlayerbotServerOrder const& order);
    void RetryServerReplies(PlayerbotRecord& bot, Player* player, uint32 diff);
    void ForgetPositionAfterTeleport(PlayerbotRecord& bot, Player* player);
    void ClearServerOrders(uint32 accountId);
    void UpdatePartyInvite(PlayerbotRecord& bot, Player* player, uint32 diff);
    bool HoldInPlace(PlayerbotRecord& bot, Player* player, uint32 diff);
    bool UpdateDeath(PlayerbotRecord& bot, Player* player, uint32 diff);
    static bool LookAroundNow(PlayerbotRecord& bot);
    void BeginDeath(PlayerbotRecord& bot, Player* player);
    void ClearDeath(PlayerbotRecord& bot);
    void ClearLivingWork(PlayerbotRecord& bot, Player* player);
    bool BeginCorpseWalk(PlayerbotRecord& bot, Player* player);
    bool BeginHealerWalk(PlayerbotRecord& bot, Player* player);
    bool UpdateSitRecover(PlayerbotRecord& bot, Player* player, uint32 diff);
    void RecoverFailedWalk(PlayerbotRecord& bot, Player* player);
    void NoteStuckAtFeet(PlayerbotRecord& bot, Player* player);
    // allowFights false keeps to talk and loot: no pull, no item used on a creature.
    bool TryImmediateWorld(PlayerbotRecord& bot, Player* player, bool walking, bool allowFights = true);
    bool TryClickFromHere(PlayerbotRecord& bot, Player* player);
    // Talks to bot.QuestTarget, and remembers a quest reward she chose to put on.
    bool TryInteractQuest(PlayerbotRecord& bot, Player* player);
    bool TryMapYellow(PlayerbotRecord& bot, Player* player, int32 skipQuestId = 0, uint32 skipEntry = 0);
    bool TrySameObjectiveYellow(PlayerbotRecord& bot, Player* player, int32 questId, uint32 entry, Position const& skipPos, ObjectGuid extraSkipGuid);
    bool TryLeaveFaceForOtherYellow(PlayerbotRecord& bot, Player* player);
    void ClearCombat(PlayerbotRecord& bot, Player* player);
    bool UpdateCombat(PlayerbotRecord& bot, Player* player, uint32 diff, bool heldInPlace = false);
    bool TryBeginFlee(PlayerbotRecord& bot, Player* player);
    bool UpdateFlee(PlayerbotRecord& bot, Player* player, uint32 diff, bool walkerUpdated);
    bool StartFleeLeg(PlayerbotRecord& bot, Player* player);
    // Her party leader when that is a human player on her map, otherwise null.
    Player* PartyLeaderToFollow(Player* player) const;
    void NoteFollowLeader(PlayerbotRecord& bot, Player* player, Player* leader);
    // True while following takes this tick. False only when she stands beside her leader, nothing is going on, and she
    // may sit down to rest.
    bool UpdateFollow(PlayerbotRecord& bot, Player* player, Player* leader, uint32 diff);
    void ClearItemLoot(PlayerbotRecord& bot);
    bool UpdateItemLoot(PlayerbotRecord& bot, Player* player, uint32 diff);
    bool BeginQuestTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::QuestTarget const& target);
    bool BeginGameObjectTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::GameObjectTarget const& target);
    bool BeginUseItemOnUnitTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::UseItemOnUnitTarget const& target);
    bool UpdateUseItem(PlayerbotRecord& bot, Player* player, uint32 diff);
    bool BeginCombatTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::CombatTarget const& target, bool mayWalk = true);
    bool BeginItemLootTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::ItemLootTarget const& target);
    bool BeginItemWork(PlayerbotRecord& bot, Player* player, PlayerbotClient::ItemLootTarget const& target);
    void ClearVendor(PlayerbotRecord& bot);
    bool BeginVendorTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::VendorTarget const& target);
    bool UpdateVendor(PlayerbotRecord& bot, Player* player, uint32 diff);
    bool TryBeginVendor(PlayerbotRecord& bot, Player* player);
    // The turn-in's rewards do not fit in her bags: a trip to a vendor to make room first. False when she cannot go now.
    bool TryBeginVendorForRoom(PlayerbotRecord& bot, Player* player, uint32 questId, uint32 slotsShort);
    // Back from making room: walk to the quest giver of the quest that did not fit.
    bool TryReturnToTurnIn(PlayerbotRecord& bot, Player* player);

    std::vector<PlayerbotRecord> _bots;
    // Where each bot's character sits in _bots. Both are filled once at startup and never change afterwards.
    std::unordered_map<ObjectGuid, std::size_t> _botIndexByGuid;
    std::unordered_set<uint32> _accountIds;
    std::unordered_set<uint64> _bridgeHandshakes;
    PlayerbotBridge _bridge;
    PlayerbotCoordinatorLease _coordinatorLease;
    PlayerbotLoginMode _loginMode = PlayerbotLoginMode::Automatic;
    PlayerbotInvitePolicy _invitePolicy = PlayerbotInvitePolicy::GameMaster;
    bool _bridgeStarted = false;
    std::unordered_map<ObjectGuid, PlayerbotRecord> _originalCommandRuntimes;
    std::unordered_map<ObjectGuid, CommandableRtsSession> _rtsSessions;
    // Filled from whichever thread sends a bot a packet; answered on the world thread.
    std::mutex _serverOrdersLock;
    std::unordered_map<uint32, std::vector<PlayerbotServerOrder>> _serverOrders;
    PlayerbotWalkMapper _walkMapper;
    PlayerbotTickStats _tickStats;
    std::string _lastTickReport;
    // Playerbots.MapThreadBrains: the brain of a bot standing on a map runs on that map's thread. Read at startup.
    bool _mapThreadBrains = false;
    // One brain pass for each map instance that has run bot brains, by map and instance id. Map threads add passes; the
    // world thread reads and drops them. The lock is held only to find, add, or drop a pass, never while one runs.
    std::mutex _mapPassesLock;
    std::unordered_map<uint64, std::unique_ptr<PlayerbotMapPass>> _mapPasses;
};

#define sPlayerbotMgr PlayerbotMgr::instance()

#endif
