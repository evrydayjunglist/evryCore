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

#include "ArchaeologyMgr.h"
#include "Containers.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "GameObjectData.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Random.h"
#include "SpellPackets.h"
#include "Timer.h"
#include <algorithm>
#include <limits>

ArchaeologyMgr::ArchaeologyMgr() = default;
ArchaeologyMgr::~ArchaeologyMgr() = default;

ArchaeologyMgr* ArchaeologyMgr::instance()
{
    static ArchaeologyMgr instance;
    return &instance;
}

void ArchaeologyMgr::LoadResearchSites()
{
    uint32 oldMSTime = getMSTime();

    _researchSitesByMap.clear();

    uint32 count = 0;
    for (ResearchSiteEntry const* site : sResearchSiteStore)
    {
        if (site->MapID < 0)
            continue;

        _researchSitesByMap[uint32(site->MapID)].push_back(site);
        ++count;
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} archaeology research sites across {} maps in {} ms",
        count, _researchSitesByMap.size(), GetMSTimeDiffToNow(oldMSTime));
}

std::vector<ResearchSiteEntry const*> const* ArchaeologyMgr::GetResearchSitesForMap(uint32 mapId) const
{
    auto itr = _researchSitesByMap.find(mapId);
    return itr != _researchSitesByMap.end() ? &itr->second : nullptr;
}

void ArchaeologyMgr::LoadDigSiteData()
{
    uint32 oldMSTime = getMSTime();

    _digSiteInfo.clear();

    //                                               0                 1                 2
    QueryResult result = WorldDatabase.Query("SELECT researchSiteId, researchBranchId, findCount FROM archaeology_dig_site");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 archaeology dig-site branch mappings. DB table `archaeology_dig_site` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();
        uint32 siteId = fields[0].GetUInt32();

        if (!sResearchSiteStore.HasRecord(siteId))
        {
            TC_LOG_ERROR("sql.sql", "Table `archaeology_dig_site` has researchSiteId {} with no matching ResearchSite.db2 entry, skipped.", siteId);
            continue;
        }

        ArchaeologyDigSiteInfo& info = _digSiteInfo[siteId];
        info.BranchID = fields[1].GetUInt32();
        info.FindCount = fields[2].GetUInt8();
        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} archaeology dig-site branch mappings in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ArchaeologyMgr::LoadResearchBranchData()
{
    uint32 oldMSTime = getMSTime();

    _findGameObjectsByBranch.clear();

    QueryResult result = WorldDatabase.Query("SELECT researchBranchId, findGameObjectId FROM archaeology_research_branch");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 archaeology research-branch policies. DB table `archaeology_research_branch` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();
        uint32 branchId = fields[0].GetUInt32();
        uint32 gameObjectId = fields[1].GetUInt32();

        ResearchBranchEntry const* branch = sResearchBranchStore.LookupEntry(branchId);
        if (!branch || !branch->CurrencyID)
        {
            TC_LOG_ERROR("sql.sql", "Table `archaeology_research_branch` has researchBranchId {} with no usable ResearchBranch.db2 entry, skipped.", branchId);
            continue;
        }

        GameObjectTemplate const* gameObjectTemplate = sObjectMgr->GetGameObjectTemplate(gameObjectId);
        if (!gameObjectTemplate || gameObjectTemplate->type != GAMEOBJECT_TYPE_CHEST ||
            gameObjectTemplate->chest.open != 1859 ||
            !gameObjectTemplate->GetLootId())
        {
            TC_LOG_ERROR("sql.sql", "Table `archaeology_research_branch` maps branch {} to invalid archaeology find GameObject {}, skipped.", branchId, gameObjectId);
            continue;
        }

        _findGameObjectsByBranch.emplace(branchId, gameObjectId);
        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} archaeology research-branch policies in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ArchaeologyMgr::LoadDigSitePoints()
{
    uint32 oldMSTime = getMSTime();

    std::unordered_map<int32, std::vector<std::pair<float, float>>> pointsByBlob;
    for (QuestPOIPointEntry const* point : sQuestPOIPointStore)
        pointsByBlob[point->QuestPOIBlobID].emplace_back(float(point->X), float(point->Y));

    uint32 points = 0, sites = 0;
    for (auto& [siteId, info] : _digSiteInfo)
    {
        ResearchSiteEntry const* site = sResearchSiteStore.LookupEntry(siteId);
        if (!site || site->QuestPOIBlobID <= 0)
            continue;

        auto itr = pointsByBlob.find(site->QuestPOIBlobID);
        if (itr == pointsByBlob.end() || itr->second.size() < 3)
            continue;

        info.Polygon = itr->second;
        ++sites;
        points += uint32(info.Polygon.size());
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} archaeology dig-site polygons ({} points) from QuestPOIPoint.db2 in {} ms",
        sites, points, GetMSTimeDiffToNow(oldMSTime));
}

