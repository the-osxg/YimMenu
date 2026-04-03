# YimMenu AI Agent Guide & Project Analysis

This guide is designed for AI agents to quickly understand the YimMenu codebase and address common technical challenges, particularly in networking and matchmaking.

## 🏗️ Architecture Overview

YimMenu is a GTA V internal mod menu built with a focus on modularity and performance.

| Component | Responsibility | Key Files |
| :--- | :--- | :--- |
| **Core** | Initialization, Settings, Hooking Engine, Logger | `src/core/`, `src/main.cpp` |
| **Hooks** | Native function redirections (R* API interception) | `src/hooks/` |
| **Services** | Business logic (Matchmaking, Players, Messaging) | `src/services/` |
| **GUI** | ImGui-based user interface | `src/views/`, `src/gui/` |
| **Fiber Pool** | Asynchronous execution within the game thread | `src/core/fiber_pool.hpp` |

---

## 📡 Networking & Matchmaking

### `matchmaking_service`
Located in `src/services/matchmaking/`, this service handles session discovery and advertisement spoofing.

- **Multiplexing Logic**: The `multiplex_session` feature attempts to advertise the same session multiple times to increase visibility or "fill" the session browser.
- **Sequential Advertising**: Multiplexed advertisements are created sequentially using the `fiber_pool` to avoid blocking the main thread and to handle Rockstar's task status (`rage::rlTaskStatus`).

#### Key Native Hooks
- `advertise_session`: The primary hook where multiplexing is triggered.
- `update_session_advertisement`: Keeps all multiplexed instances in sync.
- `unadvertise_session`: Cleans up all advertisements when leaving a session.

### Common Problems: "Multiplex Not Working"
If session advertisements fail (warnings in log: `advertise_session failed`), check:
1. **Fiber Pool Saturation**: If too many jobs are queued, timeouts might occur.
2. **Race Conditions**: `m_multiplexed_sessions` map access during high-frequency updates.
3. **Task Status Polling**: The loop `while (status.status == 1) script::get_current()->yield();` is critical for waiting on Rockstar's backend.

---

## 🗺️ Project Map (Key Directories)

- `src/hooks/`: Contains subdirectories for specific categories (matchmaking, network, script, etc.).
- `src/services/`: Each service typically has a header and implementation (e.g., `matchmaking_service.cpp`).
- `src/core/settings.hpp`: Contains the global `g` settings object which controls all features.
- `src/views/`: ImGui windows and components.
- `worker.js`: Cloudflare Worker for Discord integration (OSXG+).

---

## 🛠️ Debugging for AI Agents

1. **Log Analysis**: Search for `LOG(WARNING)` or `LOG(FATAL)` in the codebase to find error-prone areas.
2. **Settings Check**: Many features are gated by `g.spoofing.*` or `g.session_browser.*` flags.
3. **Fiber Context**: Ensure any function calling `script::get_current()->yield()` is running inside a fiber.

---

> [!TIP]
> When investigating networking issues, always look at the interactions between `matchmaking_service.cpp` and the hooks in `src/hooks/matchmaking/`.

> [!IMPORTANT]
> The project uses `rage` namespace for many Rockstar-internal classes. Refer to `src/ragxxx/` directories if available for reverse-engineered structures.
