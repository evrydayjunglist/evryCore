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

#include "tc_catch2.h"
#include "CriteriaHandler.h"
#include "DB2Structure.h"
#include "InitiativeManager.h"
#include <algorithm>
#include <deque>
#include <unordered_map>
#include <vector>

namespace
{
    // Hand-made CriteriaTree and Criteria rows. A deque keeps the rows where they are while more are added.
    struct CriteriaRows
    {
        std::deque<CriteriaTreeEntry> Trees;
        std::unordered_map<uint32, CriteriaEntry> Criteria;

        CriteriaTreeEntry const& AddTree(uint32 id, uint32 parent, uint32 amount, int32 op, uint32 criteriaId, int32 orderIndex = 0, int32 flags = 0)
        {
            CriteriaTreeEntry& tree = Trees.emplace_back();
            tree = CriteriaTreeEntry{};
            tree.ID = id;
            tree.Parent = parent;
            tree.Amount = amount;
            tree.Operator = op;
            tree.CriteriaID = criteriaId;
            tree.OrderIndex = orderIndex;
            tree.Flags = flags;
            return tree;
        }

        void AddCriteria(uint32 id, CriteriaType type, int32 asset, uint32 modifierTreeId = 0)
        {
            CriteriaEntry& criteria = Criteria[id];
            criteria = CriteriaEntry{};
            criteria.ID = id;
            criteria.Type = int16(type);
            criteria.Asset.ID = asset;
            criteria.ModifierTreeId = modifierTreeId;
        }

        std::vector<CriteriaTreeEntry const*> TreePointers() const
        {
            std::vector<CriteriaTreeEntry const*> pointers;
            for (CriteriaTreeEntry const& tree : Trees)
                pointers.push_back(&tree);
            return pointers;
        }

        void Build(InitiativeTaskCriteria& taskCriteria, std::vector<std::pair<uint32, uint32>> const& taskRoots,
            std::unordered_map<uint32, ModifierTreeNode> const& modifiers = {}) const
        {
            std::vector<CriteriaTreeEntry const*> trees = TreePointers();
            taskCriteria.Build(taskRoots, trees,
                [this](uint32 id) -> CriteriaEntry const*
                {
                    auto itr = Criteria.find(id);
                    return itr != Criteria.end() ? &itr->second : nullptr;
                },
                [&modifiers](uint32 id) -> ModifierTreeNode const*
                {
                    auto itr = modifiers.find(id);
                    return itr != modifiers.end() ? &itr->second : nullptr;
                });
        }
    };
}

