# OSXG

OSXG is a community extension built on top of YimMenu. It lets players share and join GTA V sessions through a Discord channel. The system has three parts: a Cloudflare Worker backend, a C++ Lua API layer, and a Lua UI script that runs inside YimMenu.

---

## Files

### src/osxg/osxg_api.hpp

Declares the `osxg::bind` function. This is the only public entry point of the C++ side. It is called once by `lua_module.cpp` when a Lua script is loaded, registering all OSXG functions into the Lua state under the `osxg` namespace.

---

### src/osxg/osxg_api.cpp

Implements every function exposed to Lua. All game calls are wrapped in `g_fiber_pool->queue_job` so they run on the GTA script thread rather than the Lua thread.

Functions registered:

| Lua name | What it does |
|---|---|
| `get_local_rockstar_id` | Returns the local player's Rockstar ID as a uint64. |
| `get_player_rockstar_id` | Returns another player's Rockstar ID by their in-game slot index. |
| `get_local_player_name` | Returns the local player's Social Club username. |
| `get_player_name` | Returns another player's username by slot index. |
| `get_local_session_info` | Encodes the current session into a base64 string using the game's internal encoder. Returns empty string if not in a session. |
| `join_session_by_rockstar_id` | Triggers YimMenu's native RID joiner for the given Rockstar ID. |
| `join_session_by_info` | Decodes a base64 session info string and attempts to join it directly. |
| `invite_by_rockstar_id` | Sends an official Rockstar game invite to the given Rockstar ID. The recipient sees a standard in-game invite popup. |
| `create_public_session` | Tells YimMenu to transition into a new public session. |
| `http_get` | Synchronous HTTP GET. Returns a table with `status` and `body`. |
| `http_post` | Synchronous HTTP POST with body. Returns a table with `status` and `body`. |
| `json_parse` | Parses a JSON string into a Lua table. |
| `json_stringify` | Converts a Lua table into a JSON string. |
| `open_url` | Opens a URL in the default browser. |

---

### src/osxg/osxg_menu_source.hpp

Contains the entire Lua script as a C++ string literal. It is written to disk as `osxg_menu.lua` on every YimMenu startup by `lua_manager.cpp`, which means the file is always overwritten with the version embedded in the DLL.

The script does the following:

**Token management**

The user authenticates once through Discord OAuth. The token is saved to `OSXG_Token.txt` and loaded on every startup. Without a token, all server endpoints return 401.

**RID registration**

On startup, if a token exists, the script calls `POST /register` with the local Rockstar ID. The server stores a mapping from Discord user ID to Rockstar ID. This is used so the server knows which Rockstar ID belongs to the player who clicks a Discord button.

**Hosting**

The user can click "Share Current Session" to upload their current session, or "Create and Host New Session" to first create a new public session via `osxg.create_public_session()` and then upload it. Both send `POST /host` with the host name, RID, session type label, and encoded session info.

A background fiber runs every 5 seconds. When `is_hosting` is true and a session is active, it sends a heartbeat to `/host` every 60 seconds to keep the session visible in the server's active list. It also calls `/pending_invites` at each heartbeat to check if any Discord users have requested to join.

**Invite dispatch**

When `/pending_invites` returns one or more Rockstar IDs, the script calls `osxg.invite_by_rockstar_id()` for each one. The joiner receives a standard GTA V invite popup and accepts it normally.

**Session list**

The UI fetches active sessions from `GET /sessions` and displays them. Each session has a Join button that calls `osxg.join_session_by_rockstar_id`.

---

### worker.js

The Cloudflare Worker. It handles all HTTP requests from the Lua script and all button interactions from Discord.

**KV namespaces required:**

| Binding | Purpose |
|---|---|
| `OSXG_TOKENS` | Maps tokens to Discord user IDs. Also stores OAuth state during login. |
| `OSXG_SESSIONS` | Active hosted sessions keyed by host RID. TTL 120 seconds. |
| `OSXG_RIDS` | Maps Discord user IDs to Rockstar IDs. TTL 7 days. Updated on every OSXG startup. |
| `OSXG_PENDING` | Pending invite queues keyed by host RID. Each entry is a JSON array of joiner RIDs. |

**Environment variables required:**

| Variable | Purpose |
|---|---|
| `BOT_TOKEN` | Discord bot token for sending messages and reading interactions. |
| `CHANNEL_ID` | Discord channel ID where session announcements are posted. |
| `DISCORD_PUBLIC_KEY` | Used to verify Ed25519 signatures on incoming interactions. |
| `CLIENT_ID` | Discord OAuth2 app client ID. |
| `CLIENT_SECRET` | Discord OAuth2 app client secret. |
| `REDIRECT_URI` | OAuth2 callback URL, must match what is set in the Discord developer portal. |
| `GUILD_ID` | Discord server ID. Only members of this server can authenticate. |

**Endpoints:**

| Method | Path | Description |
|---|---|---|
| POST | `/interactions` | Receives Discord button clicks. Validated via Ed25519 signature. Looks up the clicker's RID from `OSXG_RIDS` and adds it to the host's pending queue in `OSXG_PENDING`. |
| GET | `/login` | Redirects the user to Discord OAuth. |
| GET | `/callback` | Handles the OAuth code exchange, verifies guild membership, and stores the token. |
| GET | `/auth/check` | Lua polls this during login to detect when the token is ready. |
| POST | `/register` | Stores the caller's Rockstar ID against their Discord ID in `OSXG_RIDS`. |
| POST | `/host` | Creates or updates a session entry in `OSXG_SESSIONS`. On first registration, posts a Discord message with a "Request Invite" button. |
| POST | `/unhost` | Removes a session from `OSXG_SESSIONS`. |
| GET | `/sessions` | Returns all active sessions. |
| GET | `/pending_invites` | Returns and clears the pending invite queue for the calling host's RID. |

---

## Integration points in YimMenu

**src/lua/lua_manager.cpp**

Writes `osxg_menu.lua` to the scripts folder on every startup using the string from `osxg_menu_source.hpp`.

**src/lua/lua_module.cpp**

Calls `osxg::bind(state)` when loading each Lua module, making the `osxg` table available in every script.
