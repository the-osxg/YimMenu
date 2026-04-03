-- OSXG+ Community Extension for YimMenu
-- BASE_URL should be set to your Cloudflare Worker URL
local BASE_URL = "https://osxg-auth.1221647.workers.dev"

local token = nil
local auth_id = nil
local sessions = {}
local last_session_fetch = 0
local fetching_sessions = false

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

-- Polling for Discord Invites (Discord One-Click Join)
local function poll_invites()
    script.run_in_fiber(function(s)
        while true do
            if token then
                local res = osxg.http_get(BASE_URL .. "/invites/check?token=" .. token, {})
                if res.status == 200 then
                    local data = parse_json(res.body)
                    if data and data.rid then
                        gui.show_message("OSXG+", "Joining session from Discord...")
                        osxg.join_session_by_rockstar_id(tonumber(data.rid))
                    end
                end
            end
            s:sleep(5000) -- Poll every 5 seconds
        end
    end)
end

-- Initial load
load_token()
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

    if not fetching_sessions and (ImGui.Button("Refresh Sessions") or (os.time() - last_session_fetch > 30)) then
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
        if ImGui.Button("Share Current Session") then
            script.run_in_fiber(function(s)
                local rid = osxg.get_local_rockstar_id()
                
                -- Retry fetching name if it says Unknown
                local name = "Unknown"
                for i = 1, 15 do
                    name = osxg.get_local_player_name()
                    if name ~= "Unknown" and name ~= "" then break end
                    s:sleep(1000)
                end
                
                local body = osxg.json_stringify({
                    hostName = name,
                    rid = rid,
                    sessionType = "Public"
                })
                local res = osxg.http_post(BASE_URL .. "/host?token=" .. token, {["Content-Type"]="application/json"}, body)
                if res.status == 200 then
                    gui.show_success("OSXG+", "Session Shared Successfully!")
                else
                    gui.show_error("OSXG+", "Failed to host: " .. tostring(res.status))
                end
            end)
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

            local body = osxg.json_stringify({
                hostName = name,
                rid = rid,
                sessionType = "Public"
            })
            local res = osxg.http_post(BASE_URL .. "/host?token=" .. token, {["Content-Type"]="application/json"}, body)
            if res.status == 200 then
                gui.show_success("OSXG+", "New Session Hosted successfully!")
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
