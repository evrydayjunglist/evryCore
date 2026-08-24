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

// Throwaway spike harness for the RTS commander-mode plan. It retains the
// commentator-camera and order-reticle probes and now calls the public
// commandable-player movement API for Phase 1A live checks. Delete this module
// once the remaining evidence is recorded.

#include "Chat.h"
#include "ChatCommand.h"
#include "Group.h"
#include "mod-playerbots/src/CommandablePlayerService.h"
#include "mod-playerbots/src/PlayerbotMgr.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "WorldSession.h"
#include "WorldPacket.h"
#include <array>
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
        static ChatCommandTable commandableTable =
        {
            { "enter",        HandleCommandEnter,        rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
            { "state",        HandleCommandState,        rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
            { "move",         HandleCommandMove,         rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
            { "movefive",     HandleCommandMoveFive,     rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
            { "moveoriginal", HandleCommandMoveOriginal, rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
            { "stop",         HandleCommandStop,         rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
            { "release",      HandleCommandRelease,      rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
            { "exit",         HandleCommandExit,         rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
        };
        static ChatCommandTable spikeTable =
        {
            { "camflags",     camFlagsTable },
            { "command",      commandableTable },
            { "reticle",      reticleTable },
            { "acceptinvite", HandleAcceptInviteCommand, rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
            { "status",       HandleStatusCommand, rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
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

    static bool HandleAcceptInviteCommand(ChatHandler* handler, std::string subjectName)
    {
        Player* controller = handler->GetPlayer();
        if (!controller || !normalizePlayerName(subjectName))
            return false;

        Player* subject = ObjectAccessor::FindPlayerByName(subjectName);
        if (!subject || !subject->GetSession() || subject->GetSession()->PlayerLogout())
        {
            handler->PSendSysMessage("rts-spike: invite acceptance refused: %s is not online.", subjectName.c_str());
            return true;
        }
        if (!sPlayerbotMgr->IsBotAccount(subject->GetSession()->GetAccountId()))
        {
            handler->PSendSysMessage("rts-spike: invite acceptance refused: %s is not a managed playerbot.", subject->GetName());
            return true;
        }
        if (!controller->IsInWorld() || !subject->IsInWorld() || controller->GetMap() != subject->GetMap())
        {
            handler->SendSysMessage("rts-spike: invite acceptance refused: controller and bot must be on the same map instance.");
            return true;
        }
        if (subject->GetGroup())
        {
            handler->PSendSysMessage("rts-spike: invite acceptance refused: %s is already grouped.", subject->GetName());
            return true;
        }

        Group* invite = subject->GetGroupInvite();
        if (!invite)
        {
            handler->PSendSysMessage("rts-spike: invite acceptance refused: invite %s normally first.", subject->GetName());
            return true;
        }
        if (invite->GetLeaderGUID() != controller->GetGUID())
        {
            handler->PSendSysMessage("rts-spike: invite acceptance refused: %s has an invite from another leader.", subject->GetName());
            return true;
        }

        // This is the stock client response with no PartyIndex, Accept set,
        // and no requested role. The bot acts only by queueing that packet.
        WorldPacket response(CMSG_PARTY_INVITE_RESPONSE);
        response.WriteBit(false);
        response.WriteBit(true);
        response.WriteBit(false);
        response.FlushBits();
        subject->GetSession()->QueuePacket(std::move(response));

        handler->PSendSysMessage("rts-spike: %s queued the normal party-invite acceptance packet.", subject->GetName());
        TC_LOG_INFO("server.worldserver", "mod-rts-spike: {} queued CMSG_PARTY_INVITE_RESPONSE for invite from {}", subject->GetName(), controller->GetName());
        return true;
    }

    static Optional<CommandablePlayerSnapshot> FindSubject(Player* commander, ObjectGuid subject)
    {
        for (CommandablePlayerSnapshot const& snapshot : sCommandablePlayerService->GetRtsSubjects(commander->GetGUID()))
            if (snapshot.Subject == subject)
                return snapshot;
        return {};
    }

    static bool SubmitMove(ChatHandler* handler, Player* commander, CommandablePlayerSnapshot const& snapshot,
        float x, float y, float z)
    {
        CommandablePlayerRequest request;
        request.Controller = { CommandableControllerKind::Rts, commander->GetGUID() };
        request.Subject = snapshot.Subject;
        request.Generation = snapshot.Generation;
        request.Operation = CommandablePlayerOperation::Move;
        request.Destination.Relocate(x, y, z);
        CommandablePlayerResult result = sCommandablePlayerService->Submit(request);
        handler->PSendSysMessage("rts-spike: move subject %s generation " UI64FMTD ": %s.",
            snapshot.Subject.ToString(), snapshot.Generation, CommandablePlayerResultCodeName(result.Code));
        return bool(result);
    }

    static bool SubmitSimple(ChatHandler* handler, Player* commander, PlayerIdentifier const& subject,
        CommandablePlayerOperation operation)
    {
        Optional<CommandablePlayerSnapshot> snapshot = FindSubject(commander, subject.GetGUID());
        if (!snapshot)
        {
            handler->PSendSysMessage("rts-spike: %s is not claimed by this RTS session.", subject.GetName());
            return false;
        }

        CommandablePlayerRequest request;
        request.Controller = { CommandableControllerKind::Rts, commander->GetGUID() };
        request.Subject = snapshot->Subject;
        request.Generation = snapshot->Generation;
        request.Operation = operation;
        CommandablePlayerResult result = sCommandablePlayerService->Submit(request);
        handler->PSendSysMessage("rts-spike: %s subject %s generation " UI64FMTD ": %s.",
            operation == CommandablePlayerOperation::Hold ? "stop" : "release", subject.GetName(),
            snapshot->Generation, CommandablePlayerResultCodeName(result.Code));
        return bool(result);
    }

    static bool HandleCommandEnter(ChatHandler* handler)
    {
        Player* commander = handler->GetPlayer();
        if (!commander)
            return false;

        CommandableRtsEnterResult result = sCommandablePlayerService->EnterRts(commander);
        handler->PSendSysMessage("rts-spike: enter RTS: %s; %u subject(s) claimed in immediate hold.",
            CommandablePlayerResultCodeName(result.Code), uint32(result.Subjects.size()));
        for (CommandablePlayerSnapshot const& subject : result.Subjects)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(subject.Subject);
            handler->PSendSysMessage("  %s%s generation " UI64FMTD " %s",
                player ? player->GetName() : subject.Subject.ToString(), subject.OriginalCharacter ? " (original)" : "",
                subject.Generation, CommandablePlayerDirectiveName(subject.Directive));
        }
        return bool(result);
    }

    static bool HandleCommandState(ChatHandler* handler)
    {
        Player* commander = handler->GetPlayer();
        if (!commander)
            return false;

        std::vector<CommandablePlayerSnapshot> subjects = sCommandablePlayerService->GetRtsSubjects(commander->GetGUID());
        handler->PSendSysMessage("rts-spike: %u claimed subject(s).", uint32(subjects.size()));
        for (CommandablePlayerSnapshot const& subject : subjects)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(subject.Subject);
            handler->PSendSysMessage("  %s%s generation " UI64FMTD " %s",
                player ? player->GetName() : subject.Subject.ToString(), subject.OriginalCharacter ? " (original)" : "",
                subject.Generation, CommandablePlayerDirectiveName(subject.Directive));
        }
        return true;
    }

    static bool HandleCommandMove(ChatHandler* handler, PlayerIdentifier subject, float x, float y, float z)
    {
        Player* commander = handler->GetPlayer();
        if (!commander)
            return false;
        Optional<CommandablePlayerSnapshot> snapshot = FindSubject(commander, subject.GetGUID());
        if (!snapshot)
        {
            handler->PSendSysMessage("rts-spike: %s is not claimed by this RTS session.", subject.GetName());
            return false;
        }
        return SubmitMove(handler, commander, *snapshot, x, y, z);
    }

    static bool HandleCommandMoveOriginal(ChatHandler* handler, float x, float y, float z)
    {
        Player* commander = handler->GetPlayer();
        if (!commander)
            return false;
        Optional<CommandablePlayerSnapshot> snapshot = FindSubject(commander, commander->GetGUID());
        if (!snapshot)
        {
            handler->PSendSysMessage("rts-spike: the original character is not claimed.");
            return false;
        }
        return SubmitMove(handler, commander, *snapshot, x, y, z);
    }

    static bool HandleCommandMoveFive(ChatHandler* handler, float x, float y, float z)
    {
        Player* commander = handler->GetPlayer();
        if (!commander)
            return false;

        static constexpr std::array<std::array<float, 2>, 5> offsets = {{
            {{ -4.0f, -4.0f }}, {{ 4.0f, -4.0f }}, {{ 0.0f, 0.0f }},
            {{ -4.0f, 4.0f }}, {{ 4.0f, 4.0f }}
        }};
        uint32 ordered = 0;
        bool allAccepted = true;
        for (CommandablePlayerSnapshot const& subject : sCommandablePlayerService->GetRtsSubjects(commander->GetGUID()))
        {
            if (subject.OriginalCharacter || ordered >= offsets.size())
                continue;
            allAccepted = SubmitMove(handler, commander, subject,
                x + offsets[ordered][0], y + offsets[ordered][1], z) && allAccepted;
            ++ordered;
        }
        handler->PSendSysMessage("rts-spike: issued %u bot move(s) with fixed test offsets.", ordered);
        return ordered != 0 && allAccepted;
    }

    static bool HandleCommandStop(ChatHandler* handler, PlayerIdentifier subject)
    {
        Player* commander = handler->GetPlayer();
        return commander && SubmitSimple(handler, commander, subject, CommandablePlayerOperation::Hold);
    }

    static bool HandleCommandRelease(ChatHandler* handler, PlayerIdentifier subject)
    {
        Player* commander = handler->GetPlayer();
        return commander && SubmitSimple(handler, commander, subject, CommandablePlayerOperation::Release);
    }

    static bool HandleCommandExit(ChatHandler* handler)
    {
        Player* commander = handler->GetPlayer();
        if (!commander)
            return false;
        CommandablePlayerResult result = sCommandablePlayerService->ExitRts(commander->GetGUID());
        handler->PSendSysMessage("rts-spike: exit RTS: %s.", CommandablePlayerResultCodeName(result.Code));
        return bool(result);
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
