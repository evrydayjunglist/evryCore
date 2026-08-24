/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

// Throwaway measurement harness for RTS commander-mode feasibility. It uses
// only stock retail packets and existing core control primitives. It is not a
// production possession system and deliberately does not reroute input.

#include "RtsSwitchState.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "CombatPackets.h"
#include "Group.h"
#include "Log.h"
#include "MiscPackets.h"
#include "MovementPackets.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellPackets.h"
#include "WorldSession.h"
#include "WorldPacket.h"
#include "mod-playerbots/src/PlayerbotMgr.h"

#include <array>
#include <atomic>
#include <string>

using namespace Trinity::ChatCommands;

namespace
{
std::atomic<uint32> ReticleSpellId = { 0 };
std::atomic<bool> ReticleAnyDest = { false };

constexpr uint32 MinimumQuiesceMs = 250;
constexpr uint32 QuiesceTimeoutMs = 2'000;

enum class SwitchReleaseReason : uint8
{
    Explicit,
    SubjectDied,
    ControllerLogout,
    SubjectLogout,
    Teleport,
    MapChanged,
    GroupChanged,
    InvalidControlState,
    PartialActivation,
    Shutdown,
    QuiesceFailed
};

char const* PhaseName(RtsSwitchPhase phase)
{
    switch (phase)
    {
        case RtsSwitchPhase::Idle: return "idle";
        case RtsSwitchPhase::Quiescing: return "quiescing";
        case RtsSwitchPhase::Active: return "active";
    }
    return "unknown";
}

char const* ReleaseReasonName(SwitchReleaseReason reason)
{
    switch (reason)
    {
        case SwitchReleaseReason::Explicit: return "explicit release";
        case SwitchReleaseReason::SubjectDied: return "controlled bot died";
        case SwitchReleaseReason::ControllerLogout: return "controller logout";
        case SwitchReleaseReason::SubjectLogout: return "controlled bot logout";
        case SwitchReleaseReason::Teleport: return "teleport started";
        case SwitchReleaseReason::MapChanged: return "map changed";
        case SwitchReleaseReason::GroupChanged: return "group changed";
        case SwitchReleaseReason::InvalidControlState: return "control state changed";
        case SwitchReleaseReason::PartialActivation: return "partial activation";
        case SwitchReleaseReason::Shutdown: return "world shutdown";
        case SwitchReleaseReason::QuiesceFailed: return "bot did not stop";
    }
    return "unknown";
}

struct ActionBarSnapshot
{
    void Capture(Player const* player)
    {
        Slots.fill(0);
        for (auto const& [index, button] : player->GetActionButtons())
            if (index < Slots.size() && button.uState != ACTIONBUTTON_DELETED)
                Slots[index] = button.packedData;
    }

    uint32 CountDifferences(Player const* player) const
    {
        std::array<uint64, MAX_ACTION_BUTTONS> current = { };
        for (auto const& [index, button] : player->GetActionButtons())
            if (index < current.size() && button.uState != ACTIONBUTTON_DELETED)
                current[index] = button.packedData;

        uint32 count = 0;
        for (std::size_t index = 0; index < Slots.size(); ++index)
            if (Slots[index] != current[index])
                ++count;
        return count;
    }

    void Restore(Player* player) const
    {
        for (uint16 index = 0; index < MAX_ACTION_BUTTONS; ++index)
        {
            uint8 const slot = uint8(index);
            player->RemoveActionButton(slot);
            if (uint64 const packed = Slots[index])
                player->AddActionButton(slot, ACTION_BUTTON_ACTION(packed), uint8(ACTION_BUTTON_TYPE(packed)));
        }
    }

