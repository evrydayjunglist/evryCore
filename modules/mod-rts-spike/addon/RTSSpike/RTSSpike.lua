-- Throwaway probes for the RTS commander-mode spikes.
--
-- /rtsspike ping            round-trips the TrinityCore addon command channel
-- /rtsspike cmd <text>      runs a server chat command over that channel (no leading dot)
-- /rtsspike cam             dumps what C_Commentator exposes on this client
-- /rtsspike state           reads the gated and ungated calls, so you can tell which gate is open
-- /rtsspike campos          where the camera is right now
-- /rtsspike camset x y z    tries C_Commentator.SetCameraPosition (run ".rtsspike camflags on" first)
-- /rtsspike camstep dx dy dz  moves the camera by an offset from where it is
-- /rtsspike camlook         points the camera at what it is nearest to
-- /rtsspike camaim yaw pitch fov  aims the camera, to see whether the client's pitch
--                           and zoom clamps still apply in this mode
-- /rtsspike tactical        stock camera fallback: max zoom out, then pitch the view up
--
-- If the TOC Interface number is stale, enable "Load out of date AddOns" or set it to
-- the value of: /dump select(4, GetBuildInfo())

local PREFIX = "TrinityCore"

local function print_line(msg)
    DEFAULT_CHAT_FRAME:AddMessage("|cff33ff99RTSSpike|r: " .. tostring(msg))
end

-- The server-side wire format (AddonChannelCommandHandler in Chat.cpp): byte 0 is the
-- opcode (p ping, i issue command), bytes 1-4 are a caller-chosen echo token, the rest
-- is the command text. Replies come back on this prefix as a whisper to self: a=ack,
-- o=ok, f=failed, m<token><text>=message line.
local TOKEN = "SPK1"

-- Registering the prefix is what makes CHAT_MSG_ADDON fire for the replies. Do it
-- once at load rather than at send time: the server answers a ping in the same
-- tick it receives it, so registering and sending together can race the reply.
local function register_prefix()
    if not (C_ChatInfo and C_ChatInfo.RegisterAddonMessagePrefix) then
        print_line("C_ChatInfo.RegisterAddonMessagePrefix is missing on this client.")
        return false
    end
    local ok, registered = pcall(C_ChatInfo.RegisterAddonMessagePrefix, PREFIX)
    if not ok then
        print_line("registering the prefix threw: " .. tostring(registered))
        return false
    end
    if registered == false then
        print_line("the client refused to register the prefix. Its registration list may be full.")
        return false
    end
    return true
end

-- SendAddonMessage answers with Enum.SendAddonMessageResult, where zero means
-- Success. Printing the bare number invites reading a good send as a failure.
local SEND_RESULTS = {
    [0] = "Success", [1] = "InvalidPrefix", [2] = "InvalidMessage",
    [3] = "AddonMessageThrottle", [4] = "InvalidChatType", [5] = "NotInGroup",
    [6] = "TargetRequired", [7] = "InvalidChannel", [8] = "ChannelThrottle",
    [9] = "GeneralError", [10] = "NotInGuild", [11] = "AddOnMessageLockdown",
    [12] = "TargetOffline",
}

-- Nothing coming back is the ambiguous outcome for this spike, so say so out loud
-- instead of leaving the chat frame quiet.
local waiting = 0

local function send_channel(opcode, text)
    register_prefix()
    local msg = opcode .. TOKEN .. (text or "")
    local sent = C_ChatInfo.SendAddonMessage(PREFIX, msg, "WHISPER", UnitName("player"))
    local named = SEND_RESULTS[sent] or tostring(sent)
    print_line(("sent %q (send result: %s), waiting for the reply..."):format(msg, named))
    waiting = waiting + 1
    local mine = waiting
    C_Timer.After(3, function()
        if waiting == mine then
            print_line("no reply after 3 seconds. Check that the prefix registered, that AddonChannel is on in worldserver.conf, and that you are not muted.")
        end
    end)
end

local listener = CreateFrame("Frame")
listener:RegisterEvent("CHAT_MSG_ADDON")
listener:SetScript("OnEvent", function(_, _, prefix, message, channel, sender)
    if prefix ~= PREFIX then
        return
    end
    waiting = 0
    local opcode = message:sub(1, 1)
    local token = message:sub(2, 5)
    local body = message:sub(6)
    local names = { a = "ack", o = "ok", f = "FAILED", m = "message" }
    print_line(("reply %s (token %s) via %s from %s%s"):format(
        names[opcode] or opcode, token, tostring(channel), tostring(sender),
        body ~= "" and (": " .. body) or ""))
end)

