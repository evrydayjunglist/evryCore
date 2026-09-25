/* This file is part of the TrinityCore Project. See AUTHORS file for Copyright information.
 * This program is free software, licensed under the GNU General Public License, version 2 or later.
 */

#include "tc_catch2.h"
#include "DB2FileSystemSource.h"
#include "DB2LoadInfo.h"
#include "DB2Structure.h"
#include <cstdlib>
#include <filesystem>
#include <memory>

namespace
{
    struct HousingDataSpec
    {
        char const* Name;
        DB2LoadInfo const* LoadInfo;
        std::size_t RecordSize;
    };

    HousingDataSpec const HousingData[] =
    {
        { "DataTagXHouseDecorRecord", &DataTagXHouseDecorRecordLoadInfo::Instance, sizeof(DataTagXHouseDecorRecordEntry) },
        { "DecorCategory", &DecorCategoryLoadInfo::Instance, sizeof(DecorCategoryEntry) },
        { "DecorDyeSlot", &DecorDyeSlotLoadInfo::Instance, sizeof(DecorDyeSlotEntry) },
        { "DecorSubcategory", &DecorSubcategoryLoadInfo::Instance, sizeof(DecorSubcategoryEntry) },
        { "DecorXDecorSubcategory", &DecorXDecorSubcategoryLoadInfo::Instance, sizeof(DecorXDecorSubcategoryEntry) },
        { "DyeColor", &DyeColorLoadInfo::Instance, sizeof(DyeColorEntry) },
        { "DyeColorCategory", &DyeColorCategoryLoadInfo::Instance, sizeof(DyeColorCategoryEntry) },
        { "ExteriorComponent", &ExteriorComponentLoadInfo::Instance, sizeof(ExteriorComponentEntry) },
        { "ExteriorComponentExitPoint", &ExteriorComponentExitPointLoadInfo::Instance, sizeof(ExteriorComponentExitPointEntry) },
        { "ExteriorComponentGroup", &ExteriorComponentGroupLoadInfo::Instance, sizeof(ExteriorComponentGroupEntry) },
        { "ExteriorComponentGroupXHook", &ExteriorComponentGroupXHookLoadInfo::Instance, sizeof(ExteriorComponentGroupXHookEntry) },
        { "ExteriorComponentHook", &ExteriorComponentHookLoadInfo::Instance, sizeof(ExteriorComponentHookEntry) },
        { "ExteriorComponentType", &ExteriorComponentTypeLoadInfo::Instance, sizeof(ExteriorComponentTypeEntry) },
        { "ExteriorComponentXGroup", &ExteriorComponentXGroupLoadInfo::Instance, sizeof(ExteriorComponentXGroupEntry) },
        { "House", &HouseLoadInfo::Instance, sizeof(HouseEntry) },
        { "HouseDecor", &HouseDecorLoadInfo::Instance, sizeof(HouseDecorEntry) },
        { "HouseDecorMaterial", &HouseDecorMaterialLoadInfo::Instance, sizeof(HouseDecorMaterialEntry) },
        { "HouseDecorThemeSet", &HouseDecorThemeSetLoadInfo::Instance, sizeof(HouseDecorThemeSetEntry) },
        { "HouseExteriorWmoData", &HouseExteriorWmoDataLoadInfo::Instance, sizeof(HouseExteriorWmoDataEntry) },
        { "HouseLevelData", &HouseLevelDataLoadInfo::Instance, sizeof(HouseLevelDataEntry) },
        { "HouseLevelRewardInfo", &HouseLevelRewardInfoLoadInfo::Instance, sizeof(HouseLevelRewardInfoEntry) },
        { "HouseRoom", &HouseRoomLoadInfo::Instance, sizeof(HouseRoomEntry) },
        { "HouseTheme", &HouseThemeLoadInfo::Instance, sizeof(HouseThemeEntry) },
        { "InitiativeCycle", &InitiativeCycleLoadInfo::Instance, sizeof(InitiativeCycleEntry) },
        { "InitiativeCyclePriority", &InitiativeCyclePriorityLoadInfo::Instance, sizeof(InitiativeCyclePriorityEntry) },
        { "InitiativeMilestone", &InitiativeMilestoneLoadInfo::Instance, sizeof(InitiativeMilestoneEntry) },
        { "InitiativeReward", &InitiativeRewardLoadInfo::Instance, sizeof(InitiativeRewardEntry) },
        { "InitiativeRewardXMilestone", &InitiativeRewardXMilestoneLoadInfo::Instance, sizeof(InitiativeRewardXMilestoneEntry) },
        { "InitiativeTask", &InitiativeTaskLoadInfo::Instance, sizeof(InitiativeTaskEntry) },
        { "InitiativeXTask", &InitiativeXTaskLoadInfo::Instance, sizeof(InitiativeXTaskEntry) },
        { "NeighborhoodInitiative", &NeighborhoodInitiativeLoadInfo::Instance, sizeof(NeighborhoodInitiativeEntry) },
        { "NeighborhoodMap", &NeighborhoodMapLoadInfo::Instance, sizeof(NeighborhoodMapEntry) },
        { "NeighborhoodNameGen", &NeighborhoodNameGenLoadInfo::Instance, sizeof(NeighborhoodNameGenEntry) },
        { "NeighborhoodPlot", &NeighborhoodPlotLoadInfo::Instance, sizeof(NeighborhoodPlotEntry) },
        { "RoomComponent", &RoomComponentLoadInfo::Instance, sizeof(RoomComponentEntry) },
        { "RoomComponentOption", &RoomComponentOptionLoadInfo::Instance, sizeof(RoomComponentOptionEntry) },
        { "RoomComponentOptionTexture", &RoomComponentOptionTextureLoadInfo::Instance, sizeof(RoomComponentOptionTextureEntry) },
        { "RoomComponentTexture", &RoomComponentTextureLoadInfo::Instance, sizeof(RoomComponentTextureEntry) },
        { "RoomWmoData", &RoomWmoDataLoadInfo::Instance, sizeof(RoomWmoDataEntry) },
    };
}