TEST_CASE("Endeavor tasks keep their own criteria trees", "[Housing][Endeavors]")
{
    CriteriaRows rows;

    // Task 168 "Home: Pollinate Flowers" in the 12.1 client: tree 221878 (Sum, amount 20, flags 8193) over one
    // node 221879 with criteria 111738, kill creature 258523, modifier tree 413814.
    rows.AddTree(221878, 0, 20, 5, 0, 0, 8193);
    rows.AddTree(221879, 221878, 1, 0, 111738);
    rows.AddCriteria(111738, CriteriaType::KillCreature, 258523, 413814);

    // Task 3 "[DNT] Kill Bloodscale Naga": tree 210311 (Complete At Least, amount 20) over six kill criteria. Two of
    // them are listed here, out of their OrderIndex order.
    rows.AddTree(210311, 0, 20, 8, 0);
    rows.AddTree(210313, 210311, 1, 0, 105197, 1);
    rows.AddTree(210312, 210311, 1, 0, 105208, 0);
    rows.AddCriteria(105208, CriteriaType::KillCreature, 20122);
    rows.AddCriteria(105197, CriteriaType::KillCreature, 20091);

    // A made-up task whose tree shares criteria 105208 with task 3 through a node of its own, holds a Horde-only node,
    // and has a node whose criteria is not in Criteria.db2.
    rows.AddTree(900000, 0, 2, 4, 0);
    rows.AddTree(900001, 900000, 1, 0, 105208, 0, int32(CriteriaTreeFlags::HordeOnly));
    rows.AddTree(900002, 900000, 1, 0, 999999, 1);

    // A tree no task uses.
    rows.AddTree(800000, 0, 1, 0, 800001);
    rows.AddCriteria(800001, CriteriaType::KillCreature, 1);

    std::unordered_map<uint32, ModifierTreeNode> modifiers;
    modifiers[413814] = ModifierTreeNode{};

    InitiativeTaskCriteria taskCriteria;
    // Task 25's tree 210664 is one of the four task trees missing from the 12.1 CriteriaTree.db2.
    rows.Build(taskCriteria, { { 168, 221878 }, { 3, 210311 }, { 900, 900000 }, { 25, 210664 } }, modifiers);

    SECTION("Each task finds its root, whose amount is the task's target")
    {
        CriteriaTree const* pollinate = taskCriteria.GetTaskTree(168);
        REQUIRE(pollinate);
        REQUIRE(pollinate->ID == 221878);
        REQUIRE(pollinate->Entry->Amount == 20);
        REQUIRE(pollinate->Children.size() == 1);
        REQUIRE(pollinate->Children[0]->Criteria);
        REQUIRE(pollinate->Children[0]->Criteria->ID == 111738);
        REQUIRE(pollinate->Children[0]->Criteria->Modifier == &modifiers[413814]);

        CriteriaTree const* naga = taskCriteria.GetTaskTree(3);
        REQUIRE(naga);
        REQUIRE(naga->Children.size() == 2);
        REQUIRE(naga->Children[0]->ID == 210312);
        REQUIRE(naga->Children[1]->ID == 210313);
    }

    SECTION("A task whose tree is missing is listed and has no tree")
    {
        REQUIRE(taskCriteria.GetTaskTree(25) == nullptr);
        REQUIRE(taskCriteria.GetTasksWithMissingTree() == std::vector<uint32>{ 25 });
        REQUIRE(taskCriteria.GetTaskTree(12345) == nullptr);
    }

    SECTION("Criteria are indexed by type, each once, and only those of task trees")
    {
        // 111738, 105208 and 105197; 105208 is in two trees, 800001 is in no task's tree and 999999 does not exist.
        REQUIRE(taskCriteria.GetCriteriaCount() == 3);
        CriteriaList const& kills = taskCriteria.GetCriteriaByType(CriteriaType::KillCreature);
        REQUIRE(kills.size() == 3);
        REQUIRE(taskCriteria.GetCriteriaByType(CriteriaType::LootItem).empty());
        REQUIRE(taskCriteria.GetCriteriaByType(CriteriaType::Count).empty());
        REQUIRE(taskCriteria.GetTasksForCriteria(800001) == nullptr);

        CriteriaTree const* made = taskCriteria.GetTaskTree(900);
        REQUIRE(made);
        REQUIRE(made->Children.size() == 2);
        REQUIRE(made->Children[1]->Criteria == nullptr);
    }

    SECTION("A criteria shared by two tasks leads to both, each with the node that holds it")
    {
        std::vector<InitiativeTaskCriteria::TaskLink> const* links = taskCriteria.GetTasksForCriteria(105208);
        REQUIRE(links);
        REQUIRE(links->size() == 2);

        std::unordered_map<uint32, uint32> nodeByTask;
        for (InitiativeTaskCriteria::TaskLink const& link : *links)
            nodeByTask[link.TaskID] = link.Node->ID;
        REQUIRE(nodeByTask.at(3) == 210312);
        REQUIRE(nodeByTask.at(900) == 900001);

        // The Horde-only flag stays on the node, where the dispatch reads it.
        auto horde = std::ranges::find_if(*links, [](InitiativeTaskCriteria::TaskLink const& link) { return link.TaskID == 900; });
        REQUIRE(horde->Node->Entry->GetFlags().HasFlag(CriteriaTreeFlags::HordeOnly));
    }

    SECTION("Building again starts over")
    {
        rows.Build(taskCriteria, { { 168, 221878 } }, modifiers);
        REQUIRE(taskCriteria.GetTaskTree(3) == nullptr);
        REQUIRE(taskCriteria.GetTasksWithMissingTree().empty());
        REQUIRE(taskCriteria.GetCriteriaCount() == 1);
        REQUIRE(taskCriteria.GetCriteriaByType(CriteriaType::KillCreature).size() == 1);
    }
}

