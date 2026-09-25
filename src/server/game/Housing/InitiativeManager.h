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

#ifndef TRINITYCORE_INITIATIVE_MANAGER_H
#define TRINITYCORE_INITIATIVE_MANAGER_H

#include "Define.h"
#include "CriteriaHandler.h"
#include "HousingDefines.h"
#include "ObjectGuid.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <unordered_map>
#include <vector>

class Neighborhood;
class Player;
class WorldObject;
class WorldSession;
struct CriteriaEntry;
struct CriteriaTreeEntry;
struct InitiativeTaskEntry;

// Status of an individual task within an initiative
enum InitiativeTaskStatus : uint32
{
    INITIATIVE_TASK_STATUS_NOT_STARTED  = 0,
    INITIATIVE_TASK_STATUS_IN_PROGRESS  = 1,
    INITIATIVE_TASK_STATUS_COMPLETE     = 2
};

// Tracks a single task's progress for a player within a neighborhood initiative
struct InitiativeTaskProgress
{
    uint32 TaskID = 0;
    uint32 Progress = 0;
    InitiativeTaskStatus Status = INITIATIVE_TASK_STATUS_NOT_STARTED;
    uint32 CompletionTime = 0;             // Unix time the task was completed, 0 while it is not
};

// Runtime state for an active initiative instance in a neighborhood
struct ActiveInitiative
{
    uint64 DbId = 0;                       // neighborhood_initiatives.id
    uint64 NeighborhoodGuid = 0;           // neighborhood_initiatives.neighborhoodGuid
    uint32 InitiativeID = 0;               // NeighborhoodInitiative.db2 entry ID
    uint32 StartTime = 0;                  // Unix timestamp
    float  Progress = 0.0f;                // 0.0 to 1.0
    bool   Completed = false;

    // Per-task progress (TaskID -> progress)
    std::unordered_map<uint32, InitiativeTaskProgress> TaskProgress;

    // Milestones reached (MilestoneIndex -> reached)
    std::unordered_map<uint32, bool> MilestonesReached;

    // Contributions by Battle.net account: tasks and their progress are shared by the Warband (Wowhead's endeavors
    // guide: "The Tasks available to your character are Warband-wide, as is progress on those Tasks").
    // bnetAccountId -> taskId -> amount. Amounts are fractional, as retail's are (hbcd3 1305730 logs 2.2).
    std::unordered_map<uint32, std::unordered_map<uint32, float>> AccountContributions;
    // The character of each account that contributed last, which the activity log names beside the account.
    std::unordered_map<uint32, ObjectGuid::LowType> ContributorCharacters;

    // Coffer claims by Battle.net account, "once per Warband" (Wowhead's endeavors guide):
    // milestoneIndex -> accounts that claimed it
    std::unordered_map<uint32, std::set<uint32>> RewardClaims;
};

// Cached DB2 data for an initiative's tasks
struct InitiativeTaskData
{
    uint32 TaskID = 0;
    int32  CriteriaTreeID = 0;              // DB2: CriteriaTreeID FK->CriteriaTree (task completion criteria)
    int32  QuestID = 0;                     // DB2: QuestID FK->QuestV2 (associated quest)
    int32  ProgressContributionAmount = 0;  // DB2: ProgressContributionAmount (contribution weight per completion)
    int32  RepetitionDampeningCurveID = 0;  // DB2: RepetitionContributionDampeningCurve FK->Curve
    int32  SortOrder = 0;                   // From InitiativeXTask join
};

// One line of the endeavor activity log: a completed task of the active endeavor and an account that contributed to it.
struct InitiativeActivityLogLine
{
    uint32 TaskID = 0;
    uint32 BnetAccountId = 0;
    ObjectGuid::LowType CharacterGuid = 0;      // the account's character that contributed last
    uint32 CompletionTime = 0;
    float Contribution = 0.0f;
};

// The criteria trees of the endeavor tasks. CriteriaMgr keeps only the trees of achievements, scenario steps and quest
// objectives, and no task tree hangs under any of those, so the tasks keep their own copy of their trees and criteria.
// It is built once at startup and only read afterwards, so every map thread may read it.
class TC_GAME_API InitiativeTaskCriteria
{
public:
    struct TaskLink
    {
        uint32 TaskID = 0;
        CriteriaTree const* Node = nullptr;     // the tree node that holds the criteria, whose faction flags apply
    };