-- Reports a probe honestly. Most camera functions carry the precondition
-- RequiresActiveCommentator in the client's own API documentation, and its
-- failure mode is ReturnNothing: a refused call comes back with no values
-- instead of throwing. So three outcomes have to read differently -- it threw,
-- it returned nothing, or it returned something -- and a returned false must
-- not be reported as an error the way the first version of this probe did.
local function probe(name, fn, ...)
    if type(fn) ~= "function" then
        print_line(name .. "() -> missing on this client")
        return
    end
    local function report(ok, ...)
        if not ok then
            print_line(name .. "() -> threw: " .. tostring((select(1, ...))))
            return
        end
        local count = select("#", ...)
        if count == 0 then
            print_line(name .. "() -> returned nothing (the gate refused it)")
            return
        end
        local parts = {}
        for i = 1, count do
            parts[i] = tostring((select(i, ...)))
        end
        print_line(name .. "() -> " .. table.concat(parts, ", "))
    end
    report(pcall(fn, ...))
end

-- Current camera position, or nil when the client will not give one up.
-- GetCameraPosition is gated on active commentator, so nil here and a number
-- from the ungated GetCameraCollision together say the gate is shut.
local function camera_position()
    if not (C_Commentator and C_Commentator.GetCameraPosition) then
        return nil
    end
    local ok, x, y, z = pcall(C_Commentator.GetCameraPosition)
    if ok and type(x) == "number" then
        return x, y, z
    end
    return nil
end