TEST_CASE("Housing structures and hotfix fields match the selected client metadata", "[Housing][DB2]")
{
    for (HousingDataSpec const& spec : HousingData)
    {
        INFO(spec.Name);
        DB2Meta const& meta = *spec.LoadInfo->Meta;
        REQUIRE(spec.LoadInfo->FieldCount == meta.GetDbFieldCount());
        REQUIRE(spec.RecordSize == meta.GetRecordSize());
        uint32 field = 0;
        if (!meta.HasIndexFieldInData())
        {
            CHECK(spec.LoadInfo->Fields[field].Type == FT_INT);
            CHECK_FALSE(spec.LoadInfo->Fields[field].IsSigned);
            ++field;
        }
        for (uint32 index = 0; index < meta.FieldCount; ++index)
            for (uint32 element = 0; element < meta.Fields[index].ArraySize; ++element, ++field)
            {
                INFO(spec.LoadInfo->Fields[field].Name);
                CHECK(spec.LoadInfo->Fields[field].Type == meta.Fields[index].Type);
                CHECK(spec.LoadInfo->Fields[field].IsSigned == meta.IsSignedField(index));
            }
    }
}

TEST_CASE("Installed housing DB2 files load through the production loader", "[.][Housing][DB2][installed-data]")
{
    char const* directory = std::getenv("TC_HOUSING_DB2_DIR");
    if (!directory || !*directory)
        SKIP("Set TC_HOUSING_DB2_DIR to the installed data/dbc/enUS directory for this read-only check.");

    for (HousingDataSpec const& spec : HousingData)
    {
        INFO(spec.Name);
        std::filesystem::path path = std::filesystem::path(directory) / (std::string(spec.Name) + ".db2");
        DB2FileSystemSource source(path.string());
        REQUIRE(source.IsOpen());
        DB2FileLoader loader;
        REQUIRE_NOTHROW(loader.Load(&source, spec.LoadInfo));
        uint32 indexSize = 0;
        char** index = nullptr;
        std::unique_ptr<char[]> data(loader.AutoProduceData(indexSize, index));
        std::unique_ptr<char*[]> indexOwner(index);
        REQUIRE_NOTHROW(loader.AutoProduceRecordCopies(loader.GetRecordCount(), index, data.get()));
    }
}