ArchaeologyDigSiteInfo const* ArchaeologyMgr::GetDigSiteInfo(uint32 researchSiteId) const
{
    auto itr = _digSiteInfo.find(researchSiteId);
    return itr != _digSiteInfo.end() ? &itr->second : nullptr;
}

uint32 ArchaeologyMgr::GetFindGameObjectId(uint32 researchBranchId) const
{
    auto itr = _findGameObjectsByBranch.find(researchBranchId);
    return itr != _findGameObjectsByBranch.end() ? itr->second : 0;
}

bool ArchaeologyMgr::IsResearchBranchEnabled(uint32 researchBranchId) const
{
    return GetFindGameObjectId(researchBranchId) != 0;
}

bool ArchaeologyMgr::IsInsideDigSite(uint32 researchSiteId, float x, float y) const
{
    ArchaeologyDigSiteInfo const* info = GetDigSiteInfo(researchSiteId);
    if (!info || info->Polygon.size() < 3)
        return false;

    std::vector<std::pair<float, float>> const& poly = info->Polygon;
    bool inside = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
    {
        float xi = poly[i].first, yi = poly[i].second;
        float xj = poly[j].first, yj = poly[j].second;
        if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi))
            inside = !inside;
    }
    return inside;
}

bool ArchaeologyMgr::GenerateFindLocation(uint32 researchSiteId, float& x, float& y) const
{
    ArchaeologyDigSiteInfo const* info = GetDigSiteInfo(researchSiteId);
    if (!info || info->Polygon.size() < 3)
        return false;

    float minX = info->Polygon.front().first;
    float maxX = minX;
    float minY = info->Polygon.front().second;
    float maxY = minY;
    for (std::pair<float, float> const& p : info->Polygon)
    {
        minX = std::min(minX, p.first);
        maxX = std::max(maxX, p.first);
        minY = std::min(minY, p.second);
        maxY = std::max(maxY, p.second);
    }

    // Rejection sampling over the polygon's bounding box is uniform over the polygon.
    // ResearchSite.db2 does not carry fixed find coordinates; the generated point is persisted.
    for (uint32 attempt = 0; attempt < 1000; ++attempt)
    {
        x = frand(minX, maxX);
        y = frand(minY, maxY);
        if (IsInsideDigSite(researchSiteId, x, y))
            return true;
    }

    return false;
}

bool ArchaeologyMgr::IsSurveyableDigSite(uint32 researchSiteId) const
{
    ArchaeologyDigSiteInfo const* info = GetDigSiteInfo(researchSiteId);
    return info && info->FindCount && info->Polygon.size() >= 3 && GetFindGameObjectId(info->BranchID);
}

std::vector<uint32> ArchaeologyMgr::RollResearchSitesForMap(uint32 mapId, uint32 count, std::vector<uint32> const& exclude) const
{
    std::vector<uint32> result;

    std::vector<ResearchSiteEntry const*> const* pool = GetResearchSitesForMap(mapId);
    if (!pool || pool->empty() || !count)
        return result;

    std::vector<ResearchSiteEntry const*> picks = *pool;
    picks.erase(std::remove_if(picks.begin(), picks.end(),
        [this, &exclude](ResearchSiteEntry const* site)
        {
            return !IsSurveyableDigSite(site->ID) ||
                std::find(exclude.begin(), exclude.end(), site->ID) != exclude.end();
        }), picks.end());

    if (picks.size() > count)
        Trinity::Containers::RandomResize(picks, count);

    result.reserve(picks.size());
    for (ResearchSiteEntry const* site : picks)
        result.push_back(site->ID);

    return result;
}

