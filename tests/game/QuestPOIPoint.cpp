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
#include "DB2FileSystemSource.h"
#include "DB2LoadInfo.h"
#include "DB2Structure.h"
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <vector>

namespace
{
    class QuestPOIPointSource final : public DB2FileSource
    {
    public:
        QuestPOIPointSource()
        {
            // One uncompressed point, with signed coordinates and a parent ID above INT32_MAX.
            DB2Header header{};
            header.Signature = 0x35434457;
            header.Version = 5;
            header.RecordCount = 1;
            header.FieldCount = 4;
            header.RecordSize = 10;
            header.TableHash = 0x83467FEB;
            header.LayoutHash = 0x5CBBEFE7;
            header.MinId = 7;
            header.MaxId = 7;
            header.TotalFieldCount = 4;
            header.ParentLookupCount = 1;
            header.ColumnMetaSize = 4 * 24;
            header.SectionCount = 1;
            Append(&header, sizeof(header));

            DB2SectionHeader section{};
            section.FileOffset = sizeof(DB2Header) + sizeof(DB2SectionHeader) + 4 * 4 + header.ColumnMetaSize;
            section.RecordCount = 1;
            section.ParentLookupDataSize = 20;
            Append(&section, sizeof(section));

            for (uint16 offset : { uint16(0), uint16(4), uint16(6), uint16(8) })
            {
                AppendInteger(offset ? 16 : 0, 2);
                AppendInteger(offset, 2);
            }
            for (uint16 bitOffset : { uint16(0), uint16(32), uint16(48), uint16(64) })
            {
                AppendInteger(bitOffset, 2);
                AppendInteger(bitOffset ? 16 : 32, 2);
                AppendInteger(0, 4); // No additional column data.
                AppendInteger(0, 4); // No compression.
                AppendInteger(0, 8);
                AppendInteger(0, 4);
            }

            AppendInteger(7, 4);
            AppendInteger(uint16(-123), 2);
            AppendInteger(456, 2);
            AppendInteger(uint16(-789), 2);
            AppendInteger(1, 4); // One parent lookup entry.
            AppendInteger(0xFFFFFFFF, 4); // Minimum parent ID.
            AppendInteger(0xFFFFFFFF, 4); // Maximum parent ID.
            AppendInteger(0xFFFFFFFF, 4);
            AppendInteger(0, 4); // Record index within this section.
        }

        bool IsOpen() const override { return true; }
        bool Read(void* buffer, std::size_t numBytes) override
        {
            if (numBytes > _bytes.size() - _position)
                return false;
            if (numBytes)
                std::memcpy(buffer, _bytes.data() + _position, numBytes);
            _position += numBytes;
            return true;
        }
        int64 GetPosition() const override { return int64(_position); }
        bool SetPosition(int64 position) override
        {
            if (position < 0 || uint64(position) > _bytes.size())
                return false;
            _position = std::size_t(position);
            return true;
        }
        int64 GetFileSize() const override { return int64(_bytes.size()); }
        char const* GetFileName() const override { return "QuestPOIPoint regression fixture"; }
        DB2EncryptedSectionHandling HandleEncryptedSection(DB2SectionHeader const&) const override
        {
            return DB2EncryptedSectionHandling::Skip;
        }

    private:
        void Append(void const* value, std::size_t size)
        {
            auto bytes = static_cast<uint8 const*>(value);
            _bytes.insert(_bytes.end(), bytes, bytes + size);
        }
        void AppendInteger(uint64 value, std::size_t size)
        {
            for (std::size_t i = 0; i < size; ++i)
                _bytes.push_back(uint8(value >> (i * 8)));
        }

        std::vector<uint8> _bytes;
        std::size_t _position = 0;
    };
}

TEST_CASE("QuestPOIPoint loads unsigned parent IDs and signed coordinates", "[DB2][QuestPOIPoint]")
{
    QuestPOIPointSource source;
    DB2FileLoader loader;
    REQUIRE_NOTHROW(loader.Load(&source, &QuestPOIPointLoadInfo::Instance));

    uint32 indexSize = 0;
    char** index = nullptr;
    std::unique_ptr<char[]> data(loader.AutoProduceData(indexSize, index));
    std::unique_ptr<char*[]> indexOwner(index);
    REQUIRE(indexSize == 8);
    REQUIRE(index[7] != nullptr);
    auto point = reinterpret_cast<QuestPOIPointEntry const*>(index[7]);
    CHECK(point->ID == 7);
    CHECK(point->X == -123);
    CHECK(point->Y == 456);
    CHECK(point->Z == -789);
    CHECK(uint64(point->QuestPOIBlobID) == 0xFFFFFFFFULL);
}

TEST_CASE("QuestPOIPoint rejects the former signed parent declaration", "[DB2][QuestPOIPoint]")
{
    QuestPOIPointSource source;
    DB2FileLoader loader;
    std::vector<DB2FieldMeta> fields(std::begin(QuestPOIPointLoadInfo::Fields), std::end(QuestPOIPointLoadInfo::Fields));
    fields.back().IsSigned = true;
    DB2FileLoadInfo formerLoadInfo(fields.data(), fields.size(), &QuestPOIPointMeta::Instance);
    REQUIRE_THROWS_WITH(loader.Load(&source, &formerLoadInfo),
        Catch::Matchers::ContainsSubstring("ParentIndexField must always be unsigned"));
}

TEST_CASE("Installed QuestPOIPoint data loads through the production loader", "[.][DB2][QuestPOIPoint][installed-data]")
{
    char const* path = std::getenv("TC_QUEST_POI_POINT_DB2");
    if (!path || !*path)
        SKIP("Set TC_QUEST_POI_POINT_DB2 to an installed QuestPOIPoint.db2 to run this read-only check.");

    DB2FileSystemSource source(path);
    DB2FileLoader loader;
    REQUIRE(source.IsOpen());
    REQUIRE_NOTHROW(loader.Load(&source, &QuestPOIPointLoadInfo::Instance));
    REQUIRE(loader.GetRecordCount() > 0);

    uint32 indexSize = 0;
    char** index = nullptr;
    std::unique_ptr<char[]> data(loader.AutoProduceData(indexSize, index));
    std::unique_ptr<char*[]> indexOwner(index);
    loader.AutoProduceRecordCopies(loader.GetRecordCount(), index, data.get());
    uint32 loaded = 0;
    uint32 withParent = 0;
    for (uint32 id = 0; id < indexSize; ++id)
    {
        if (!index[id])
            continue;
        ++loaded;
        auto point = reinterpret_cast<QuestPOIPointEntry const*>(index[id]);
        if (point->QuestPOIBlobID)
            ++withParent;
    }
    REQUIRE(loaded > 0);
    REQUIRE(withParent > 0);
    std::cout << "Loaded " << loaded << " QuestPOIPoint records; " << withParent << " have parent IDs.\n";
}
