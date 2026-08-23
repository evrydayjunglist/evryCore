-- Throwaway probes for the RTS commander-mode spikes.
--
-- /rtsspike ping            round-trips the TrinityCore addon command channel
-- /rtsspike cmd <text>      runs a server chat command over that channel (no leading dot)
-- /rtsspike cam             dumps what C_Commentator exposes on this client
-- /rtsspike camset x y z    tries C_Commentator.SetCameraPosition (run ".rtsspike camflags on" first)
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

local function send_channel(opcode, text)
    local ok = pcall(C_ChatInfo.RegisterAddonMessagePrefix, PREFIX)
    if not ok then
        print_line("could not register the addon prefix")
    end
    local msg = opcode .. TOKEN .. (text or "")
    local sent = C_ChatInfo.SendAddonMessage(PREFIX, msg, "WHISPER", UnitName("player"))
    print_line(("sent %q (result %s), waiting for the reply..."):format(msg, tostring(sent)))
end

local listener = CreateFrame("Frame")
listener:RegisterEvent("CHAT_MSG_ADDON")
listener:SetScript("OnEvent", function(_, _, prefix, message, channel, sender)
    if prefix ~= PREFIX then
        return
    end
    local opcode = message:sub(1, 1)
    local token = message:sub(2, 5)
    local body = message:sub(6)
    local names = { a = "ack", o = "ok", f = "FAILED", m = "message" }
    print_line(("reply %s (token %s)%s"):format(names[opcode] or opcode, token, body ~= "" and (": " .. body) or ""))
end)

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
    if C_Commentator.IsSpectating then
        local ok, res = pcall(C_Commentator.IsSpectating)
        print_line("IsSpectating() -> " .. tostring(ok and res or "error"))
    end
    if C_Commentator.GetMode then
        local ok, res = pcall(C_Commentator.GetMode)
        print_line("GetMode() -> " .. tostring(ok and res or "error"))
    end
end

local function try_camset(x, y, z)
    if not (C_Commentator and C_Commentator.SetCameraPosition) then
        print_line("C_Commentator.SetCameraPosition does not exist on this client.")
        return
    end
    local ok, err = pcall(C_Commentator.SetCameraPosition, x, y, z, true)
    if ok then
        print_line(("SetCameraPosition(%.1f, %.1f, %.1f) did not error. Did the camera move?"):format(x, y, z))
    else
        print_line("SetCameraPosition errored: " .. tostring(err))
    end
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
    elseif verb == "camset" then
        local x, y, z = rest:match("^(-?[%d%.]+)%s+(-?[%d%.]+)%s+(-?[%d%.]+)$")
        if x then
            try_camset(tonumber(x), tonumber(y), tonumber(z))
        else
            print_line("usage: /rtsspike camset x y z (try coords near your character from .gps)")
        end
    elseif verb == "tactical" then
        tactical_camera()
    else
        print_line("usage: /rtsspike ping | cmd <server command> | cam | camset x y z | tactical")
    end
end

print_line("loaded. /rtsspike for usage.")
