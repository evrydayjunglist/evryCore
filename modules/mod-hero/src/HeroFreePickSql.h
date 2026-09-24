#ifndef EVRY_HERO_FREE_PICK_SQL_H
#define EVRY_HERO_FREE_PICK_SQL_H

#include "HeroFreePick.h"

namespace HeroFreePick
{
inline std::string SqlKey(std::uint32_t realm, std::uint64_t guid)
{
    return "`realm`=" + std::to_string(realm) + " AND `guid`=" + std::to_string(guid) +
        " AND `profile`='" + std::string(Profile) + "'";
}

inline std::string SqlIdentity(std::uint32_t realm, std::uint64_t guid)
{
    return std::to_string(realm) + "," + std::to_string(guid) + ",'" + std::string(Profile) + "'";
}

inline std::vector<std::string> InitializeSql(std::uint32_t realm, std::uint64_t guid)
{
    std::string r = std::to_string(realm), g = std::to_string(guid);
    std::vector<std::string> sql;
    sql.push_back("INSERT IGNORE INTO `character_hero_freepick` (`realm`,`guid`,`profile`,`mode`,`rules_revision`,`version`,`previous_version`,`balance`) "
        "SELECT " + SqlIdentity(realm, guid) + ",601,1,0,0,8 FROM `characters` WHERE `guid`=" + g +
        " AND `class`=16 AND `deleteDate` IS NULL AND NOT EXISTS (SELECT 1 FROM `character_hero_freepick_deleted` WHERE `realm`=" + r + " AND `guid`=" + g + ")");
    sql.push_back("UPDATE `character_hero_freepick` SET `source_epoch`=`source_epoch`+1 WHERE " + SqlKey(realm, guid));
    for (Entry const& entry : Entries)
        sql.push_back("INSERT IGNORE INTO `character_hero_spell_source` (`realm`,`guid`,`spell`,`independent`) SELECT " + r +
            ",`guid`,`spell`,1 FROM `character_spell` WHERE `guid`=" + g + " AND `spell`=" + std::to_string(entry.Spell) +
            " AND EXISTS (SELECT 1 FROM `character_hero_freepick` WHERE " + SqlKey(realm, guid) + ")");
    return sql;
}

inline std::vector<std::string> InitializeSessionSql(std::uint32_t realm, std::uint64_t guid,
    std::vector<std::string> const& sessionTokens)
{
    auto sql = InitializeSql(realm, guid);
    // Session tokens allocate exactly once, including a save before login's
    // asynchronous read completes. Pending predecessors are allocated first.
    sql.erase(sql.begin() + 1);
    std::vector<std::string> sessions;
    for (std::string const& token : sessionTokens)
    {
        if (!HexId(token))
            return {};
        std::string key = SqlKey(realm, guid);
        sessions.push_back("UPDATE `character_hero_freepick` SET `source_epoch`=`source_epoch`+"
            "IF(EXISTS(SELECT 1 FROM `character_hero_source_session` WHERE " + key + " AND `token`='" + token +
            "'),0,1) WHERE " + key);
        sessions.push_back("INSERT IGNORE INTO `character_hero_source_session` (`realm`,`guid`,`profile`,`token`,`epoch`) SELECT " +
            SqlIdentity(realm, guid) + ",'" + token + "',`source_epoch` FROM `character_hero_freepick` WHERE " + key);
    }
    sql.insert(sql.begin() + 1, sessions.begin(), sessions.end());
    return sql;
}

inline std::vector<std::string> SourceSql(std::uint32_t realm, std::uint64_t guid, std::uint32_t touched,
    std::uint32_t independent, std::array<std::uint64_t, 10> const& revisions, std::uint64_t epoch = 1,
    std::string_view sessionToken = {})
{
    std::vector<std::string> sql;
    if (!sessionToken.empty() && !HexId(sessionToken))
        return sql;
    std::string epochSql = sessionToken.empty() ? std::to_string(epoch) :
        "(SELECT `epoch` FROM `character_hero_source_session` WHERE " + SqlKey(realm, guid) +
        " AND `token`='" + std::string(sessionToken) + "')";
    for (std::size_t i = 0; i < Entries.size(); ++i)
        if (touched & (1u << i))
            sql.push_back("INSERT INTO `character_hero_spell_source` (`realm`,`guid`,`spell`,`independent`,`revision`,`epoch`) SELECT " +
                std::to_string(realm) + "," + std::to_string(guid) + "," + std::to_string(Entries[i].Spell) + "," +
                ((independent & (1u << i)) ? "1" : "0") + "," + std::to_string(revisions[i]) + "," + epochSql +
                " FROM `character_hero_freepick` WHERE " + SqlKey(realm, guid) +
                (sessionToken.empty() ? "" : " AND EXISTS (SELECT 1 FROM `character_hero_source_session` WHERE " +
                    SqlKey(realm, guid) + " AND `token`='" + std::string(sessionToken) + "')") +
                " ON DUPLICATE KEY UPDATE `independent`=IF(VALUES(`epoch`)>`epoch` OR "
                "(VALUES(`epoch`)=`epoch` AND VALUES(`revision`)>=`revision`),VALUES(`independent`),`independent`),"
                "`revision`=IF(VALUES(`epoch`)>`epoch`,VALUES(`revision`),"
                "IF(VALUES(`epoch`)=`epoch`,GREATEST(`revision`,VALUES(`revision`)),`revision`)),"
                "`epoch`=GREATEST(`epoch`,VALUES(`epoch`))");
    return sql;
}

inline std::vector<std::string> ApplySql(std::uint32_t realm, std::uint64_t guid, Request const& request,
    Change const& change, std::uint32_t /*externalSources*/)
{
    if (!HexId(request.Id) || !request.Commit || (request.Desired & ~AllEntries))
        return {};
    std::vector<std::string> sql;
    std::string key = SqlKey(realm, guid);
    if (change.Code == Result::Ok)
    {
        sql.push_back("UPDATE `character_hero_freepick` SET `version`=`version`+1,`previous_version`=" +
            std::to_string(request.Version) + ",`balance`=" + std::to_string(change.State.Essence) + " WHERE " + key);
        for (std::size_t i = 0; i < Entries.size(); ++i)
        {
            if (change.Removed & (1u << i))
            {
                sql.push_back("DELETE FROM `character_hero_freepick_owned` WHERE " + key + " AND `catalog`='" +
                    std::string(Catalog) + "' AND `advancement`=" + std::to_string(Entries[i].Advancement));
            }
            if (change.Added & (1u << i))
                sql.push_back("INSERT INTO `character_hero_freepick_owned` (`realm`,`guid`,`profile`,`catalog`,`advancement`,`spell`,`paid`,`rules_revision`) VALUES (" +
                    SqlIdentity(realm, guid) + ",'" + std::string(Catalog) + "'," + std::to_string(Entries[i].Advancement) + "," +
                    std::to_string(Entries[i].Spell) + "," + std::to_string(change.State.Paid[i]) + "," + std::to_string(RulesRevision) + ")");
        }
    }
    sql.push_back("INSERT INTO `character_hero_freepick_request` (`realm`,`guid`,`profile`,`request_id`,`expected_version`,`desired_mask`,`result`,`committed_version`) VALUES (" +
        SqlIdentity(realm, guid) + ",'" + request.Id + "'," + std::to_string(request.Version) + "," +
        std::to_string(request.Desired) + "," + std::to_string(std::uint32_t(change.Code)) + "," +
        (change.Code == Result::Ok ? std::to_string(change.State.Version) : "NULL") + ")");
    return sql;
}

inline std::vector<std::string> DeleteSql(std::uint32_t realm, std::uint64_t guid)
{
    std::string identity = "`realm`=" + std::to_string(realm) + " AND `guid`=" + std::to_string(guid);
    return {
        "INSERT IGNORE INTO `character_hero_freepick_deleted` (`realm`,`guid`) VALUES (" + std::to_string(realm) + "," + std::to_string(guid) + ")",
        "DELETE FROM `character_hero_freepick` WHERE " + SqlKey(realm, guid),
        "DELETE FROM `character_hero_spell_source` WHERE " + identity
    };
}

inline std::vector<std::string> NewCharacterSql(std::uint32_t realm, std::uint64_t guid)
{
    return {
        "DELETE FROM `character_hero_freepick` WHERE " + SqlKey(realm, guid),
        "DELETE FROM `character_hero_spell_source` WHERE `realm`=" + std::to_string(realm) + " AND `guid`=" + std::to_string(guid),
        "DELETE FROM `character_hero_freepick_deleted` WHERE `realm`=" + std::to_string(realm) + " AND `guid`=" + std::to_string(guid)
    };
}
}
#endif