    InitiativeTaskCriteria() = default;
    InitiativeTaskCriteria(InitiativeTaskCriteria const&) = delete;
    InitiativeTaskCriteria& operator=(InitiativeTaskCriteria const&) = delete;

    // taskRoots pairs each task id with its CriteriaTreeID; trees is every CriteriaTree row. A criteria id the lookup
    // does not know leaves its node without a criteria. A task whose root is not among the trees is listed as missing.
    void Build(std::vector<std::pair<uint32, uint32>> const& taskRoots, std::span<CriteriaTreeEntry const* const> trees,
        std::function<CriteriaEntry const*(uint32)> const& criteriaLookup,
        std::function<ModifierTreeNode const*(uint32)> const& modifierLookup);

    CriteriaTree const* GetTaskTree(uint32 taskId) const;
    CriteriaList const& GetCriteriaByType(CriteriaType type) const;
    std::vector<TaskLink> const* GetTasksForCriteria(uint32 criteriaId) const;
    std::vector<uint32> const& GetTasksWithMissingTree() const { return _tasksWithMissingTree; }
    std::size_t GetCriteriaCount() const { return _criteria.size(); }

private:
    std::unordered_map<uint32, CriteriaTree> _trees;
    std::unordered_map<uint32, Criteria> _criteria;
    std::unordered_map<uint32, CriteriaTree const*> _taskRoots;
    std::unordered_map<uint32, std::vector<TaskLink>> _tasksByCriteria;
    std::vector<CriteriaList> _criteriaByType = std::vector<CriteriaList>(size_t(CriteriaType::Count));
    std::vector<uint32> _tasksWithMissingTree;
};

// Cached DB2 data for an initiative's milestones
struct InitiativeMilestoneData
{
    uint32 MilestoneID = 0;
    int32  MilestoneOrderIndex = 0;      // DB2: MilestoneOrderIndex
    float  RequiredContributionAmount = 0.0f; // DB2: RequiredContributionAmount
    int32  Field_3 = 0;                  // DB2: Field_12_0_0_63534_003
};

// Endeavor state is changed only on the world thread: in Update, in the endeavor packet handlers (none of which runs on
// a map thread) and when Update applies the deeds that map threads queue through OnPlayerCriteriaEvent. Map threads only
// read it, and never while world-thread code runs, because World::Update waits for the map updates to finish.
class TC_GAME_API InitiativeManager
{
public:
    static InitiativeManager& Instance();

    InitiativeManager(InitiativeManager const&) = delete;
    InitiativeManager(InitiativeManager&&) = delete;
    InitiativeManager& operator=(InitiativeManager const&) = delete;
    InitiativeManager& operator=(InitiativeManager&&) = delete;

    // Lifecycle
    void Initialize();
    void LoadFromDB();
    void Update(uint32 diff);

    // DB2 data cache
    void BuildDB2IndexMaps();
    std::vector<InitiativeTaskData> GetTasksForInitiative(uint32 initiativeID) const;
    std::vector<InitiativeMilestoneData> GetMilestonesForCycle(uint32 cycleID) const;
    uint32 GetActiveCycleForInitiative(uint32 initiativeID) const;

    // Initiative lifecycle
    ActiveInitiative* StartInitiative(uint64 neighborhoodGuid, uint32 initiativeID);
    ActiveInitiative* GetActiveInitiative(uint64 neighborhoodGuid) const;
    std::vector<ActiveInitiative const*> GetInitiativesForNeighborhood(uint64 neighborhoodGuid) const;
    void CompleteInitiative(uint64 neighborhoodGuid, uint32 initiativeID);

    // Task progress
    void ClearTaskCriteria(uint64 neighborhoodGuid, uint32 initiativeID, uint32 taskID);

