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

#ifndef TRINITY_ARCHAEOLOGYMGR_H
#define TRINITY_ARCHAEOLOGYMGR_H

#include "Define.h"
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

struct ResearchProjectEntry;
struct ResearchSiteEntry;
struct ArchaeologyMgrTestAccess;

namespace WorldPackets::Spells
{
    struct SpellWeight;
}

struct ArchaeologySolvePlan
{
    uint32 ProjectID = 0;
    uint32 BranchID = 0;
    uint32 FragmentCurrencyID = 0;
    uint32 FragmentCount = 0;
    uint32 KeystoneItemID = 0;
    uint32 KeystoneCount = 0;
    uint32 RequiredWeight = 0;
};

// Per dig site: which research branch its fragments belong to, and how many finds exhaust it.
// ResearchSite.db2 has no branch field. AreaPOIIconEnum is not a site-to-branch join.
// Branch mapping is server policy loaded from world table `archaeology_dig_site`.
struct ArchaeologyDigSiteInfo
{
    uint32 BranchID = 0;
    uint8 FindCount = 0;
    std::vector<std::pair<float, float>> Polygon; // dig-site boundary, world X/Y vertices in order
};

class TC_GAME_API ArchaeologyMgr
{
    private:
        ArchaeologyMgr();
        ~ArchaeologyMgr();

    public:
        ArchaeologyMgr(ArchaeologyMgr const&) = delete;
        ArchaeologyMgr(ArchaeologyMgr&&) = delete;
        ArchaeologyMgr& operator=(ArchaeologyMgr const&) = delete;
        ArchaeologyMgr& operator=(ArchaeologyMgr&&) = delete;

        static ArchaeologyMgr* instance();

        // Index dig sites (ResearchSite.db2) by map. Call once at startup after DB2 stores load.
        void LoadResearchSites();

        // Load the site-to-branch policy from `archaeology_dig_site`. Call after LoadResearchSites.
        void LoadDigSiteData();

        // Load server-owned branch policy (the branch-specific find GameObject).
        void LoadResearchBranchData();

        // Attach QuestPOIPoint.db2 vertices to each mapped site using ResearchSite.QuestPOIBlobID.
        void LoadDigSitePoints();

        bool IsInsideDigSite(uint32 researchSiteId, float x, float y) const;

        // Generate a uniformly distributed hidden-find location inside the dig-site polygon.
        bool GenerateFindLocation(uint32 researchSiteId, float& x, float& y) const;

        // True if the server has every policy needed to drive this site through Survey and loot.
        bool IsSurveyableDigSite(uint32 researchSiteId) const;

        std::vector<ResearchSiteEntry const*> const* GetResearchSitesForMap(uint32 mapId) const;

        // Randomly pick up to `count` distinct surveyable dig-site IDs from a map's pool.
        std::vector<uint32> RollResearchSitesForMap(uint32 mapId, uint32 count, std::vector<uint32> const& exclude = {}) const;

        // Pick one random surveyable dig site on a map that is not in `exclude`. Returns 0 if none.
        uint32 RollReplacementSite(uint32 mapId, std::vector<uint32> const& exclude) const;

        // Pick a research project for a branch. Prefers projects not in `completed`, then any.
        // Uniform among eligible. Rarity is a DB2 field for criteria modifiers, not a roll weight.
        uint32 RollResearchProject(uint32 branchId, std::unordered_set<uint32> const& completed) const;

        ResearchProjectEntry const* GetProjectBySpellId(uint32 spellId) const;

        std::optional<ArchaeologySolvePlan> BuildSolvePlan(uint32 spellId, std::vector<WorldPackets::Spells::SpellWeight> const& weights) const;

        ArchaeologyDigSiteInfo const* GetDigSiteInfo(uint32 researchSiteId) const;

        uint32 GetFindGameObjectId(uint32 researchBranchId) const;

        bool IsResearchBranchEnabled(uint32 researchBranchId) const;

    private:
        friend struct ArchaeologyMgrTestAccess;

        std::unordered_map<uint32 /*mapId*/, std::vector<ResearchSiteEntry const*>> _researchSitesByMap;
        std::unordered_map<uint32 /*researchSiteId*/, ArchaeologyDigSiteInfo> _digSiteInfo;
        std::unordered_map<uint32 /*researchBranchId*/, uint32 /*findGameObjectId*/> _findGameObjectsByBranch;
};

#define sArchaeologyMgr ArchaeologyMgr::instance()

#endif
