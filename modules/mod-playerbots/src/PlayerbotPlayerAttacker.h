/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_PLAYERBOT_PLAYER_ATTACKER_H
#define EVRY_PLAYERBOT_PLAYER_ATTACKER_H

#include "Define.h"

// A bot never starts a fight with a player. She fights back against a player who attacks her, only while that player
// is still attacking her, and she does not walk after them into the other faction's land or farther than this from
// where she stood when they attacked her.
inline constexpr float PLAYERBOT_PLAYER_CHASE_YARDS = 30.0f;

struct PlayerbotPlayerAttackerFacts
{
    bool OnHerMap = false;
    bool Alive = false;
    bool SheSeesThem = false;       // CanSeeOrDetect: a player who vanished or went into stealth cannot be clicked on
    bool ValidAttackTarget = false; // the server's own rule for whether she may attack them
    bool SwingingAtHer = false;     // she is the victim of their auto-attack
    bool InCombatWithHer = false;   // their spell or swing put the two of them in combat
    bool TargetingHer = false;      // she is the target they have selected
};

enum class PlayerbotPlayerAttackerVerdict
{
    Answer,
    Gone,            // logged out, or on another map
    Dead,
    Unseen,
    NotAValidTarget, // the server would refuse her attack (a sanctuary, no PvP flag, same side, and the like)
    NotAttackingHer
};

// A player is attacking her while they swing at her, or while they are in combat with her and have her selected, as a
// caster or hunter who never swings does.
inline PlayerbotPlayerAttackerVerdict JudgePlayerbotPlayerAttacker(PlayerbotPlayerAttackerFacts const& facts)
{
    if (!facts.OnHerMap)
        return PlayerbotPlayerAttackerVerdict::Gone;
    if (!facts.Alive)
        return PlayerbotPlayerAttackerVerdict::Dead;
    if (!facts.SheSeesThem)
        return PlayerbotPlayerAttackerVerdict::Unseen;
    if (!facts.ValidAttackTarget)
        return PlayerbotPlayerAttackerVerdict::NotAValidTarget;
    if (!facts.SwingingAtHer && !(facts.InCombatWithHer && facts.TargetingHer))
        return PlayerbotPlayerAttackerVerdict::NotAttackingHer;
    return PlayerbotPlayerAttackerVerdict::Answer;
}

inline char const* PlayerbotPlayerAttackerVerdictText(PlayerbotPlayerAttackerVerdict verdict)
{
    switch (verdict)
    {
        case PlayerbotPlayerAttackerVerdict::Answer: return "is attacking her";
        case PlayerbotPlayerAttackerVerdict::Gone: return "is no longer on her map";
        case PlayerbotPlayerAttackerVerdict::Dead: return "is dead";
        case PlayerbotPlayerAttackerVerdict::Unseen: return "is out of her sight";
        case PlayerbotPlayerAttackerVerdict::NotAValidTarget: return "is not someone she may attack any more";
        case PlayerbotPlayerAttackerVerdict::NotAttackingHer: return "is no longer attacking her";
    }
    return "is no longer attacking her";
}

// Land that belongs to the other faction, by the area's faction group and her faction template's friend and enemy
// groups, the way the server decides whether a place is hostile to her. A sanctuary is nobody's enemy land.
inline bool PlayerbotAreaIsEnemyLand(uint32 areaFactionGroupMask, uint32 herFriendGroup, uint32 herEnemyGroup, bool sanctuary)
{
    if (sanctuary || !areaFactionGroupMask)
        return false;
    if (herFriendGroup & areaFactionGroupMask)
        return false;
    return (herEnemyGroup & areaFactionGroupMask) != 0;
}

enum class PlayerbotPlayerChaseVerdict
{
    Walk,
    EnemyLand,
    TooFar
};

// Whether she may walk to a spot to reach a player who attacked her. If not, she answers them from where she stands.
inline PlayerbotPlayerChaseVerdict JudgePlayerbotPlayerChase(bool endsInEnemyLand, float endYardsFromFightStart)
{
    if (endsInEnemyLand)
        return PlayerbotPlayerChaseVerdict::EnemyLand;
    if (endYardsFromFightStart > PLAYERBOT_PLAYER_CHASE_YARDS)
        return PlayerbotPlayerChaseVerdict::TooFar;
    return PlayerbotPlayerChaseVerdict::Walk;
}

#endif