TEST_CASE("The endeavor activity log lists the completed tasks of the running endeavor", "[Housing][Endeavors]")
{
    ActiveInitiative initiative;
    initiative.InitiativeID = 19;
    initiative.StartTime = 1000;

    auto addTask = [&initiative](uint32 taskId, uint32 progress, InitiativeTaskStatus status, uint32 completionTime)
    {
        InitiativeTaskProgress& task = initiative.TaskProgress[taskId];
        task.TaskID = taskId;
        task.Progress = progress;
        task.Status = status;
        task.CompletionTime = completionTime;
    };

    // Task 168 completed at 0x6A64F4C8, the time retail logged for it (hbcd3 1305730), by two accounts.
    addTask(168, 20, INITIATIVE_TASK_STATUS_COMPLETE, 0x6A64F4C8);
    // Task 12 completed earlier, by account 9 only.
    addTask(12, 10, INITIATIVE_TASK_STATUS_COMPLETE, 0x6A64E000);
    // A task still running is not in the log, whoever helped it.
    addTask(84, 3, INITIATIVE_TASK_STATUS_IN_PROGRESS, 0);
    // A completed task nobody is known to have helped has no line: retail's log has no line without an account.
    addTask(5, 30, INITIATIVE_TASK_STATUS_COMPLETE, 0x6A64F000);

    initiative.AccountContributions[9][168] = 1.5f;
    initiative.AccountContributions[9][12] = 4.383f;
    initiative.AccountContributions[9][84] = 4.0f;
    initiative.AccountContributions[7][168] = 2.2f;
    initiative.ContributorCharacters[7] = 42;
    initiative.ContributorCharacters[9] = 43;

    std::vector<InitiativeActivityLogLine> log = InitiativeManager::BuildActivityLog(initiative);
    REQUIRE(log.size() == 3);

    // Each account's lines together, oldest first, as in the 12.1 capture's log.
    REQUIRE(log[0].TaskID == 168);
    REQUIRE(log[0].BnetAccountId == 7);
    REQUIRE(log[0].CharacterGuid == 42);
    REQUIRE(log[0].CompletionTime == 0x6A64F4C8);
    REQUIRE(log[0].Contribution == 2.2f);

    REQUIRE(log[1].TaskID == 12);
    REQUIRE(log[1].BnetAccountId == 9);
    REQUIRE(log[1].CharacterGuid == 43);
    REQUIRE(log[1].CompletionTime == 0x6A64E000);
    REQUIRE(log[1].Contribution == 4.383f);

    REQUIRE(log[2].TaskID == 168);
    REQUIRE(log[2].BnetAccountId == 9);
    REQUIRE(log[2].CharacterGuid == 43);
    REQUIRE(log[2].CompletionTime == 0x6A64F4C8);
    REQUIRE(log[2].Contribution == 1.5f);

    for (InitiativeActivityLogLine const& line : log)
        REQUIRE(line.BnetAccountId != 0);

    SECTION("The log does not depend on whether the whole endeavor is complete")
    {
        initiative.Completed = true;
        REQUIRE(InitiativeManager::BuildActivityLog(initiative).size() == 3);
    }

    SECTION("An endeavor with no completed task has an empty log")
    {
        ActiveInitiative fresh;
        fresh.TaskProgress[168].TaskID = 168;
        REQUIRE(InitiativeManager::BuildActivityLog(fresh).empty());
    }
}
