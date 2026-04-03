#pragma once

namespace osxg
{
	constexpr const char* osxg_menu_lua_source = R"lua(
-- OSXG+ Community Extension for YimMenu
-- BASE_URL should be set to your Cloudflare Worker URL
local BASE_URL = "https://osxg-auth.1221647.workers.dev"

local token = nil
local auth_id = nil
local sessions = {}
local last_session_fetch = 0
local fetching_sessions = false
local is_hosting = false
local last_heartbeat = 0

-- Helper: Load token from file
local function load_token()
    local file = io.open("OSXG_Token.txt", "r")
    if file then
        token = file:read("*a")
        file:close()
        if token == "" then token = nil end
    end
end

-- Helper: Save token to file
local function save_token(t)
    local file = io.open("OSXG_Token.txt", "w")
    if file then
        file:write(t or "")
        file:close()
        token = t
    end
end

-- Helper: Parse JSON safely
local function parse_json(str)
    if not str or str == "" then return nil end
    return osxg.json_parse(str)
end

-- Polling for Auth
local function poll_auth()
    script.run_in_fiber(function(s)
        while auth_id and not token do
            local res = osxg.http_get(BASE_URL .. "/auth/check?auth_id=" .. auth_id, {})
            if res.status == 200 then
                local data = parse_json(res.body)
                if data and data.token then
                    save_token(data.token)
                    gui.show_success("OSXG+", "Discord Linked Successfully!")
                    auth_id = nil
                    -- Register our RID with the server now that we have a token
                    script.run_in_fiber(function(s)
                        local rid = 0
                        while rid == 0 do
                            rid = osxg.get_local_rockstar_id()
                            if rid ~= 0 then
                                local body = osxg.json_stringify({ rid = rid })
                                osxg.http_post(BASE_URL .. "/register?token=" .. token, {["Content-Type"]="application/json"}, body)
                                break
                            end
                            s:sleep(2000)
                        end
                    end)
                    return
                end
            elseif res.status ~= 202 then
                -- Error or timeout
                auth_id = nil
                return
            end
            s:sleep(2000) -- Poll every 2 seconds
        end
    end)
end

-- Host-side: Poll for pending invite requests from Discord button clicks
local function poll_pending_invites(rid)
    if not token or not rid then return end
    local res = osxg.http_get(BASE_URL .. "/pending_invites?token=" .. token .. "&rid=" .. tostring(rid), {})
    if res.status == 200 then
        local data = parse_json(res.body)
        if data and data.rids then
            for _, joiner_rid in ipairs(data.rids) do
                local joiner_rid_num = tonumber(joiner_rid)
                if joiner_rid_num and joiner_rid_num ~= 0 then
                    osxg.invite_by_rockstar_id(joiner_rid_num)
                    gui.show_message("OSXG+", "Sent invite to player " .. joiner_rid)
                end
            end
        end
    end
end

-- Joiner-side: auto-accept incoming Rockstar invites
local auto_accept_active = false
local function start_auto_accept()
    if auto_accept_active then return end
    auto_accept_active = true
    script.run_in_fiber(function(s)
        local timeout = os.time() + 120 -- wait up to 2 minutes
        while os.time() < timeout do
            if NETWORK.NETWORK_HAS_PENDING_INVITE() or NETWORK.NETWORK_SESSION_WAS_INVITED() then
                NETWORK.NETWORK_SESSION_JOIN_INVITE()
                gui.show_success("OSXG+", "Auto-accepted invite. Joining session...")
                break
            end
            s:sleep(500)
        end
        auto_accept_active = false
    end)
end

-- Background fiber: main loop
local function poll_invites()
    script.run_in_fiber(function(s)
        while true do
            if token then
                -- Check if we requested an invite from Discord (joiner-side)
                local res = osxg.http_get(BASE_URL .. "/invites/check?token=" .. token, {})
                if res.status == 200 then
                    local data = parse_json(res.body)
                    if data and data.status == "awaiting_invite" then
                        if data.hostRid then
                            gui.show_message("OSXG+", "Join request recognized. Joining Host " .. data.hostRid)
                            osxg.join_session_by_rockstar_id(tonumber(data.hostRid))
                            -- Also start auto-accept just in case direct join fails but invite arrives
                            start_auto_accept()
                        else
                            gui.show_message("OSXG+", "Join request recognized. Waiting for host to invite...")
                            start_auto_accept()
                        end
                    end
                end

                -- Heartbeat and session tracking logic
                if is_hosting then
                    local rid = osxg.get_local_rockstar_id()
                    if network.is_session_started() and rid ~= 0 then
                        -- Check for pending invites to dispatch (Fast poll: every 5s)
                        poll_pending_invites(rid)

                        -- Session heartbeat (Slow poll: every 60s)
                        if (os.time() - last_heartbeat > 60) then
                            local name = osxg.get_local_player_name()
                            local sessionInfo = osxg.get_local_session_info()
                            local body = osxg.json_stringify({
                                hostName = name,
                                rid = rid,
                                sessionType = "Public",
                                sessionInfo = sessionInfo
                            })
                            osxg.http_post(BASE_URL .. "/host?token=" .. token, {["Content-Type"]="application/json"}, body)
                            last_heartbeat = os.time()
                        end
                    elseif not network.is_session_started() then
                        local local_rid = osxg.get_local_rockstar_id()
                        if local_rid ~= 0 then
                            local body = osxg.json_stringify({ rid = local_rid })
                            osxg.http_post(BASE_URL .. "/unhost?token=" .. token, {["Content-Type"]="application/json"}, body)
                        end
                        is_hosting = false
                        gui.show_message("OSXG+", "Session unhosted (You left GTA Online)")
                    end
                end
            end
            s:sleep(5000) -- Poll every 5 seconds
        end
    end)
end

-- Initial load
load_token()
if token then
    -- Re-register our RID every startup (TTL refreshes every 7 days)
    script.run_in_fiber(function(s)
        local rid = 0
        local attempts = 0
        while rid == 0 and attempts < 30 do -- try for 60 seconds
            rid = osxg.get_local_rockstar_id()
            if rid ~= 0 then
                local body = osxg.json_stringify({ rid = rid })
                osxg.http_post(BASE_URL .. "/register?token=" .. token, {["Content-Type"]="application/json"}, body)
                break
            end
            attempts = attempts + 1
            s:sleep(2000)
        end
    end)
end
poll_invites()

-- UI Tab
local osxg_tab = gui.get_tab("GUI_TAB_NETWORK"):add_tab("OSXG+ Community")

osxg_tab:add_imgui(function()
    if not token then
        ImGui.Text("Discord account not linked.")
        if auth_id then
            ImGui.Text("Waiting for authorization...")
            if ImGui.Button("Cancel Login") then
                auth_id = nil
            end
        else
            if ImGui.Button("Link Discord via OSXG+") then
                auth_id = tostring(math.random(100000, 999999))
                local login_url = BASE_URL .. "/login?auth_id=" .. auth_id
                osxg.open_url(login_url)
                poll_auth()
            end
            ImGui.SameLine()
            if ImGui.Button("Copy Login URL") then
                auth_id = tostring(math.random(100000, 999999))
                ImGui.SetClipboardText(BASE_URL .. "/login?auth_id=" .. auth_id)
                poll_auth()
            end
        end
        return
    end

    -- Authenticated UI
    if ImGui.Button("Logout") then
        save_token("")
        token = nil
    end

    ImGui.Separator()

    if fetching_sessions then
        ImGui.Button("Refreshing...")
    else
        if ImGui.Button("Refresh Sessions") then
            fetching_sessions = true
            script.run_in_fiber(function(s)
                local res = osxg.http_get(BASE_URL .. "/sessions?token=" .. token, {})
                if res.status == 200 then
                    sessions = parse_json(res.body) or {}
                end
                last_session_fetch = os.time()
                fetching_sessions = false
            end)
        end
    end

    if not fetching_sessions and (os.time() - last_session_fetch > 30) then
        fetching_sessions = true
        script.run_in_fiber(function(s)
            local res = osxg.http_get(BASE_URL .. "/sessions?token=" .. token, {})
            if res.status == 200 then
                sessions = parse_json(res.body) or {}
            end
            last_session_fetch = os.time()
            fetching_sessions = false
        end)
    end

    ImGui.SameLine()

    if network.is_session_started() then
        if not is_hosting then
            if ImGui.Button("Share Current Session") then
                script.run_in_fiber(function(s)
                    local rid = osxg.get_local_rockstar_id()
                    
                    local name = "Unknown"
                    for i = 1, 15 do
                        name = osxg.get_local_player_name()
                        if name ~= "Unknown" and name ~= "" then break end
                        s:sleep(1000)
                    end
                    
                    local sessionInfo = osxg.get_local_session_info()
                    local body = osxg.json_stringify({
                        hostName = name,
                        rid = rid,
                        sessionType = "Public",
                        sessionInfo = sessionInfo
                    })
                    local res = osxg.http_post(BASE_URL .. "/host?token=" .. token, {["Content-Type"]="application/json"}, body)
                    if res.status == 200 then
                        gui.show_success("OSXG+", "Session Shared Successfully!")
                        is_hosting = true
                        last_heartbeat = os.time()
                    else
                        gui.show_error("OSXG+", "Failed to host: " .. tostring(res.status))
                    end
                end)
            end
        else
            if ImGui.Button("Stop Sharing Session") then
                script.run_in_fiber(function(s)
                    local rid = osxg.get_local_rockstar_id()
                    local body = osxg.json_stringify({ rid = rid })
                    osxg.http_post(BASE_URL .. "/unhost?token=" .. token, {["Content-Type"]="application/json"}, body)
                    is_hosting = false
                    gui.show_success("OSXG+", "Session is no longer shared.")
                end)
            end
        end
        ImGui.SameLine()
    end

    if ImGui.Button("Create & Host New Session") then
        script.run_in_fiber(function(s)
            gui.show_message("OSXG+", "Creating new Public Session...")
            osxg.create_public_session()
            
            -- Wait until the session actually starts
            while not network.is_session_started() do
                s:sleep(1000)
            end
            
            -- Extra safe delay to let local player spawn and RID assign
            s:sleep(10000)

            local rid = 0
            for i = 1, 15 do
                rid = osxg.get_local_rockstar_id()
                if rid ~= 0 then break end
                s:sleep(1000)
            end

            if rid == 0 then
                gui.show_error("OSXG+", "Failed to fetch Rockstar ID.")
                return
            end

            -- Retry fetching name if it says Unknown
            local name = "Unknown"
            for i = 1, 15 do
                name = osxg.get_local_player_name()
                if name ~= "Unknown" and name ~= "" then break end
                s:sleep(1000)
            end

            local sessionInfo = osxg.get_local_session_info()
            local body = osxg.json_stringify({
                hostName = name,
                rid = rid,
                sessionType = "Public",
                sessionInfo = sessionInfo
            })
            local res = osxg.http_post(BASE_URL .. "/host?token=" .. token, {["Content-Type"]="application/json"}, body)
            if res.status == 200 then
                gui.show_success("OSXG+", "New Session Hosted successfully!")
                is_hosting = true
                last_heartbeat = os.time()
            else
                gui.show_error("OSXG+", "Failed to host: " .. tostring(res.status))
            end
        end)
    end

    ImGui.SeparatorText("Active Sessions")

    if #sessions == 0 then
        ImGui.Text("No active OSXG sessions found.")
    else
        for _, session in ipairs(sessions) do
            ImGui.BeginGroup()
            ImGui.Text(string.format("%s (%s)", session.hostName, session.sessionType))
            ImGui.SameLine()
            if ImGui.Button("Join##" .. tostring(session.rid)) then
                osxg.join_session_by_rockstar_id(tonumber(session.rid))
            end
            ImGui.EndGroup()
        end
    end
end)
)lua";
}
