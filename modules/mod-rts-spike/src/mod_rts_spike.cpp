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

// Throwaway spike harness for the RTS commander-mode plan. Two questions this
// answers, both about the live client rather than the core: does the retail
// client still unlock the commentator free camera when the server sets the
// commentator player flags, and what destination does a ground-targeted cast
// deliver to the server. Delete this module once the answers are recorded.

#include "Chat.h"
#include "ChatCommand.h"
#include "Log.h"
#include "Player.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "WorldSession.h"
#include <atomic>

using namespace Trinity::ChatCommands;

// Which casts the spell logger reports. 0 with anyDest false means off.
// anyDest true reports every player cast that carries a ground destination,
// which is how you find a good reticle spell without cloning one first.
namespace
{
    std::atomic<uint32> ReticleSpellId = { 0 };
    std::atomic<bool> ReticleAnyDest = { false };
}

class rts_spike_commandscript : public CommandScript
{
public:
    rts_spike_commandscript() : CommandScript("rts_spike_commandscript") { }

    std::span<ChatCommandBuilder const> GetCommands() const override
    {
        static ChatCommandTable camFlagsTable =
        {
            { "on",  HandleCamFlagsOnCommand,  rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
            { "off", HandleCamFlagsOffCommand, rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
        };
        static ChatCommandTable reticleTable =
        {
            { "spell", HandleReticleSpellCommand, rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
            { "any",   HandleReticleAnyCommand,   rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
            { "off",   HandleReticleOffCommand,   rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
        };
        static ChatCommandTable spikeTable =
        {
            { "camflags", camFlagsTable },
            { "reticle",  reticleTable },
            { "status",   HandleStatusCommand, rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
        };
        static ChatCommandTable commandTable =
        {
            { "rtsspike", spikeTable },
        };
        return commandTable;
    }

    // Sets both commentator flags on your own character. The client side of
    // the spike is then the RTSSpike addon: /rtsspike cam dumps what
    // C_Commentator exposes and whether SetCameraPosition starts working.
    static bool HandleCamFlagsOnCommand(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!player)
            return false;

        player->SetPlayerFlag(PLAYER_FLAGS_COMMENTATOR2);
        player->SetPlayerFlag(PLAYER_FLAGS_COMMENTATOR_CAMERA);
        handler->PSendSysMessage("rts-spike: commentator flags set on %s. Now probe C_Commentator from the RTSSpike addon.", player->GetName());
        TC_LOG_INFO("server.worldserver", "mod-rts-spike: commentator flags set on {}", player->GetName());
        return true;
    }

    static bool HandleCamFlagsOffCommand(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!player)
            return false;

        player->RemovePlayerFlag(PLAYER_FLAGS_COMMENTATOR2);
        player->RemovePlayerFlag(PLAYER_FLAGS_COMMENTATOR_CAMERA);
        handler->PSendSysMessage("rts-spike: commentator flags removed from %s.", player->GetName());
        TC_LOG_INFO("server.worldserver", "mod-rts-spike: commentator flags removed from {}", player->GetName());
        return true;
    }

    static bool HandleReticleSpellCommand(ChatHandler* handler, uint32 spellId)
    {
        ReticleSpellId = spellId;
        ReticleAnyDest = false;
        handler->PSendSysMessage("rts-spike: logging ground destinations for spell %u.", spellId);
        return true;
    }

    static bool HandleReticleAnyCommand(ChatHandler* handler)
    {
        ReticleSpellId = 0;
        ReticleAnyDest = true;
        handler->PSendSysMessage("rts-spike: logging ground destinations for every player cast. Cast any ground-targeted spell.");
        return true;
    }

    static bool HandleReticleOffCommand(ChatHandler* handler)
    {
        ReticleSpellId = 0;
        ReticleAnyDest = false;
        handler->PSendSysMessage("rts-spike: reticle logging off.");
        return true;
    }

    static bool HandleStatusCommand(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!player)
            return false;

        bool const flags = player->HasPlayerFlag(PLAYER_FLAGS_COMMENTATOR2) && player->HasPlayerFlag(PLAYER_FLAGS_COMMENTATOR_CAMERA);
        uint32 const spellId = ReticleSpellId.load();
        bool const anyDest = ReticleAnyDest.load();
        handler->PSendSysMessage("rts-spike: commentator flags %s. Reticle logging: %s.",
            flags ? "ON" : "off",
            anyDest ? "every ground cast" : (spellId ? Trinity::StringFormat("spell {}", spellId) : "off"));
        return true;
    }
};

// Runs from Spell::_cast before CheckCast, so it also sees casts that then
// fail. That is fine here: the spike wants the clicked coordinates, not the
// cast outcome, and a future order spell would be read the same way.
class rts_spike_spell_logger : public AllSpellScript
{
public:
    rts_spike_spell_logger() : AllSpellScript("rts_spike_spell_logger") { }

    void OnSpellCast(Spell* spell, WorldObject* caster, SpellInfo const* spellInfo, bool /*skipCheck*/) override
    {
        uint32 const armedSpellId = ReticleSpellId.load(std::memory_order_relaxed);
        bool const anyDest = ReticleAnyDest.load(std::memory_order_relaxed);
        if (!armedSpellId && !anyDest)
            return;

        Player* player = caster ? caster->ToPlayer() : nullptr;
        if (!player)
            return;

        if (armedSpellId && spellInfo->Id != armedSpellId)
            return;

        if (!spell->m_targets.HasDst())
        {
            if (armedSpellId)
                ChatHandler(player->GetSession()).PSendSysMessage("rts-spike: spell %u cast, but it carries no ground destination.", spellInfo->Id);
            return;
        }

        // The map comes from the caster, not the destination. A destination the
        // client sent is filled by Position::Relocate, which copies x, y, z and
        // orientation and never touches the map, so the WorldLocation keeps its
        // default of MAPID_INVALID. Anything that acts on these coordinates has
        // to read the map off the player.
        // Log where she was standing as well as where she clicked. The clicked point
        // on its own cannot tell you whether the client aimed from the camera or from
        // the character, because a click near the body and a click under a distant
        // camera look the same once the caster's position is forgotten. The distance
        // is the number that separates them.
        WorldLocation const* dest = spell->m_targets.GetDstPos();
        float const distance = player->GetExactDist(dest);
        ChatHandler(player->GetSession()).PSendSysMessage("rts-spike: spell %u dest map %u at %.3f %.3f %.3f, caster at %.3f %.3f %.3f, %.1f yards away",
            spellInfo->Id, player->GetMapId(), dest->GetPositionX(), dest->GetPositionY(), dest->GetPositionZ(),
            player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), distance);
        TC_LOG_INFO("server.worldserver", "mod-rts-spike: {} cast spell {} dest map {} at {:.3f} {:.3f} {:.3f}, caster at {:.3f} {:.3f} {:.3f}, {:.1f} yards away",
            player->GetName(), spellInfo->Id, player->GetMapId(), dest->GetPositionX(), dest->GetPositionY(), dest->GetPositionZ(),
            player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), distance);
    }
};

void Addmod_rts_spikeScripts()
{
    new rts_spike_commandscript();
    new rts_spike_spell_logger();
}