    std::array<uint64, MAX_ACTION_BUTTONS> Slots = { };
};

void SendActorUi(Player* receiver, Player const* actor, char const* reason)
{
    if (!receiver || !receiver->GetSession() || receiver->GetSession()->PlayerLogout() || !actor)
        return;

    WorldPackets::Spells::SendKnownSpells knownSpells;
    for (auto const& [spellId, spell] : actor->GetSpellMap())
    {
        if (spell.state == PLAYERSPELL_REMOVED || !spell.active || spell.disabled)
            continue;

        knownSpells.KnownSpells.push_back(spellId);
        if (spell.favorite)
            knownSpells.FavoriteSpells.push_back(spellId);
    }
    receiver->SendDirectMessage(knownSpells.Write());

    WorldPackets::Spells::UpdateActionButtons actions;
    for (auto const& [index, button] : actor->GetActionButtons())
        if (index < actions.ActionButtons.size() && button.uState != ACTIONBUTTON_DELETED)
            actions.ActionButtons[index] = button.packedData;
    actions.Reason = 0;
    receiver->SendDirectMessage(actions.Write());

    WorldPackets::Spells::SendSpellHistory history;
    actor->GetSpellHistory()->WritePacket(&history);
    receiver->SendDirectMessage(history.Write());

    WorldPackets::Spells::SendSpellCharges charges;
    actor->GetSpellHistory()->WritePacket(&charges);
    receiver->SendDirectMessage(charges.Write());

    TC_LOG_INFO("server.worldserver", "mod-rts-spike: sent stock actor UI probe ({}) to {} from {}: {} known spells, {} action buttons, {} cooldowns, {} charge categories",
        reason, receiver->GetName(), actor->GetName(), knownSpells.KnownSpells.size(), actor->GetActionButtons().size(), history.Entries.size(), charges.Entries.size());
}

bool HasTravelState(Player const* player)
{
    return player->IsBeingTeleported() || player->IsInFlight() || player->GetVehicle() || player->GetTransport();
}

bool HasNormalViewpoint(Player const* player)
{
    WorldObject* viewpoint = player->GetViewpoint();
    return !viewpoint || viewpoint == player;
}

class DirectSwitchHarness
{
public:
    static DirectSwitchHarness& Instance()
    {
        static DirectSwitchHarness instance;
        return instance;
    }

    bool Begin(ChatHandler* handler, Player* subject)
    {
        Player* controller = handler && handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        std::string error;
        if (!ValidatePair(controller, subject, error))
        {
            handler->PSendSysMessage("rts-spike: switch refused: %s", error.c_str());
            return false;
        }

        if (!_state.Begin(controller->GetGUID(), subject->GetGUID()))
        {
            handler->PSendSysMessage("rts-spike: switch refused: another direct-switch probe is already %s.", PhaseName(_state.Phase()));
            return false;
        }

        _mapId = controller->GetMapId();
        _controllerBars.Capture(controller);
        _subjectBars.Capture(subject);
        _controllerWasDead = false;

        if (!sPlayerbotMgr->SetDirectControlSpike(subject, true))
        {
            _state.Clear();
            handler->SendSysMessage("rts-spike: switch refused: the managed bot could not quiesce safely.");
            return false;
        }

        handler->PSendSysMessage("rts-spike: quiescing %s; direct switch will start after it is stationary.", subject->GetName());
        LogState("quiesce-start", controller, subject);
        return true;
    }

    bool Release(SwitchReleaseReason reason, Player* controllerHint = nullptr, Player* subjectHint = nullptr)
    {
        if (_state.Phase() == RtsSwitchPhase::Idle)
            return false;

        Player* controller = Resolve(_state.ControllerGuid(), controllerHint);
        Player* subject = Resolve(_state.SubjectGuid(), subjectHint);
        uint32 ownerBarChanges = controller ? _controllerBars.CountDifferences(controller) : 0;
        uint32 botBarChanges = subject ? _subjectBars.CountDifferences(subject) : 0;

        if (controller && subject && controller->GetUnitBeingMoved() == subject)
            controller->SetClientControl(subject, false);

        if (controller)
        {
            if (WorldObject* viewpoint = controller->GetViewpoint(); viewpoint && viewpoint != controller)
                controller->SetViewpoint(viewpoint, false);
            controller->SetMovedUnit(controller);
            controller->SetClientControl(controller, true);

            _controllerBars.Restore(controller);
            SendActorUi(controller, controller, "release-owner-restore");
        }

        if (subject)
        {
            subject->SetMovedUnit(subject);
            sPlayerbotMgr->SetDirectControlSpike(subject, false);
        }

        TC_LOG_INFO("server.worldserver", "mod-rts-spike: released direct switch ({}) controller={} subject={} owner-bar-differences={} bot-bar-differences={} controller-dead={}",
            ReleaseReasonName(reason), controller ? controller->GetName() : "offline", subject ? subject->GetName() : "offline",
            ownerBarChanges, botBarChanges, _controllerWasDead);

        if (controller && controller->GetSession() && !controller->GetSession()->PlayerLogout())
            ChatHandler(controller->GetSession()).PSendSysMessage("rts-spike: released %s; owner bars restored (%u changed slots observed).", ReleaseReasonName(reason), ownerBarChanges);

        _state.Clear();
        _mapId = MAPID_INVALID;
        _controllerWasDead = false;
        return true;
    }