    // Called once for each criteria event of a player, from Player::UpdateCriteria on her map thread. Checks the event
    // against the criteria of the endeavor task trees with her achievement criteria handler, as her achievements are
    // checked, and queues one unit of progress for each task it meets. Update applies the queue on the world thread, to
    // the endeavor of the neighborhood she had chosen when the deed happened.
    void OnPlayerCriteriaEvent(Player* player, CriteriaHandler const& checker, CriteriaType type, uint64 miscValue1,
        uint64 miscValue2, uint64 miscValue3, WorldObject const* ref);

    InitiativeTaskCriteria const& GetTaskCriteria() const { return _taskCriteria; }

    // Reward queries and distribution
    bool HasUnclaimedRewards(uint64 neighborhoodGuid, uint32 initiativeID, uint32 bnetAccountId) const;
    bool ClaimMilestoneReward(uint64 neighborhoodGuid, uint32 initiativeID, uint32 milestoneIndex, Player* player);

    // Contribution queries, by Battle.net account
    float GetAccountContribution(uint64 neighborhoodGuid, uint32 initiativeID, uint32 bnetAccountId) const;
    std::vector<std::pair<uint32, float>> GetTopContributors(uint64 neighborhoodGuid, uint32 initiativeID, uint32 limit) const;
    void UpdatePlayerInitiativeFavor(Player* player, uint64 neighborhoodGuid);

    // Send packets to a session
    void SendInitiativeServiceStatus(WorldSession* session, bool enabled) const;
    void SendPlayerInitiativeInfo(WorldSession* session, ObjectGuid const& neighborhoodGuid, uint64 neighborhoodLowGuid) const;
    void SendActivityLog(WorldSession* session, ObjectGuid const& neighborhoodGuid, uint64 neighborhoodLowGuid) const;
    // The activity log of an endeavor: one line per account that contributed to each of its completed tasks, with the
    // time that task was completed. Each account's lines come together, oldest first.
    static std::vector<InitiativeActivityLogLine> BuildActivityLog(ActiveInitiative const& initiative);
    void SendInitiativeRewardsResult(WorldSession* session, uint32 result) const;

    // Broadcast packets to all neighborhood members
    void BroadcastTaskComplete(Neighborhood* neighborhood, uint32 initiativeID, uint32 taskID) const;
    void BroadcastInitiativeComplete(Neighborhood* neighborhood, uint32 initiativeID) const;
    void BroadcastRewardAvailable(Neighborhood* neighborhood, uint32 initiativeID, uint32 milestoneIndex) const;
    // Login only: SMSG_INITIATIVE_REWARD_AVAILABLE for the player's houses with an unclaimed reached milestone.
    void SendRewardsAvailable(Player* player) const;
    // SMSG_CLEAR_INITIATIVE_TASK_CRITERIA_PROGRESS (0x420367) — tells the client to zero its
    // cached progress for the given leaf CriteriaIDs. Sent whenever server-side task progress
    // is reset to zero, otherwise the client keeps rendering the pre-reset bars.
    void BroadcastClearTaskCriteriaProgress(Neighborhood* neighborhood, std::vector<uint64> const& criteriaIDs) const;

    // Starts an endeavor in each public neighborhood that has none. Guild and charter neighborhoods wait for one of
    // their managers to pick one at the Steward.
    void CheckAndStartInitiatives();

private:
    InitiativeManager() = default;

    // A deed a map thread queued for the world thread.
    struct PendingTaskCredit
    {
        uint32 BnetAccountId = 0;
        ObjectGuid CharacterGuid;
        uint64 NeighborhoodGuid = 0;
        uint32 TaskID = 0;
        uint32 CriteriaID = 0;
    };

    // Whose a unit of task progress is. ContributorPlayer is the character while she is online, else null.
    struct TaskContributor
    {
        uint32 BnetAccountId = 0;
        ObjectGuid CharacterGuid;
        Player* ContributorPlayer = nullptr;
    };

    void BuildTaskCriteria();
    void ApplyPendingTaskCredits();
    void UpdateTaskProgress(uint64 neighborhoodGuid, uint32 initiativeID, uint32 taskID, uint32 progressDelta, TaskContributor const& contributor);