uint32 ArchaeologyMgr::RollReplacementSite(uint32 mapId, std::vector<uint32> const& exclude) const
{
    std::vector<ResearchSiteEntry const*> const* pool = GetResearchSitesForMap(mapId);
    if (!pool || pool->empty())
        return 0;

    std::vector<uint32> candidates;
    for (ResearchSiteEntry const* site : *pool)
        if (IsSurveyableDigSite(site->ID) && std::find(exclude.begin(), exclude.end(), site->ID) == exclude.end())
            candidates.push_back(site->ID);

    if (candidates.empty())
        return 0;

    return Trinity::Containers::SelectRandomContainerElement(candidates);
}

uint32 ArchaeologyMgr::RollResearchProject(uint32 branchId, std::unordered_set<uint32> const& completed) const
{
    if (!IsResearchBranchEnabled(branchId))
        return 0;

    std::vector<uint32> eligible;
    for (ResearchProjectEntry const* project : sResearchProjectStore)
    {
        if (project->ResearchBranchID != branchId || project->SpellID <= 0)
            continue;

        eligible.push_back(project->ID);
    }

    if (eligible.empty())
        return 0;

    std::vector<uint32> fresh;
    for (uint32 id : eligible)
        if (!completed.count(id))
            fresh.push_back(id);

    std::vector<uint32> const& pool = !fresh.empty() ? fresh : eligible;
    return Trinity::Containers::SelectRandomContainerElement(pool);
}

ResearchProjectEntry const* ArchaeologyMgr::GetProjectBySpellId(uint32 spellId) const
{
    if (!spellId)
        return nullptr;

    for (ResearchProjectEntry const* project : sResearchProjectStore)
        if (project->SpellID > 0 && uint32(project->SpellID) == spellId)
            return project;

    return nullptr;
}

std::optional<ArchaeologySolvePlan> ArchaeologyMgr::BuildSolvePlan(
    uint32 spellId, std::vector<WorldPackets::Spells::SpellWeight> const& weights) const
{
    ResearchProjectEntry const* project = GetProjectBySpellId(spellId);
    if (!project || !IsResearchBranchEnabled(project->ResearchBranchID))
        return std::nullopt;

    ResearchBranchEntry const* branch = sResearchBranchStore.LookupEntry(project->ResearchBranchID);
    if (!branch || !branch->CurrencyID)
        return std::nullopt;

    CurrencyTypesEntry const* fragments = sCurrencyTypesStore.LookupEntry(branch->CurrencyID);
    if (!fragments || !fragments->SpellWeight)
        return std::nullopt;

    uint64 fragmentCount = 0;
    uint64 keystoneCount = 0;
    for (WorldPackets::Spells::SpellWeight const& weight : weights)
    {
        if (weight.ID <= 0 || !weight.Quantity)
            return std::nullopt;

        switch (weight.Type)
        {
            case 1:
                if (uint32(weight.ID) != branch->CurrencyID)
                    return std::nullopt;
                fragmentCount += weight.Quantity;
                break;
            case 2:
                if (branch->ItemID <= 0 || weight.ID != branch->ItemID)
                    return std::nullopt;
                keystoneCount += weight.Quantity;
                break;
            default:
                return std::nullopt;
        }
    }

    if (keystoneCount > project->NumSockets || fragmentCount > project->RequiredWeight ||
        fragmentCount > uint64(std::numeric_limits<int32>::max()))
        return std::nullopt;

    uint64 totalWeight = fragmentCount * fragments->SpellWeight;
    if (totalWeight > project->RequiredWeight)
        return std::nullopt;

    if (keystoneCount)
    {
        ItemSparseEntry const* keystone = sItemSparseStore.LookupEntry(uint32(branch->ItemID));
        if (!keystone || !keystone->SpellWeight ||
            keystone->SpellWeightCategory != fragments->SpellCategory)
            return std::nullopt;

        uint64 keystoneWeight = keystoneCount * keystone->SpellWeight;
        if (keystoneWeight > project->RequiredWeight - totalWeight)
            return std::nullopt;

        totalWeight += keystoneWeight;
    }

    if (totalWeight != project->RequiredWeight)
        return std::nullopt;

    ArchaeologySolvePlan plan;
    plan.ProjectID = project->ID;
    plan.BranchID = project->ResearchBranchID;
    plan.FragmentCurrencyID = branch->CurrencyID;
    plan.FragmentCount = uint32(fragmentCount);
    plan.KeystoneItemID = branch->ItemID > 0 ? uint32(branch->ItemID) : 0;
    plan.KeystoneCount = uint32(keystoneCount);
    plan.RequiredWeight = project->RequiredWeight;
    return plan;
}
