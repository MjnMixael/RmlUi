#include "LuaDocument.h"
#include "InterpreterInternal.h"
#include "LuaPlugin.h"
#include <RmlUi/Core/Stream.h>
#include <RmlUi/Lua/IncludeLua.h>
#include <RmlUi/Lua/Interpreter.h>
#include <RmlUi/Lua/Utilities.h>
#include <string.h>

namespace Rml {
namespace Lua {

RMLUI_RTTI_Define(LuaDocument)

LuaDocument::LuaDocument(const String& tag) : ElementDocument(tag) {}

LuaDocument::~LuaDocument()
{
	lua_State* L = Interpreter::GetLuaState();
	if (L && environment_ref != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, environment_ref);
}

void LuaDocument::LoadInlineScript(const String& context, const String& source_path, int source_line)
{
	String buffer;
	buffer += "--";
	buffer += source_path;
	buffer += ":";
	buffer += Rml::ToString(source_line);
	buffer += "\n";
	buffer += context;
	if (Interpreter::LoadString(buffer, buffer))
	{
		ApplyEnvironment(Interpreter::GetLuaState(), -1, true);
		Interpreter::ExecuteCall(0, 0);
	}
}

void LuaDocument::LoadExternalScript(const String& source_path)
{
	lua_State* L = Interpreter::GetLuaState();
	if (LoadFileChunk(L, source_path))
	{
		ApplyEnvironment(L, -1, true);
		Interpreter::ExecuteCall(0, 0);
	}
}

void LuaDocument::ApplyEnvironment(lua_State* L, int function_index, bool is_chunk)
{
	if (!LuaPlugin::GetOptions().document_environments)
		return;

	function_index = lua_absindex(L, function_index);

	if (environment_ref == LUA_NOREF)
	{
		// The environment is a new table that looks up missing names in the global table.
		lua_newtable(L);
		lua_newtable(L);
#if LUA_VERSION_NUM < 502
		lua_pushvalue(L, LUA_GLOBALSINDEX);
#else
		lua_pushglobaltable(L);
#endif
		lua_setfield(L, -2, "__index");
		lua_setmetatable(L, -2);
		environment_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	lua_rawgeti(L, LUA_REGISTRYINDEX, environment_ref);
#if LUA_VERSION_NUM < 502
	(void)is_chunk;
	lua_setfenv(L, function_index);
#else
	// The environment is the function's '_ENV' upvalue. A loaded chunk always has it as its first upvalue, even when
	// compiled without debug information. Other functions are searched by name, and have none if they don't use any
	// global names.
	if (is_chunk)
	{
		lua_setupvalue(L, function_index, 1);
		return;
	}
	for (int i = 1;; i++)
	{
		const char* name = lua_getupvalue(L, function_index, i);
		if (!name)
			break;
		lua_pop(L, 1);
		if (strcmp(name, "_ENV") == 0)
		{
			lua_pushvalue(L, -1);
			lua_setupvalue(L, function_index, i);
			break;
		}
	}
	lua_pop(L, 1);
#endif
}

} // namespace Lua
} // namespace Rml