local function dump_commentator()
    if type(C_Commentator) ~= "table" then
        print_line("C_Commentator does not exist on this client. Rung 1 is dead; move to rung 2.")
        return
    end
    local keys = {}
    for k in pairs(C_Commentator) do
        keys[#keys + 1] = k
    end
    table.sort(keys)
    print_line(("C_Commentator has %d entries:"):format(#keys))
    for _, k in ipairs(keys) do
        print_line("  C_Commentator." .. k)
    end
end

-- The gate test. GetCameraPosition, GetCamera and GetSpeedFactor need active
-- commentator; GetCameraCollision and the two smart camera reads do not. If the
-- gated ones answer, every camera function in the namespace is open.
local function commentator_state()
    if type(C_Commentator) ~= "table" then
        print_line("C_Commentator does not exist on this client.")
        return
    end
    probe("IsSpectating", C_Commentator.IsSpectating)
    probe("GetMode", C_Commentator.GetMode)
    probe("GetCameraCollision", C_Commentator.GetCameraCollision)
    probe("IsUsingSmartCamera", C_Commentator.IsUsingSmartCamera)
    probe("IsSmartCameraLocked", C_Commentator.IsSmartCameraLocked)
    probe("GetCameraPosition", C_Commentator.GetCameraPosition)
    probe("GetCamera", C_Commentator.GetCamera)
    probe("GetSpeedFactor", C_Commentator.GetSpeedFactor)
end

local function try_camset(x, y, z)
    if not (C_Commentator and C_Commentator.SetCameraPosition) then
        print_line("C_Commentator.SetCameraPosition does not exist on this client.")
        return
    end
    local bx, by, bz = camera_position()
    if bx then
        print_line(("camera before: %.1f %.1f %.1f"):format(bx, by, bz))
    else
        print_line("camera before: no position (call refused or no camera yet)")
    end
    local ok, err = pcall(C_Commentator.SetCameraPosition, x, y, z, true)
    if not ok then
        print_line("SetCameraPosition threw: " .. tostring(err))
        return
    end
    local ax, ay, az = camera_position()
    if ax then
        print_line(("camera after:  %.1f %.1f %.1f (asked for %.1f %.1f %.1f)"):format(ax, ay, az, x, y, z))
    else
        print_line("camera after:  no position. The call was accepted silently or refused silently.")
    end
end

-- SetCamera takes position, yaw, pitch, roll and field of view together, so it is
-- the one call that says whether the client's zoom ceiling and pitch limit still
-- apply here. Those two clamps are the whole reason the plan has a patching tier;
-- if pitch straight down and a narrow field of view are accepted, that tier is not
-- needed for the camera. Keeps the current position and changes only the aim.
local function try_camaim(yaw, pitch, fov)
    if not (C_Commentator and C_Commentator.SetCamera and C_Commentator.GetCamera) then
        print_line("C_Commentator.SetCamera or GetCamera is missing on this client.")
        return
    end
    local ok, x, y, z = pcall(C_Commentator.GetCamera)
    if not ok or type(x) ~= "number" then
        print_line("GetCamera gave no camera to aim. Run /rtsspike camset first.")
        return
    end
    local set = pcall(C_Commentator.SetCamera, x, y, z, yaw, pitch, 0, fov)
    if not set then
        print_line("SetCamera threw.")
        return
    end
    probe("GetCamera", C_Commentator.GetCamera)
    print_line(("asked for yaw %.2f pitch %.2f fov %.2f. Compare against the readback above."):format(yaw, pitch, fov))
end

-- Walks the camera by an offset from where it already is, so the streaming test
-- does not need coordinates typed by hand. Positive dz lifts it.
local function try_camstep(dx, dy, dz)
    local x, y, z = camera_position()
    if not x then
        print_line("no camera position to step from. Run /rtsspike camset first.")
        return
    end
    try_camset(x + dx, y + dy, z + dz)
end

-- The always-works fallback: widest stock zoom, then pitch the view up for a while so
-- it looks down. This is rung 4 of the camera ladder, here so height can be judged.
local pitch_ticker
local function tactical_camera()
    pcall(SetCVar, "cameraDistanceMaxZoomFactor", 2.6)
    for _ = 1, 40 do
        CameraZoomOut(1)
    end
    MoveViewUpStart()
    if pitch_ticker then
        pitch_ticker:Cancel()
    end
    pitch_ticker = C_Timer.NewTimer(1.4, function()
        MoveViewUpStop()
        pitch_ticker = nil
    end)
    print_line("max stock zoom + pitching up. Judge whether this height is playable.")
end

SLASH_RTSSPIKE1 = "/rtsspike"
SlashCmdList["RTSSPIKE"] = function(input)
    input = (input or ""):gsub("^%s+", "")
    local verb, rest = input:match("^(%S*)%s*(.-)$")
    verb = verb:lower()
    if verb == "ping" then
        send_channel("p")
    elseif verb == "cmd" and rest ~= "" then
        send_channel("i", rest)
    elseif verb == "cam" then
        dump_commentator()
    elseif verb == "state" then
        commentator_state()
    elseif verb == "campos" then
        probe("GetCameraPosition", C_Commentator and C_Commentator.GetCameraPosition)
    elseif verb == "camset" then
        local x, y, z = rest:match("^(-?[%d%.]+)%s+(-?[%d%.]+)%s+(-?[%d%.]+)$")
        if x then
            try_camset(tonumber(x), tonumber(y), tonumber(z))
        else
            print_line("usage: /rtsspike camset x y z (try coords near your character from .gps)")
        end
    elseif verb == "camstep" then
        local dx, dy, dz = rest:match("^(-?[%d%.]+)%s+(-?[%d%.]+)%s+(-?[%d%.]+)$")
        if dx then
            try_camstep(tonumber(dx), tonumber(dy), tonumber(dz))
        else
            print_line("usage: /rtsspike camstep dx dy dz (offset from where the camera is now)")
        end
    elseif verb == "camlook" then
        probe("SnapCameraLookAtPoint", C_Commentator and C_Commentator.SnapCameraLookAtPoint)
    elseif verb == "camaim" then
        local yaw, pitch, fov = rest:match("^(-?[%d%.]+)%s+(-?[%d%.]+)%s+(-?[%d%.]+)$")
        if yaw then
            try_camaim(tonumber(yaw), tonumber(pitch), tonumber(fov))
        else
            print_line("usage: /rtsspike camaim yaw pitch fov (try: camaim 0 -1.5 1.0 for straight down)")
        end
    elseif verb == "tactical" then
        tactical_camera()
    else
        print_line("usage: /rtsspike ping | cmd <server command> | cam | state | campos | camset x y z | camstep dx dy dz | camlook | camaim yaw pitch fov | tactical")
    end
end

if register_prefix() then
    print_line("loaded, prefix registered. /rtsspike for usage.")
else
    print_line("loaded, but the addon channel prefix did not register. /rtsspike ping will not hear a reply.")
end