    // False, with an error logged, for an endeavor without a database id.
    static bool HasSavedId(uint64 initiativeDbId);
    void PersistInitiative(ActiveInitiative const& initiative);
    void PersistTaskProgress(ActiveInitiative const& initiative);
    void PersistSingleTaskProgress(uint64 initiativeDbId, InitiativeTaskProgress const& taskProgress);
    void PersistMilestoneReached(uint64 initiativeDbId, uint32 milestoneIndex, uint32 reachedTime);
    void PersistRewardClaim(uint64 initiativeDbId, uint32 milestoneIndex, uint32 bnetAccountId);
    void PersistContribution(uint64 initiativeDbId, uint32 bnetAccountId, ObjectGuid::LowType characterGuid, uint32 taskId, float amount);
    void CheckMilestones(ActiveInitiative& initiative, Neighborhood* neighborhood);
    void GrantMilestoneRewards(Player* player, uint64 neighborhoodGuid, uint32 milestoneID);

    // How many criteria hits finish this task. This is the task's CriteriaTree root Amount — NOT
    // InitiativeTask.ProgressContributionAmount, which is the contribution weight one completion is
    // worth. Returns 1 when the tree carries no amount (a single criteria hit finishes the task).
    uint32 GetTaskTargetCount(uint32 taskID) const;

    // InitiativeTask.RepetitionContributionDampeningCurve evaluated at alreadyContributed. Returns a
    // multiplier in (0, 1]; returns 1.0 (no dampening) when the task has no curve or the curve has no
    // points, so a missing curve can never zero a contribution out.
    static float GetRepetitionDampening(InitiativeTaskEntry const* taskEntry, float alreadyContributed);

    // Pays House XP ("Favor") for an endeavor task contribution to the account's house in the endeavor's neighborhood,
    // capped per cycle by InitiativeCycle.HouseXPCap (2250 in every 12.1 row). Takes the account's before and after
    // contribution totals so the cap can be applied without any extra persisted state.
    void GrantInitiativeTaskFavor(Player* player, uint64 neighborhoodGuid, uint32 initiativeID, float contributionBefore, float contributionAfter) const;
    uint32 SelectWeightedCycle(uint32 initiativeID) const;
    uint32 CalculateMaxPoints(uint32 initiativeID) const;
    // Leaf Criteria IDs reachable from a task's CriteriaTree (all tasks of an initiative when
    // taskID == 0). These are exactly the IDs the client indexes its task progress cache by.
    std::vector<uint64> CollectTaskCriteriaIDs(uint32 initiativeID, uint32 taskID) const;

    // Active initiatives: neighborhoodGuid -> list of active initiatives
    std::unordered_map<uint64, std::vector<std::unique_ptr<ActiveInitiative>>> _activeInitiatives;

    // DB2 index maps (built once during Initialize)
    // InitiativeID -> list of tasks
    std::unordered_map<uint32, std::vector<InitiativeTaskData>> _initiativeTasks;
    // CycleID -> list of milestones
    std::unordered_map<uint32, std::vector<InitiativeMilestoneData>> _cycleMilestones;
    // InitiativeID -> active cycle ID
    std::unordered_map<uint32, uint32> _initiativeActiveCycle;
    // CycleID -> list of priority entries (for weighted selection)
    std::unordered_map<uint32, std::vector<std::pair<uint32, int32>>> _cyclePriorities; // cycleID -> [(initiativeID, weight)]

    // The task trees, built once at startup.
    InitiativeTaskCriteria _taskCriteria;

    // Deeds map threads met, waiting for Update on the world thread.
    std::mutex _pendingCreditsLock;
    std::vector<PendingTaskCredit> _pendingCredits;

    // The id the next endeavor is saved with. The server picks it, as it does for houses, so a new endeavor can save
    // its progress straight away; the insert stays on the asynchronous queue, ahead of every later write for that id.
    std::atomic<uint64> _nextInitiativeDbId{1};

    // Update timer
    uint32 _updateTimer = 0;
    static constexpr uint32 UPDATE_INTERVAL_MS = 60000; // Check every 60 seconds
};

#define sInitiativeManager InitiativeManager::Instance()

#endif // TRINITYCORE_INITIATIVE_MANAGER_H