    void Update(uint32 diff)
    {
        if (_state.Phase() == RtsSwitchPhase::Idle)
            return;

        Player* controller = ObjectAccessor::FindConnectedPlayer(_state.ControllerGuid());
        Player* subject = ObjectAccessor::FindConnectedPlayer(_state.SubjectGuid());
        if (!controller)
        {
            Release(SwitchReleaseReason::ControllerLogout, nullptr, subject);
            return;
        }
        if (!subject)
        {
            Release(SwitchReleaseReason::SubjectLogout, controller, nullptr);
            return;
        }

        if (!subject->IsAlive())
        {
            Release(SwitchReleaseReason::SubjectDied, controller, subject);
            return;
        }
        if (controller->IsBeingTeleported() || subject->IsBeingTeleported())
        {
            Release(SwitchReleaseReason::Teleport, controller, subject);
            return;
        }
        if (controller->GetMapId() != _mapId || subject->GetMapId() != _mapId)
        {
            Release(SwitchReleaseReason::MapChanged, controller, subject);
            return;
        }
        if (!controller->GetGroup() || controller->GetGroup() != subject->GetGroup())
        {
            Release(SwitchReleaseReason::GroupChanged, controller, subject);
            return;
        }
        if (HasTravelState(controller) || HasTravelState(subject))
        {
            Release(SwitchReleaseReason::InvalidControlState, controller, subject);
            return;
        }

        if (_state.Phase() == RtsSwitchPhase::Quiescing)
        {
            if (!controller->IsAlive() || controller->IsFalling() || subject->IsFalling() || controller->isMoving() ||
                controller->GetUnitBeingMoved() != controller || controller->GetPlayerMovingMe() != controller || !HasNormalViewpoint(controller) ||
                subject->GetUnitBeingMoved() != subject || subject->GetPlayerMovingMe() != subject || !HasNormalViewpoint(subject) ||
                !controller->GetCharmerGUID().IsEmpty() || controller->GetCharmed() || !subject->GetCharmerGUID().IsEmpty() || subject->GetCharmed())
            {
                Release(SwitchReleaseReason::InvalidControlState, controller, subject);
                return;
            }

            bool const waited = _state.AdvanceQuiescing(diff, MinimumQuiesceMs);
            if (waited && !subject->isMoving())
            {
                Activate(controller, subject);
                return;
            }

            if (_state.QuiesceElapsedMs() >= QuiesceTimeoutMs)
                Release(SwitchReleaseReason::QuiesceFailed, controller, subject);
            return;
        }

        // The original character may die while the bot remains controlled. Do
        // not resurrect or hide that state; release returns to the dead owner.
        if (!controller->IsAlive() && !_controllerWasDead)
        {
            _controllerWasDead = true;
            TC_LOG_INFO("server.worldserver", "mod-rts-spike: original controller {} died while {} remains controlled", controller->GetName(), subject->GetName());
            ChatHandler(controller->GetSession()).PSendSysMessage("rts-spike: your original character died; control of %s remains active. Release will return to the real death state.", subject->GetName());
        }

        if (uint32 const changedSlots = _controllerBars.CountDifferences(controller))
        {
            TC_LOG_INFO("server.worldserver", "mod-rts-spike: observed {} owner action-bar slot changes during direct control; restoring the owner snapshot and re-sending the subject probe", changedSlots);
            _controllerBars.Restore(controller);
            SendActorUi(controller, subject, "owner-bar-protection");
        }

        if ((controller->IsAlive() && (controller->isMoving() || controller->IsFalling())) ||
            controller->GetUnitBeingMoved() != subject || subject->GetPlayerMovingMe() != controller || controller->GetViewpoint() != subject)
            Release(SwitchReleaseReason::InvalidControlState, controller, subject);
    }

