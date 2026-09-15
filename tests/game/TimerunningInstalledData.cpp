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
#include "GridDefines.h"
#include "GridMap.h"
#include "StringFormat.h"
#include "TimerunningPandaria.h"
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>

TEST_CASE("Pandaria introduction placements match installed terrain", "[.TimerunningInstalledData]")
{
    char const* dataDir = std::getenv("TIMERUNNING_DATA_DIR");
    if (!dataDir)
        SKIP("Set TIMERUNNING_DATA_DIR to the extracted server data directory for this optional installed-data check.");

    auto checkPosition = [dataDir](float x, float y, float z)
    {
        GridMap grid;
        std::filesystem::path file = std::filesystem::path(dataDir) / "maps" /
            Trinity::StringFormat("0870_{:02}_{:02}.map", int(CENTER_GRID_ID - x / SIZE_OF_GRIDS), int(CENTER_GRID_ID - y / SIZE_OF_GRIDS));
        CAPTURE(x, y, z, file.string());
        REQUIRE(grid.loadData(file.string().c_str()) == GridMap::LoadResult::Ok);
        REQUIRE(std::fabs(grid.getHeight(x, y) - z) < 0.1f);
        REQUIRE(grid.getLiquidLevel(x, y) < z);
    };
    Position const& start = Timerunning::Pandaria::StartLocation;
    checkPosition(start.GetPositionX(), start.GetPositionY(), start.GetPositionZ());

    std::filesystem::path repo = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    std::ifstream input(repo / "sql/updates/world/master/2026_09_13_02_world.sql");
    REQUIRE(input.is_open());
    std::string sql(std::istreambuf_iterator<char>{ input }, {});
    std::regex spawn(R"(\(118010[0-9]{2},[0-9]+,870,[0-9]+,[0-9]+,'0',([-0-9.]+),([-0-9.]+),([-0-9.]+),)");
    uint32 count = 0;
    for (std::sregex_iterator itr(sql.begin(), sql.end(), spawn), end; itr != end; ++itr)
    {
        checkPosition(std::stof((*itr)[1]), std::stof((*itr)[2]), std::stof((*itr)[3]));
        ++count;
    }
    REQUIRE(count == 16);
}
