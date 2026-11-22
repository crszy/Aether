/* hyprlua.exe <hyprland.lua> - runs a Hyprland Lua config against prelude.lua (next to this exe) and prints
 * what it asked for as JSON on stdout. Used by Caelestia for Windows (services/HyprConf.qml).
 * Lua 5.5.1 (MIT, lua.org) is compiled in. The config's own folder is on package.path, so require("x") works
 * like in Hyprland. os.execute / io.popen are removed: a config is read, never run as a program here. */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: hyprlua <hyprland.lua>\n"); return 2; }

    char self[MAX_PATH]; GetModuleFileNameA(NULL, self, MAX_PATH);
    char* slash = strrchr(self, '\\'); if (slash) slash[1] = 0;
    char prelude[MAX_PATH + 32]; snprintf(prelude, sizeof prelude, "%sprelude.lua", self);

    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    /* no programs from a config file */
    lua_getglobal(L, "os"); lua_pushnil(L); lua_setfield(L, -2, "execute"); lua_pushnil(L); lua_setfield(L, -2, "remove");
    lua_pushnil(L); lua_setfield(L, -2, "rename"); lua_pushnil(L); lua_setfield(L, -2, "exit"); lua_pop(L, 1);
    lua_getglobal(L, "io"); lua_pushnil(L); lua_setfield(L, -2, "popen"); lua_pop(L, 1);

    /* require() looks next to the config */
    char dir[MAX_PATH * 2]; strncpy(dir, argv[1], sizeof dir - 1); dir[sizeof dir - 1] = 0;
    for (char* p = dir; *p; p++) if (*p == '\\') *p = '/';
    char* cut = strrchr(dir, '/'); if (cut) *cut = 0; else strcpy(dir, ".");
    lua_getglobal(L, "package");
    lua_pushfstring(L, "%s/?.lua;%s/?/init.lua", dir, dir);
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);

    if (luaL_dofile(L, prelude) != LUA_OK) {
        fprintf(stderr, "hyprlua: prelude: %s\n", lua_tostring(L, -1));
        return 3;
    }

    int rc = luaL_dofile(L, argv[1]);
    lua_getglobal(L, "__hl_dump");
    if (rc != LUA_OK) lua_pushvalue(L, -2); else lua_pushnil(L);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) { fprintf(stderr, "hyprlua: dump: %s\n", lua_tostring(L, -1)); return 4; }
    lua_close(L);
    return rc == LUA_OK ? 0 : 1;
}