    void OnLogout(Player* player)
    {
        if (!_state.Contains(player ? player->GetGUID() : ObjectGuid::Empty))
            return;
        Release(player->GetGUID() == _state.ControllerGuid() ? SwitchReleaseReason::ControllerLogout : SwitchReleaseReason::SubjectLogout,
            player->GetGUID() == _state.ControllerGuid() ? player : nullptr,
            player->GetGUID() == _state.SubjectGuid() ? player : nullptr);
    }

    void OnMapChanged(Player* player)
    {
        if (_state.Contains(player ? player->GetGUID() : ObjectGuid::Empty))
            Release(SwitchReleaseReason::MapChanged, player->GetGUID() == _state.ControllerGuid() ? player : nullptr,
                player->GetGUID() == _state.SubjectGuid() ? player : nullptr);
    }

    void OnGroupChanged(ObjectGuid guid)
    {
        if (_state.Contains(guid))
            Release(SwitchReleaseReason::GroupChanged);
    }

    bool IsActiveController(WorldSession const* session) const
    {
        return _state.Phase() == RtsSwitchPhase::Active && session && session->GetPlayer() && session->GetPlayer()->GetGUID() == _state.ControllerGuid();
    }

    bool IsParticipant(Player const* player) const
    {
        return player && _state.Contains(player->GetGUID());
    }

    RtsSwitchPhase Phase() const { return _state.Phase(); }

    void Report(ChatHandler* handler) const
    {
        Player* controller = ObjectAccessor::FindConnectedPlayer(_state.ControllerGuid());
        Player* subject = ObjectAccessor::FindConnectedPlayer(_state.SubjectGuid());
        handler->PSendSysMessage("rts-spike: switch %s; controller %s; subject %s; elapsed %u ms.", PhaseName(_state.Phase()),
            controller ? controller->GetName() : "none", subject ? subject->GetName() : "none", _state.QuiesceElapsedMs());
        if (controller)
            LogState("status", controller, subject);
    }

private:
    static Player* Resolve(ObjectGuid guid, Player* hint)
    {
        if (hint && hint->GetGUID() == guid)
            return hint;
        return ObjectAccessor::FindConnectedPlayer(guid);
    }

    bool ValidatePair(Player* controller, Player* subject, std::string& error) const
    {
        auto fail = [&error](char const* message)
        {
            error = message;
            return false;
        };

        if (_state.Phase() != RtsSwitchPhase::Idle)
            return fail("another probe already owns the switch");
        if (!controller || !controller->GetSession() || controller->GetSession()->PlayerLogout())
            return fail("controller has no live session");
        if (!subject || !subject->GetSession() || subject->GetSession()->PlayerLogout())
            return fail("select or name an online playerbot");
        if (controller == subject)
            return fail("controller and subject must differ");
        if (sPlayerbotMgr->IsManagedBot(controller))
            return fail("a managed bot cannot be the controller");
        if (!sPlayerbotMgr->IsManagedBot(subject))
            return fail("subject is not a managed playerbot");
        if (!controller->IsInWorld() || !subject->IsInWorld())
            return fail("both players must be in world");
        if (!controller->IsAlive() || !subject->IsAlive())
            return fail("both players must be alive at acquisition");
        if (HasTravelState(controller) || HasTravelState(subject) || controller->IsFalling() || subject->IsFalling())
            return fail("transport, vehicle, flight, fall, or teleport state is active");
        if (controller->GetMap() != subject->GetMap())
            return fail("both players must be on the same map instance");
        if (!controller->GetGroup() || controller->GetGroup() != subject->GetGroup())
            return fail("both players must be in the same group");
        if (!controller->HaveAtClient(subject))
            return fail("the subject is not visible to the controller client");
        if (!controller->GetCharmerGUID().IsEmpty() || controller->GetCharmed() || !subject->GetCharmerGUID().IsEmpty() || subject->GetCharmed())
            return fail("charm or possession state is already active");
        if (controller->GetUnitBeingMoved() != controller || controller->GetPlayerMovingMe() != controller)
            return fail("controller does not own its normal mover");
        if (!HasNormalViewpoint(controller))
            return fail("controller already has a non-default viewpoint");
        if (subject->GetUnitBeingMoved() != subject || subject->GetPlayerMovingMe() != subject)
            return fail("subject does not own its normal mover");
        if (!HasNormalViewpoint(subject))
            return fail("subject already has a non-default viewpoint");
        if (controller->isMoving())
            return fail("original character must be stationary");
        return true;
    }

