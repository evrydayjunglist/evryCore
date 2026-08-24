/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_COMMANDABLE_PLAYER_STATE_H
#define EVRY_COMMANDABLE_PLAYER_STATE_H

#include "CommandablePlayer.h"

class CommandablePlayerState
{
public:
    CommandablePlayerResult Acquire(ObjectGuid subject, CommandableControllerIdentity baseline,
        CommandableControllerIdentity controller)
    {
        if (_active)
        {
            if (_controller != controller)
                return { CommandablePlayerResultCode::ControllerMismatch, _generation };
            if (_subject != subject)
                return { CommandablePlayerResultCode::InvalidSubject, _generation };
            return { CommandablePlayerResultCode::Accepted, _generation };
        }

        _subject = subject;
        _baseline = baseline;
        _controller = controller;
        _directive = CommandablePlayerDirective::Hold;
        _active = true;
        _deathSuspended = false;
        _teleportSuspended = false;
        ++_generation;
        return { CommandablePlayerResultCode::Accepted, _generation };
    }

    CommandablePlayerResult Apply(CommandablePlayerRequest const& request)
    {
        if (!_active)
            return { CommandablePlayerResultCode::NoClaim, _generation };
        if (request.Subject != _subject)
            return { CommandablePlayerResultCode::InvalidSubject, _generation };
        if (request.Controller != _controller)
            return { CommandablePlayerResultCode::ControllerMismatch, _generation };
        if (request.Generation != _generation)
            return { CommandablePlayerResultCode::StaleGeneration, _generation };
        if (request.Operation == CommandablePlayerOperation::Release)
            return Release(request.Controller, request.Generation);
        if (_deathSuspended || _teleportSuspended)
            return { CommandablePlayerResultCode::Suspended, _generation };

        switch (request.Operation)
        {
            case CommandablePlayerOperation::Move:
                _directive = CommandablePlayerDirective::Move;
                break;
            case CommandablePlayerOperation::Hold:
                _directive = CommandablePlayerDirective::Hold;
                break;
            case CommandablePlayerOperation::Release:
                break;
        }

        return { CommandablePlayerResultCode::Accepted, _generation };
    }

    CommandablePlayerResult Release(CommandableControllerIdentity controller, uint64 generation)
    {
        if (!_active)
            return { CommandablePlayerResultCode::NoClaim, _generation };
        if (controller != _controller)
            return { CommandablePlayerResultCode::ControllerMismatch, _generation };
        if (generation != _generation)
            return { CommandablePlayerResultCode::StaleGeneration, _generation };

        _controller = _baseline;
        _directive = CommandablePlayerDirective::None;
        _active = false;
        _deathSuspended = false;
        _teleportSuspended = false;
        ++_generation;
        return { CommandablePlayerResultCode::Accepted, _generation };
    }

    bool Invalidate()
    {
        if (!_active)
            return false;

        _controller = _baseline;
        _directive = CommandablePlayerDirective::None;
        _active = false;
        _deathSuspended = false;
        _teleportSuspended = false;
        ++_generation;
        return true;
    }

    bool SuspendForDeath()
    {
        if (!_active || _deathSuspended)
            return false;

        _directive = CommandablePlayerDirective::Recovering;
        _deathSuspended = true;
        _teleportSuspended = false;
        ++_generation;
        return true;
    }

    bool FinishDeathRecovery()
    {
        if (!_active || !_deathSuspended)
            return false;

        _deathSuspended = false;
        _directive = CommandablePlayerDirective::Hold;
        return true;
    }

    bool SuspendForTeleport()
    {
        if (!_active || _teleportSuspended)
            return false;

        _directive = CommandablePlayerDirective::Hold;
        _teleportSuspended = true;
        ++_generation;
        return true;
    }

    bool FinishTeleportRevalidation()
    {
        if (!_active || !_teleportSuspended)
            return false;

        _teleportSuspended = false;
        _directive = CommandablePlayerDirective::Hold;
        return true;
    }

    void Arrive()
    {
        if (_active && !_deathSuspended && !_teleportSuspended)
            _directive = CommandablePlayerDirective::Hold;
    }

    bool Active() const { return _active; }
    bool DeathSuspended() const { return _deathSuspended; }
    bool TeleportSuspended() const { return _teleportSuspended; }
    uint64 Generation() const { return _generation; }
    ObjectGuid Subject() const { return _subject; }
    CommandableControllerIdentity Controller() const { return _controller; }
    CommandableControllerIdentity Baseline() const { return _baseline; }
    CommandablePlayerDirective Directive() const { return _directive; }

private:
    ObjectGuid _subject;
    CommandableControllerIdentity _controller;
    CommandableControllerIdentity _baseline;
    CommandablePlayerDirective _directive = CommandablePlayerDirective::None;
    uint64 _generation = 0;
    bool _active = false;
    bool _deathSuspended = false;
    bool _teleportSuspended = false;
};

#endif
