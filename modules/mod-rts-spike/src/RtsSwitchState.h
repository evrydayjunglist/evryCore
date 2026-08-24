/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef EVRY_MOD_RTS_SPIKE_SWITCH_STATE_H
#define EVRY_MOD_RTS_SPIKE_SWITCH_STATE_H

#include "Define.h"
#include "ObjectGuid.h"

enum class RtsSwitchPhase : uint8
{
    Idle,
    Quiescing,
    Active
};

class RtsSwitchState
{
public:
    bool Begin(ObjectGuid controllerGuid, ObjectGuid subjectGuid)
    {
        if (_phase != RtsSwitchPhase::Idle || controllerGuid.IsEmpty() || subjectGuid.IsEmpty() || controllerGuid == subjectGuid)
            return false;

        _controllerGuid = controllerGuid;
        _subjectGuid = subjectGuid;
        _quiesceElapsedMs = 0;
        _phase = RtsSwitchPhase::Quiescing;
        return true;
    }

    bool AdvanceQuiescing(uint32 diff, uint32 delayMs)
    {
        if (_phase != RtsSwitchPhase::Quiescing)
            return false;

        _quiesceElapsedMs += diff;
        return _quiesceElapsedMs >= delayMs;
    }

    bool Activate(ObjectGuid controllerGuid, ObjectGuid subjectGuid)
    {
        if (_phase != RtsSwitchPhase::Quiescing || controllerGuid != _controllerGuid || subjectGuid != _subjectGuid)
            return false;

        _phase = RtsSwitchPhase::Active;
        return true;
    }

    void Clear()
    {
        _controllerGuid.Clear();
        _subjectGuid.Clear();
        _quiesceElapsedMs = 0;
        _phase = RtsSwitchPhase::Idle;
    }

    bool Contains(ObjectGuid guid) const
    {
        return !guid.IsEmpty() && (guid == _controllerGuid || guid == _subjectGuid);
    }

    RtsSwitchPhase Phase() const { return _phase; }
    uint32 QuiesceElapsedMs() const { return _quiesceElapsedMs; }
    ObjectGuid ControllerGuid() const { return _controllerGuid; }
    ObjectGuid SubjectGuid() const { return _subjectGuid; }

private:
    ObjectGuid _controllerGuid;
    ObjectGuid _subjectGuid;
    uint32 _quiesceElapsedMs = 0;
    RtsSwitchPhase _phase = RtsSwitchPhase::Idle;
};

#endif