    void Activate(Player* controller, Player* subject)
    {
        controller->SetViewpoint(subject, true);
        if (controller->GetViewpoint() != subject)
        {
            Release(SwitchReleaseReason::PartialActivation, controller, subject);
            return;
        }

        controller->SetMovedUnit(subject);
        if (controller->GetUnitBeingMoved() != subject || subject->GetPlayerMovingMe() != controller)
        {
            Release(SwitchReleaseReason::PartialActivation, controller, subject);
            return;
        }

        controller->SetClientControl(subject, true);
        if (controller->GetUnitBeingMoved() != subject || subject->GetPlayerMovingMe() != controller || controller->GetViewpoint() != subject)
        {
            Release(SwitchReleaseReason::PartialActivation, controller, subject);
            return;
        }

        if (!_state.Activate(controller->GetGUID(), subject->GetGUID()))
        {
            Release(SwitchReleaseReason::PartialActivation, controller, subject);
            return;
        }

        SendActorUi(controller, subject, "acquire-subject-probe");
        LogState("active", controller, subject);
        ChatHandler(controller->GetSession()).PSendSysMessage("rts-spike: controlling %s. Use .rtsspike release before normal play.", subject->GetName());
    }

    static void LogState(char const* event, Player const* controller, Player const* subject)
    {
        TC_LOG_INFO("server.worldserver", "mod-rts-spike: {} controller={} alive={} map={} mover={} viewpoint={} selection={} subject={} alive={} map={} moving={} health={}/{} power={}/{}",
            event, controller ? controller->GetName() : "none", controller && controller->IsAlive(), controller ? controller->GetMapId() : MAPID_INVALID,
            controller && controller->GetUnitBeingMoved() ? controller->GetUnitBeingMoved()->GetGUID().ToString() : "none",
            controller && controller->GetViewpoint() ? controller->GetViewpoint()->GetGUID().ToString() : "none",
            controller ? controller->GetTarget().ToString() : "none",
            subject ? subject->GetName() : "none", subject && subject->IsAlive(), subject ? subject->GetMapId() : MAPID_INVALID,
            subject && subject->isMoving(), subject ? subject->GetHealth() : 0, subject ? subject->GetMaxHealth() : 0,
            subject ? subject->GetPower(subject->GetPowerType()) : 0, subject ? subject->GetMaxPower(subject->GetPowerType()) : 0);
    }

