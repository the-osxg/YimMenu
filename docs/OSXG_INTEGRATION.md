# OSXG+ Integration

OSXG+ is a networked session browser and one-click join extension for YimMenu. It uses a Cloudflare Worker backend to facilitate communication between members and Discord.

## Architecture

### C++ Layer (`src/osxg/`)
- `osxg_api.cpp/hpp`: Implements the `osxg` Lua table.
    - `get_local_rockstar_id()`: Returns the local player's Rockstar ID.
    - `get_player_rockstar_id(pid)`: Returns the Rockstar ID of a given player.
    - `join_session_by_rockstar_id(rid)`: Joins a session by Rockstar ID (Rockstar ID).
    - `http_get/post`: Blocking HTTP client for Lua.
    - `json_parse/stringify`: JSON support for Lua.
    - `open_url(url)`: Opens a URL in the default browser.
    - `get_player_name(pid)`: Returns the name of a player.

### Lua Layer (`scripts/osxg/`)
- `osxg_menu.lua`: Implements the UI and high-level logic.
    - Discord OAuth flow.
    - Session hosting (advertising to the Worker).
    - Session browsing (fetching from the Worker).
    - Polling for Discord invites (one-click join).

## Integration Hooks

To maintain compatibility with upstream YimMenu, the modification is isolated to a few hooks:

1.  **Lua Registration**: `src/lua/lua_module.cpp`
    - Included `osxg/osxg_api.hpp`.
    - Called `osxg::bind(m_state)` in `init_lua_api`.
    - Marked with `// OSXG hook BEGIN/END`.

2.  **UI Entry Point**:
    - The `osxg_menu.lua` script automatically registers an "OSXG+ Community" sub-tab under "Network" using `gui.get_tab("Network"):add_tab(...)`.

## Files Added
- `src/osxg/osxg_api.hpp`
- `src/osxg/osxg_api.cpp`
- `scripts/osxg/osxg_menu.lua`
- `docs/OSXG_INTEGRATION.md` (this file)
