/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_PLAYERBOT_CREATURE_INDEX_H
#define EVRY_PLAYERBOT_CREATURE_INDEX_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Which creature spawns each map has for each creature entry, and which entries count for another entry's kill
// credit. The finders ask "where are the loaded creatures of these entries" instead of walking every creature loaded
// on the map, which on a continent is tens of thousands for every quest objective of every bot that picks work.
//
// Built from the world database's spawn list once, and again when the number of spawns changes (a game master adding or
// deleting one). It has no server dependencies so it can be tested with made-up spawns.
class PlayerbotCreatureIndex
{
public:
    struct Spawn
    {
        uint64_t SpawnId = 0;
        uint32_t MapId = 0;
        uint32_t Entry = 0;
    };

    // One creature template's own entry and the entries its kills also count as.
    struct KillCredits
    {
        uint32_t Entry = 0;
        std::vector<uint32_t> CountsAs;
    };

    void BuildSpawns(std::vector<Spawn> const& spawns, std::size_t sourceSize)
    {
        _spawns.clear();
        for (Spawn const& spawn : spawns)
            _spawns[Key(spawn.MapId, spawn.Entry)].push_back(spawn.SpawnId);
        _spawnSourceSize = sourceSize;
        _spawnsBuilt = true;
    }

    void BuildKillCredits(std::vector<KillCredits> const& templates, std::size_t sourceSize)
    {
        _creditedBy.clear();
        for (KillCredits const& info : templates)
        {
            for (uint32_t credit : info.CountsAs)
            {
                if (!credit || credit == info.Entry)
                    continue;
                std::vector<uint32_t>& entries = _creditedBy[credit];
                if (std::find(entries.begin(), entries.end(), info.Entry) == entries.end())
                    entries.push_back(info.Entry);
            }
        }
        _creditSourceSize = sourceSize;
        _creditsBuilt = true;
    }

    bool SpawnsAreCurrent(std::size_t sourceSize) const { return _spawnsBuilt && _spawnSourceSize == sourceSize; }
    bool KillCreditsAreCurrent(std::size_t sourceSize) const { return _creditsBuilt && _creditSourceSize == sourceSize; }

    // The spawn ids of this entry on this map, in no particular order.
    std::vector<uint64_t> const& SpawnIds(uint32_t mapId, uint32_t entry) const
    {
        auto itr = _spawns.find(Key(mapId, entry));
        return itr != _spawns.end() ? itr->second : Empty64();
    }

    // The entry itself and every entry whose kill counts as it, each once.
    std::vector<uint32_t> EntriesGivingCredit(uint32_t creditEntry) const
    {
        std::vector<uint32_t> entries;
        if (!creditEntry)
            return entries;
        entries.push_back(creditEntry);
        auto itr = _creditedBy.find(creditEntry);
        if (itr != _creditedBy.end())
            entries.insert(entries.end(), itr->second.begin(), itr->second.end());
        return entries;
    }

private:
    static uint64_t Key(uint32_t mapId, uint32_t entry) { return (uint64_t(mapId) << 32) | entry; }
    static std::vector<uint64_t> const& Empty64()
    {
        static std::vector<uint64_t> const empty;
        return empty;
    }

    std::unordered_map<uint64_t, std::vector<uint64_t>> _spawns;
    std::unordered_map<uint32_t, std::vector<uint32_t>> _creditedBy;
    std::size_t _spawnSourceSize = 0;
    std::size_t _creditSourceSize = 0;
    bool _spawnsBuilt = false;
    bool _creditsBuilt = false;
};

#endif