    RtsSwitchState _state;
    ActionBarSnapshot _controllerBars;
    ActionBarSnapshot _subjectBars;
    uint32 _mapId = MAPID_INVALID;
    bool _controllerWasDead = false;
};
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
            { "camflags",    camFlagsTable },
            { "reticle",     reticleTable },
            { "acceptinvite", HandleAcceptInviteCommand, rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
            { "switch",      HandleSwitchCommand,       rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
            { "release",     HandleReleaseCommand,      rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
            { "switchstatus", HandleSwitchStatusCommand, rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
            { "status",      HandleStatusCommand,       rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
        };
        static ChatCommandTable commandTable =
        {
            { "rtsspike", spikeTable },
        };
        return commandTable;
    }

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

    static bool HandleSwitchCommand(ChatHandler* handler, Optional<std::string> subjectName)
    {
        Player* subject = nullptr;
        if (subjectName)
        {
            normalizePlayerName(*subjectName);
            subject = ObjectAccessor::FindPlayerByName(*subjectName);
        }
        else
            subject = handler->getSelectedPlayer();

        if (!subject)
        {
            handler->SendSysMessage("rts-spike: select an online managed playerbot or use .rtsspike switch Name.");
            return true;
        }

        DirectSwitchHarness::Instance().Begin(handler, subject);
        return true;
    }

    static bool HandleAcceptInviteCommand(ChatHandler* handler, std::string subjectName)
    {
        Player* controller = handler->GetSession()->GetPlayer();
        if (!controller || !normalizePlayerName(subjectName))
            return false;

        Player* subject = ObjectAccessor::FindPlayerByName(subjectName);
        if (!subject || !subject->GetSession() || subject->GetSession()->PlayerLogout())
        {
            handler->PSendSysMessage("rts-spike: invite acceptance refused: %s is not online.", subjectName.c_str());
            return true;
        }
        if (DirectSwitchHarness::Instance().Phase() != RtsSwitchPhase::Idle)
        {
            handler->SendSysMessage("rts-spike: invite acceptance refused while a direct-switch probe is reserved.");
            return true;
        }
        if (!sPlayerbotMgr->IsManagedBot(subject))
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

    static bool HandleReleaseCommand(ChatHandler* handler)
    {
        if (!DirectSwitchHarness::Instance().Release(SwitchReleaseReason::Explicit))
            handler->SendSysMessage("rts-spike: no direct switch is active.");
        return true;
    }

    static bool HandleSwitchStatusCommand(ChatHandler* handler)
    {
        DirectSwitchHarness::Instance().Report(handler);
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
        handler->SendSysMessage("rts-spike: logging ground destinations for every player cast. Cast any ground-targeted spell.");
        return true;
    }

    static bool HandleReticleOffCommand(ChatHandler* handler)
    {
        ReticleSpellId = 0;
        ReticleAnyDest = false;
        handler->SendSysMessage("rts-spike: reticle logging off.");
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
        handler->PSendSysMessage("rts-spike: commentator flags %s. Reticle logging: %s. Direct switch: %s.",
            flags ? "ON" : "off", anyDest ? "every ground cast" : (spellId ? Trinity::StringFormat("spell {}", spellId) : "off"),
            PhaseName(DirectSwitchHarness::Instance().Phase()));
        return true;
    }
};

namespace
{
bool IsMeasuredMovementOpcode(OpcodeClient opcode)
{
    switch (opcode)
    {
        case CMSG_SET_ACTIVE_MOVER:
        case CMSG_MOVE_START_FORWARD:
        case CMSG_MOVE_START_BACKWARD:
        case CMSG_MOVE_STOP:
        case CMSG_MOVE_START_STRAFE_LEFT:
        case CMSG_MOVE_START_STRAFE_RIGHT:
        case CMSG_MOVE_STOP_STRAFE:
        case CMSG_MOVE_START_TURN_LEFT:
        case CMSG_MOVE_START_TURN_RIGHT:
        case CMSG_MOVE_STOP_TURN:
        case CMSG_MOVE_HEARTBEAT:
        case CMSG_MOVE_JUMP:
        case CMSG_MOVE_FALL_LAND:
        case CMSG_MOVE_SET_FACING:
        case CMSG_MOVE_SET_PITCH:
            return true;
        default:
            return false;
    }
}

bool IsMeasuredServerOpcode(OpcodeServer opcode)
{
    switch (opcode)
    {
        case SMSG_CONTROL_UPDATE:
        case SMSG_MOVE_SET_ACTIVE_MOVER:
        case SMSG_ATTACK_START:
        case SMSG_ATTACK_STOP:
        case SMSG_CAST_FAILED:
        case SMSG_SPELL_FAILURE:
        case SMSG_SPELL_FAILED_OTHER:
        case SMSG_SPELL_START:
        case SMSG_SPELL_GO:
        case SMSG_SEND_KNOWN_SPELLS:
        case SMSG_UPDATE_ACTION_BUTTONS:
        case SMSG_SEND_SPELL_HISTORY:
        case SMSG_SEND_SPELL_CHARGES:
            return true;
        default:
            return false;
    }
}
}

class rts_spike_packet_logger : public ServerScript
{
public:
    rts_spike_packet_logger() : ServerScript("rts_spike_packet_logger") { }

    void OnPacketReceive(WorldSession* session, WorldPacket& packet) override
    {
        DirectSwitchHarness& harness = DirectSwitchHarness::Instance();
        if (!harness.IsActiveController(session))
            return;

        Player* owner = session->GetPlayer();
        Unit* mover = owner->GetUnitBeingMoved();
        OpcodeClient const opcode = static_cast<OpcodeClient>(packet.GetOpcode());
        if (opcode == CMSG_LOGOUT_REQUEST)
        {
            TC_LOG_INFO("server.worldserver", "mod-rts-spike: controller logout request received; restoring direct control before the stock logout handler runs");
            harness.Release(SwitchReleaseReason::ControllerLogout, owner, mover ? mover->ToPlayer() : nullptr);
            return;
        }

        if (IsMeasuredMovementOpcode(opcode))
        {
            if (opcode == CMSG_SET_ACTIVE_MOVER)
            {
                WorldPackets::Movement::SetActiveMover parsed{ WorldPacket(packet) };
                parsed.Read();
                TC_LOG_INFO("server.worldserver", "mod-rts-spike: input {} session-owner={} packet-mover={} expected-active-mover={} route=active-mover-validation",
                    GetOpcodeNameForLogging(opcode), owner->GetName(), parsed.ActiveMover.ToString(), mover ? mover->GetGUID().ToString() : "none");
            }
            else
            {
                WorldPackets::Movement::ClientPlayerMovement parsed{ WorldPacket(packet) };
                parsed.Read();
                TC_LOG_INFO("server.worldserver", "mod-rts-spike: input {} session-owner={} packet-mover={} expected-active-mover={} position={} route=active-mover-validation",
                    GetOpcodeNameForLogging(opcode), owner->GetName(), parsed.Status.guid.ToString(), mover ? mover->GetGUID().ToString() : "none", parsed.Status.pos);
            }
            return;
        }

        switch (opcode)
        {
            case CMSG_SET_SELECTION:
            {
                WorldPackets::Misc::SetSelection parsed{ WorldPacket(packet) };
                parsed.Read();
                TC_LOG_INFO("server.worldserver", "mod-rts-spike: input CMSG_SET_SELECTION session-owner={} active-mover={} target={} route=session-owner",
                    owner->GetName(), mover ? mover->GetGUID().ToString() : "none", parsed.Selection.ToString());
                break;
            }
            case CMSG_ATTACK_SWING:
            {
                WorldPackets::Combat::AttackSwing parsed{ WorldPacket(packet) };
                parsed.Read();
                TC_LOG_INFO("server.worldserver", "mod-rts-spike: input CMSG_ATTACK_SWING session-owner={} active-mover={} target={} route=session-owner",
                    owner->GetName(), mover ? mover->GetGUID().ToString() : "none", parsed.Victim.ToString());
                break;
            }
            case CMSG_CAST_SPELL:
            {
                WorldPackets::Spells::CastSpell parsed{ WorldPacket(packet) };
                parsed.Read();
                TC_LOG_INFO("server.worldserver", "mod-rts-spike: input CMSG_CAST_SPELL session-owner={} active-mover={} spell={} unit-target={} route=session-owner",
                    owner->GetName(), mover ? mover->GetGUID().ToString() : "none", parsed.Cast.SpellID, parsed.Cast.Target.Unit.ToString());
                break;
            }
            case CMSG_SET_ACTION_BUTTON:
            {
                WorldPackets::Spells::SetActionButton parsed{ WorldPacket(packet) };
                parsed.Read();
                TC_LOG_INFO("server.worldserver", "mod-rts-spike: input CMSG_SET_ACTION_BUTTON session-owner={} active-mover={} slot={} packed={} route=session-owner-with-release-rollback",
                    owner->GetName(), mover ? mover->GetGUID().ToString() : "none", parsed.Index, parsed.Action);
                break;
            }
            default:
                break;
        }
    }

    void OnPacketSend(WorldSession* session, WorldPacket& packet) override
    {
        Player* receiver = session ? session->GetPlayer() : nullptr;
        if (!DirectSwitchHarness::Instance().IsParticipant(receiver))
            return;

        OpcodeServer const opcode = static_cast<OpcodeServer>(packet.GetOpcode());
        if (IsMeasuredServerOpcode(opcode))
            TC_LOG_INFO("server.worldserver", "mod-rts-spike: output {} session-player={}", GetOpcodeNameForLogging(opcode), receiver->GetName());
    }
};

class rts_spike_worldscript : public WorldScript
{
public:
    rts_spike_worldscript() : WorldScript("rts_spike_worldscript") { }

    void OnUpdate(uint32 diff) override
    {
        DirectSwitchHarness::Instance().Update(diff);
    }

    void OnShutdownInitiate(ShutdownExitCode /*code*/, ShutdownMask /*mask*/) override
    {
        // Playerbots registers first and logs managed sessions out from its
        // OnShutdown hook. Release while both Players are still authoritative.
        DirectSwitchHarness::Instance().Release(SwitchReleaseReason::Shutdown);
    }

    void OnShutdown() override
    {
        DirectSwitchHarness::Instance().Release(SwitchReleaseReason::Shutdown);
    }
};

class rts_spike_playerscript : public PlayerScript
{
public:
    rts_spike_playerscript() : PlayerScript("rts_spike_playerscript") { }

    void OnLogout(Player* player) override
    {
        DirectSwitchHarness::Instance().OnLogout(player);
    }

    void OnMapChanged(Player* player) override
    {
        DirectSwitchHarness::Instance().OnMapChanged(player);
    }
};

class rts_spike_groupscript : public GroupScript
{
public:
    rts_spike_groupscript() : GroupScript("rts_spike_groupscript") { }

    void OnRemoveMember(Group* /*group*/, ObjectGuid guid, RemoveMethod /*method*/, ObjectGuid /*kicker*/, char const* /*reason*/) override
    {
        DirectSwitchHarness::Instance().OnGroupChanged(guid);
    }

    void OnDisband(Group* group) override
    {
        if (!group)
            return;
        for (Group::MemberSlot const& member : group->GetMemberSlots())
            DirectSwitchHarness::Instance().OnGroupChanged(member.guid);
    }
};

// Runs from Spell::_cast before CheckCast, so the reticle probe also sees a
// cast that later fails. During a direct switch the log records the actual
// Player selected by the stock cast handler.
class rts_spike_spell_logger : public AllSpellScript
{
public:
    rts_spike_spell_logger() : AllSpellScript("rts_spike_spell_logger") { }

    void OnSpellCast(Spell* spell, WorldObject* caster, SpellInfo const* spellInfo, bool /*skipCheck*/) override
    {
        Player* player = caster ? caster->ToPlayer() : nullptr;
        if (!player)
            return;

        if (DirectSwitchHarness::Instance().IsParticipant(player))
            TC_LOG_INFO("server.worldserver", "mod-rts-spike: cast resolved by stock core caster={} spell={} target={} (compare with active-mover input log)",
                player->GetName(), spellInfo->Id, spell->m_targets.GetUnitTargetGUID().ToString());

        uint32 const armedSpellId = ReticleSpellId.load(std::memory_order_relaxed);
        bool const anyDest = ReticleAnyDest.load(std::memory_order_relaxed);
        if (!armedSpellId && !anyDest)
            return;
        if (armedSpellId && spellInfo->Id != armedSpellId)
            return;

        if (!spell->m_targets.HasDst())
        {
            if (armedSpellId && player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage("rts-spike: spell %u cast, but it carries no ground destination.", spellInfo->Id);
            return;
        }

        WorldLocation const* dest = spell->m_targets.GetDstPos();
        float const distance = player->GetExactDist(dest);
        if (player->GetSession())
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
    new rts_spike_packet_logger();
    new rts_spike_worldscript();
    new rts_spike_playerscript();
    new rts_spike_groupscript();
    new rts_spike_spell_logger();
}
