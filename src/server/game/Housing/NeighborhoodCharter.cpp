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

#include "NeighborhoodCharter.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include <algorithm>

NeighborhoodCharter::NeighborhoodCharter(uint64 id, ObjectGuid creatorGuid, uint32 creatorBnetAccountId /*= 0*/)
    : _id(id), _creatorGuid(creatorGuid), _creatorBnetAccountId(creatorBnetAccountId), _createTime(static_cast<uint32>(GameTime::GetGameTime()))
{
}

bool NeighborhoodCharter::LoadFromDB(PreparedQueryResult charter, PreparedQueryResult signatures)
{
    if (!charter)
        return false;

    Field* fields = charter->Fetch();

    //          0    1            2       3                4             5        6           7
    // SELECT id, creatorGuid, name, neighborhoodMapID, factionFlags, isGuild, createTime, creatorBnetAccountId
    //        FROM neighborhood_charters WHERE id = ?

    _id                 = fields[0].GetUInt64();
    _creatorGuid        = ObjectGuid::Create<HighGuid::Player>(fields[1].GetUInt64());
    _name               = fields[2].GetString();
    _neighborhoodMapID  = fields[3].GetUInt32();
    _factionFlags       = fields[4].GetUInt32();
    _isGuild            = fields[5].GetBool();
    _createTime         = fields[6].GetUInt32();
    _creatorBnetAccountId = fields[7].GetUInt32();

    TC_LOG_DEBUG("housing", "NeighborhoodCharter::LoadFromDB: Loaded charter {} '{}' by {}",
        _id, _name, _creatorGuid.ToString());

    // Load signatures
    _signatures.clear();
    _signerBnetAccountIds.clear();

    if (signatures)
    {
        do
        {
            Field* sigFields = signatures->Fetch();

            //          0           1
            // SELECT signerGuid, signerBnetAccountId FROM neighborhood_charter_signatures WHERE charterId = ?

            ObjectGuid signerGuid = ObjectGuid::Create<HighGuid::Player>(sigFields[0].GetUInt64());
            _signatures.push_back(signerGuid);
            _signerBnetAccountIds.push_back(sigFields[1].GetUInt32());
        } while (signatures->NextRow());
    }

    TC_LOG_DEBUG("housing", "NeighborhoodCharter::LoadFromDB: Loaded {} signatures for charter {}",
        _signatures.size(), _id);

    return true;
}

void NeighborhoodCharter::SaveToDB(CharacterDatabaseTransaction trans)
{
    // Replace the charter row
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_NEIGHBORHOOD_CHARTER);
    uint8 index = 0;
    stmt->setUInt64(index++, _id);
    stmt->setUInt64(index++, _creatorGuid.GetCounter());
    stmt->setString(index++, _name);
    stmt->setUInt32(index++, _neighborhoodMapID);
    stmt->setUInt32(index++, _factionFlags);
    stmt->setBool(index++, _isGuild);
    stmt->setUInt32(index++, _createTime);
    stmt->setUInt32(index++, _creatorBnetAccountId);
    trans->Append(stmt);

    // Delete all existing signatures and re-insert
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD_CHARTER_SIGNATURES);
    stmt->setUInt64(0, _id);
    trans->Append(stmt);

    for (std::size_t i = 0; i < _signatures.size(); ++i)
    {
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_NEIGHBORHOOD_CHARTER_SIGNATURE);
        index = 0;
        stmt->setUInt64(index++, _id);
        stmt->setUInt64(index++, _signatures[i].GetCounter());
        stmt->setUInt32(index++, _signerBnetAccountIds[i]);
        stmt->setUInt32(index++, _createTime);
        trans->Append(stmt);
    }

    TC_LOG_DEBUG("housing", "NeighborhoodCharter::SaveToDB: Saved charter {} '{}' with {} signatures",
        _id, _name, _signatures.size());
}

/*static*/ void NeighborhoodCharter::DeleteFromDB(uint64 id, CharacterDatabaseTransaction trans)
{
    // Delete signatures first (foreign key dependency)
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD_CHARTER_SIGNATURES);
    stmt->setUInt64(0, id);
    trans->Append(stmt);

    // Delete the charter
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD_CHARTER);
    stmt->setUInt64(0, id);
    trans->Append(stmt);

    TC_LOG_DEBUG("housing", "NeighborhoodCharter::DeleteFromDB: Deleted charter {}", id);
}

/*static*/ HousingResult NeighborhoodCharter::CheckSignature(uint32 signerBnetAccountId, uint32 creatorBnetAccountId,
    std::vector<uint32> const& signerBnetAccountIds, bool signedAnotherCharter)
{
    if (!signerBnetAccountId || signerBnetAccountId == creatorBnetAccountId)
        return HOUSING_RESULT_PERMISSION_DENIED;

    if (std::find(signerBnetAccountIds.begin(), signerBnetAccountIds.end(), signerBnetAccountId) != signerBnetAccountIds.end())
        return HOUSING_RESULT_DUPLICATE_CHARTER_SIGNATURE;

    if (signedAnotherCharter)
        return HOUSING_RESULT_DUPLICATE_CHARTER_SIGNATURE;

    return HOUSING_RESULT_SUCCESS;
}

HousingResult NeighborhoodCharter::AddSignature(ObjectGuid signerGuid, uint32 signerBnetAccountId, bool signedAnotherCharter)
{
    HousingResult result = CheckSignature(signerBnetAccountId, _creatorBnetAccountId, _signerBnetAccountIds, signedAnotherCharter);
    if (result != HOUSING_RESULT_SUCCESS)
    {
        TC_LOG_DEBUG("housing", "NeighborhoodCharter::AddSignature: {} (Battle.net account {}) may not sign charter {} (result {})",
            signerGuid.ToString(), signerBnetAccountId, _id, uint32(result));
        return result;
    }

    _signatures.push_back(signerGuid);
    _signerBnetAccountIds.push_back(signerBnetAccountId);

    // Saved before the handler returns: the next signature, perhaps from another game account of the same Battle.net
    // account, reads the signatures back from the database to enforce one charter per account.
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_NEIGHBORHOOD_CHARTER_SIGNATURE);
    uint8 index = 0;
    stmt->setUInt64(index++, _id);
    stmt->setUInt64(index++, signerGuid.GetCounter());
    stmt->setUInt32(index++, signerBnetAccountId);
    stmt->setUInt32(index++, static_cast<uint32>(GameTime::GetGameTime()));
    trans->Append(stmt);
    CharacterDatabase.DirectCommitTransaction(trans);

    TC_LOG_DEBUG("housing", "NeighborhoodCharter::AddSignature: Player {} signed charter {} ({}/{} signatures)",
        signerGuid.ToString(), _id, _signatures.size(), MIN_CHARTER_SIGNATURES);

    return HOUSING_RESULT_SUCCESS;
}

bool NeighborhoodCharter::HasSameSettings(std::string const& name, uint32 neighborhoodMapID, uint32 factionFlags) const
{
    return _name == name && _neighborhoodMapID == neighborhoodMapID && _factionFlags == factionFlags;
}

void NeighborhoodCharter::CopySignaturesFrom(NeighborhoodCharter const& other)
{
    _signatures = other._signatures;
    _signerBnetAccountIds = other._signerBnetAccountIds;
}

bool NeighborhoodCharter::HasSigned(ObjectGuid signerGuid) const
{
    return std::find(_signatures.begin(), _signatures.end(), signerGuid) != _signatures.end();
}
