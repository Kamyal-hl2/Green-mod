//========= Green Engine v2 - Lua VM bootstrap (MVP, Part 1.4 step 1-2) =====
//
// Boots a LuaJIT state and provides the two primitives GameFrame() needs:
//   - LuaVM_Bootstrap()      : create the VM, register the tiny set of
//                              global helpers hook.lua depends on, load
//                              hook.lua itself.
//   - LuaVM_RunHooks(name)   : call the Lua-side hook.Run(name) — all hook
//                              bookkeeping (the Hooks table) lives in Lua,
//                              NOT here. Do not re-add a C++ hook table.
//
// Deliberately does not yet implement ents.*, util.*, Vector/Angle userdata,
// etc. Those are separate MVP steps (1.4.3 onward) and go in their own
// lua_ents.cpp / lua_math.cpp files added to server_garrysmod.vpc the same
// way this file is.
//=============================================================================
#ifndef LUA_VM_H
#define LUA_VM_H
#ifdef _WIN32
#pragma once
#endif

struct lua_State;

// Global VM handle. Server-side only for the MVP; a client-side instance
// (for client hooks / menu state) is a later step, not this one.
extern lua_State *g_pLuaVM;

// Called once from CServerGameDLL::DLLInit(), after engine interfaces are
// connected but before any game/map scripts are loaded. Creates lua_State,
// opens the standard libs, registers isfunction/isstring/isnumber/isbool/
// ErrorNoHaltWithStack, and loads garrysmod/lua/includes/modules/hook.lua.
// Returns false (and logs why) if any step fails; callers should treat that
// as a fatal DLLInit error the same way missing engine interfaces are.
bool LuaVM_Bootstrap();

// Called once from CServerGameDLL::PostInit(), after IGameSystem::PostInit
// has run. This is where the gamemode entry point and autorun scripts get
// loaded (gamemodes/sandbox/gamemode/init.lua and its include() chain).
// Stubbed for now — real implementation lands with ents.* (step 1.4.3),
// since the gamemode's init.lua immediately calls ents.* on load.
void LuaVM_LoadGameScripts();

// Called from CServerGameDLL::GameFrame() once per simulated server tick,
// right after Physics_RunThinkFunctions(). Thin wrapper around the Lua
// global hook.Run(name) — errors are caught with lua_pcall and logged via
// Warning(), never allowed to propagate and crash the engine.
void LuaVM_RunHooks(const char *hookName);

// Called from CServerGameDLL::DLLShutdown(). Closes the Lua state.
void LuaVM_Shutdown();

// Loads and executes a single Lua file addressed relative to the mod's
// content root (i.e. the same rooting used by the engine's IFileSystem,
// "GAME" search path — so "lua/includes/modules/hook.lua", not an absolute
// OS path). Returns false and logs the Lua error on failure. Exposed here
// because LuaVM_LoadGameScripts() and the future include()/AddCSLuaFile
// bindings both need it.
bool LuaVM_DoFile(const char *relativePath);

#endif // LUA_VM_H
