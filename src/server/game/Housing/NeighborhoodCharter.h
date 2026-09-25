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

#ifndef TRINITYCORE_NEIGHBORHOOD_CHARTER_H
#define TRINITYCORE_NEIGHBORHOOD_CHARTER_H

#include "Define.h"
#include "DatabaseEnvFwd.h"
#include "HousingDefines.h"
#include "ObjectGuid.h"
#include <string>
#include <vector>

class TC_GAME_API NeighborhoodCharter
{
public:
    explicit NeighborhoodCharter(uint64 id, ObjectGuid creatorGuid, uint32 creatorBnetAccountId = 0);

    uint64 GetId() const { return _id; }
    ObjectGuid GetCreatorGuid() const { return _creatorGuid; }
    uint32 GetCreatorBnetAccountId() const { return _creatorBnetAccountId; }
    std::string const& GetName() const { return _name; }
    uint32 GetNeighborhoodMapID() const { return _neighborhoodMapID; }
    uint32 GetFactionFlags() const { return _factionFlags; }
    bool IsGuildCharter() const { return _isGuild; }

    void SetName(std::string const& name) { _name = name; }
    void SetNeighborhoodMapID(uint32 mapId) { _neighborhoodMapID = mapId; }
    void SetFactionFlags(uint32 flags) { _factionFlags = flags; }
    void SetIsGuild(bool isGuild) { _isGuild = isGuild; }

    // Whether a Battle.net account may sign a charter: not the creator's account, not an account that already signed
    // it, and not an account that has signed another open charter ("Player has already signed another Neighborhood
    // Charter", GlobalStrings ERR_HOUSING_RESULT_DUPLICATE_CHARTER_SIGNATURE).
    static HousingResult CheckSignature(uint32 signerBnetAccountId, uint32 creatorBnetAccountId,
        std::vector<uint32> const& signerBnetAccountIds, bool signedAnotherCharter);

    // Signs the charter for a character of signerBnetAccountId and saves the signature, after CheckSignature.
    HousingResult AddSignature(ObjectGuid signerGuid, uint32 signerBnetAccountId, bool signedAnotherCharter);
    bool HasSigned(ObjectGuid signerGuid) const;
    uint32 GetSignatureCount() const { return static_cast<uint32>(_signatures.size()); }
    // One signature per Battle.net account, so the signature count is the count of accounts.
    bool HasEnoughSignatures() const { return GetSignatureCount() >= MIN_CHARTER_SIGNATURES; }
    std::vector<ObjectGuid> const& GetSignatures() const { return _signatures; }
    std::vector<uint32> const& GetSignerBnetAccountIds() const { return _signerBnetAccountIds; }
    // A charter keeps its signatures only while its name, district and faction stay as they were signed.
    bool HasSameSettings(std::string const& name, uint32 neighborhoodMapID, uint32 factionFlags) const;
    void CopySignaturesFrom(NeighborhoodCharter const& other);

    void SaveToDB(CharacterDatabaseTransaction trans);
    bool LoadFromDB(PreparedQueryResult charter, PreparedQueryResult signatures);
    static void DeleteFromDB(uint64 id, CharacterDatabaseTransaction trans);

private:
    uint64 _id = 0;
    ObjectGuid _creatorGuid;
    uint32 _creatorBnetAccountId = 0;
    std::string _name;
    uint32 _neighborhoodMapID = 0;
    uint32 _factionFlags = 0;
    bool _isGuild = false;
    uint32 _createTime = 0;
    // The signing characters, and each one's Battle.net account at the same index.
    std::vector<ObjectGuid> _signatures;
    std::vector<uint32> _signerBnetAccountIds;
};

#endif // TRINITYCORE_NEIGHBORHOOD_CHARTER_H
