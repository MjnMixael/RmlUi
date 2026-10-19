#include "../Common/TestsShell.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Lua/IncludeLua.h>
#include <RmlUi/Lua/Interpreter.h>
#include <RmlUi/Lua/Lua.h>
#include <RmlUi/Lua/Utilities.h>
#include <cstring>
#include <doctest.h>

// The tests below call the Lua C API from inside the Rml::Lua namespace, the same way the Lua plugin does. That way
// they resolve to the plugin's compatibility fallbacks when building against plain Lua 5.1, and to the functions
// provided by Lua itself otherwise, so the same expectations are checked against both.
namespace Rml {
namespace Lua {
	namespace {

		struct LuaState {
			LuaState() : L(luaL_newstate()) { luaL_openlibs(L); }
			~LuaState() { lua_close(L); }
			lua_State* L;
		};

		int ReturnFirstUpvalue(lua_State* L)
		{
			lua_pushvalue(L, lua_upvalueindex(1));
			return 1;
		}

		int ReturnSecondUpvalue(lua_State* L)
		{
			lua_pushvalue(L, lua_upvalueindex(2));
			return 1;
		}

		int ReturnOne(lua_State* L)
		{
			lua_pushinteger(L, 1);
			return 1;
		}

		int ReturnTwo(lua_State* L)
		{
			lua_pushinteger(L, 2);
			return 1;
		}

		int TracebackHandler(lua_State* L)
		{
			luaL_traceback(L, L, lua_tostring(L, 1), 1);
			return 1;
		}

		lua_Integer RunInteger(lua_State* L, const char* code)
		{
			REQUIRE(luaL_loadstring(L, code) == LUA_OK);
			REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);
			const lua_Integer result = lua_tointeger(L, -1);
			lua_pop(L, 1);
			return result;
		}

		bool Contains(const String& haystack, const char* needle)
		{
			return haystack.find(needle) != String::npos;
		}

		TEST_CASE("lua.c_api.setfuncs")
		{
			LuaState state;
			lua_State* L = state.L;

			SUBCASE("without_upvalues")
			{
				const luaL_Reg functions[] = {{"one", ReturnOne}, {"two", ReturnTwo}, {nullptr, nullptr}};
				lua_newtable(L);
				const int top = lua_gettop(L);
				luaL_setfuncs(L, functions, 0);
				CHECK(lua_gettop(L) == top);
				lua_setglobal(L, "t");

				CHECK(RunInteger(L, "return t.one()") == 1);
				CHECK(RunInteger(L, "return t.two()") == 2);
			}

			SUBCASE("with_upvalues")
			{
				const luaL_Reg functions[] = {{"first", ReturnFirstUpvalue}, {"second", ReturnSecondUpvalue}, {nullptr, nullptr}};
				lua_newtable(L);
				const int top = lua_gettop(L);
				lua_pushinteger(L, 10);
				lua_pushinteger(L, 20);
				luaL_setfuncs(L, functions, 2);
				// The upvalues are popped, leaving only the table.
				CHECK(lua_gettop(L) == top);
				lua_setglobal(L, "t");

				// Every function gets all the upvalues, in the order they were pushed.
				CHECK(RunInteger(L, "return t.first()") == 10);
				CHECK(RunInteger(L, "return t.second()") == 20);
			}
		}

		TEST_CASE("lua.c_api.testudata")
		{
			LuaState state;
			lua_State* L = state.L;

			luaL_newmetatable(L, "TestType");
			lua_pop(L, 1);
			luaL_newmetatable(L, "OtherType");
			lua_pop(L, 1);

			void* userdata = lua_newuserdata(L, sizeof(int));
			luaL_getmetatable(L, "TestType");
			lua_setmetatable(L, -2);
			const int top = lua_gettop(L);

			CHECK(luaL_testudata(L, -1, "TestType") == userdata);
			CHECK(lua_gettop(L) == top);

			CHECK(luaL_testudata(L, -1, "OtherType") == nullptr);
			CHECK(lua_gettop(L) == top);

			CHECK(luaL_testudata(L, -1, "UnregisteredType") == nullptr);
			CHECK(lua_gettop(L) == top);

			// Userdata without a metatable.
			lua_newuserdata(L, sizeof(int));
			CHECK(luaL_testudata(L, -1, "TestType") == nullptr);
			lua_pop(L, 1);

			// Values that are not userdata.
			lua_pushinteger(L, 5);
			CHECK(luaL_testudata(L, -1, "TestType") == nullptr);
			lua_pop(L, 1);

			lua_newtable(L);
			luaL_getmetatable(L, "TestType");
			lua_setmetatable(L, -2);
			CHECK(luaL_testudata(L, -1, "TestType") == nullptr);
			lua_pop(L, 1);

			CHECK(lua_gettop(L) == top);
		}

		TEST_CASE("lua.c_api.tointegerx")
		{
			LuaState state;
			lua_State* L = state.L;
			int is_number = -1;

			lua_pushinteger(L, 42);
			CHECK(lua_tointegerx(L, -1, &is_number) == 42);
			CHECK(is_number != 0);

			// Zero is a number, even though lua_tointeger returns zero for non-numbers too.
			is_number = -1;
			lua_pushinteger(L, 0);
			CHECK(lua_tointegerx(L, -1, &is_number) == 0);
			CHECK(is_number != 0);

			is_number = -1;
			lua_pushstring(L, "12");
			CHECK(lua_tointegerx(L, -1, &is_number) == 12);
			CHECK(is_number != 0);

			is_number = -1;
			lua_pushstring(L, "abc");
			CHECK(lua_tointegerx(L, -1, &is_number) == 0);
			CHECK(is_number == 0);

			is_number = -1;
			lua_pushnil(L);
			CHECK(lua_tointegerx(L, -1, &is_number) == 0);
			CHECK(is_number == 0);

			// The output parameter is optional.
			lua_pushinteger(L, 7);
			CHECK(lua_tointegerx(L, -1, nullptr) == 7);
		}

		TEST_CASE("lua.c_api.traceback")
		{
			LuaState state;
			lua_State* L = state.L;

			SUBCASE("from_error_handler")
			{
				const char* script = "function inner() error('boom') end\n"
									 "function middle() inner() end\n"
									 "function outer() middle() end\n";

				lua_pushcfunction(L, TracebackHandler);
				const int handler = lua_gettop(L);

				REQUIRE(luaL_loadbuffer(L, script, strlen(script), "=test") == LUA_OK);
				REQUIRE(lua_pcall(L, 0, 0, handler) == LUA_OK);

				lua_getglobal(L, "outer");
				REQUIRE(lua_pcall(L, 0, 1, handler) != LUA_OK);
				const String traceback = lua_tostring(L, -1);
				INFO(traceback);

				// The message comes first, followed by one line per stack frame.
				CHECK(traceback.find("test:1: boom\nstack traceback:") == 0);
				CHECK(Contains(traceback, "test:1:"));
				CHECK(Contains(traceback, "test:2:"));
				CHECK(Contains(traceback, "test:3:"));
				// Newer Lua versions may write global functions as '_G.inner', so only check the end of each name. The
				// outermost function is called from C, so whether its name is known differs between Lua versions.
				CHECK(Contains(traceback, "inner'"));
				CHECK(Contains(traceback, "middle'"));
			}

			SUBCASE("tail_call")
			{
				const char* script = "function inner() error('boom') end\n"
									 "function tail() return inner() end\n"
									 "function outer() tail() end\n";

				lua_pushcfunction(L, TracebackHandler);
				const int handler = lua_gettop(L);

				REQUIRE(luaL_loadbuffer(L, script, strlen(script), "=test") == LUA_OK);
				REQUIRE(lua_pcall(L, 0, 0, handler) == LUA_OK);

				lua_getglobal(L, "outer");
				REQUIRE(lua_pcall(L, 0, 1, handler) != LUA_OK);
				const String traceback = lua_tostring(L, -1);
				INFO(traceback);

				// Each Lua version writes the frame lost to the tail call differently, but never as a function with
				// a source location, and the frames below it are still listed.
				CHECK_FALSE(Contains(traceback, "<(tail call)"));
				CHECK(Contains(traceback, "test:3:"));
#ifdef RMLUI_LUA_51_COMPAT
				// Lua 5.1 writes it like a C function.
				CHECK(Contains(traceback, "(tail call): ?"));
#endif
			}

			SUBCASE("without_message")
			{
				luaL_traceback(L, L, nullptr, 0);
				const String traceback = lua_tostring(L, -1);
				INFO(traceback);
				CHECK(traceback.find("stack traceback:") == 0);
			}
		}

		TEST_CASE("lua.c_api.lua_ok")
		{
			LuaState state;
			lua_State* L = state.L;

			REQUIRE(luaL_loadstring(L, "local x = 1") == LUA_OK);
			CHECK(lua_pcall(L, 0, 0, 0) == LUA_OK);
			CHECK(LUA_OK == 0);
		}

	} // namespace
} // namespace Lua
} // namespace Rml

using namespace Rml;

static const String lua_data_model_rml = R"(
<rml>
<head>
	<title>Test</title>
	<link type="text/rcss" href="/assets/rml.rcss"/>
	<style>
		body { font-family: LatoLatin; }
	</style>
</head>
<body data-model="lua_test">
	<p id="title">{{ title }}</p>
	<p id="list"><span data-for="items">{{ it }}</span></p>
</body>
</rml>
)";

TEST_CASE("lua.plugin")
{
	// Start from a fresh shell so that the Lua plugin is set up before RmlUi is initialized.
	TestsShell::ShutdownShell();

	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	Rml::Lua::Initialise(L);

	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	SUBCASE("script_errors")
	{
		CHECK(Rml::Lua::Interpreter::DoString("local x = 1", "test"));

		// A runtime error is reported through the plugin's error handler, which adds a stack traceback.
		TestsShell::SetNumExpectedWarnings(1);
		CHECK_FALSE(Rml::Lua::Interpreter::DoString("error('boom')", "test"));
		TestsShell::SetNumExpectedWarnings(0);
	}

	SUBCASE("data_model")
	{
		REQUIRE(Rml::Lua::Interpreter::DoString(R"(
			lua_test_model = rmlui.contexts["main"]:OpenDataModel("lua_test", {
				title = "Lua title",
				items = { "a", "b", "c" },
			})
		)",
			"test"));

		ElementDocument* document = context->LoadDocumentFromMemory(lua_data_model_rml);
		REQUIRE(document);
		document->Show();
		TestsShell::RenderLoop();

		CHECK(document->GetElementById("title")->GetInnerRML() == "Lua title");

		// The data-for view inserts one element per array entry before the hidden template element.
		Element* list = document->GetElementById("list");
		REQUIRE(list->GetNumChildren() == 4);
		CHECK(list->GetChild(0)->GetInnerRML() == "a");
		CHECK(list->GetChild(1)->GetInnerRML() == "b");
		CHECK(list->GetChild(2)->GetInnerRML() == "c");

		// Writing to the model from Lua updates the document.
		REQUIRE(Rml::Lua::Interpreter::DoString("lua_test_model.title = 'Changed'", "test"));
		TestsShell::RenderLoop();
		CHECK(document->GetElementById("title")->GetInnerRML() == "Changed");

		document->Close();
		TestsShell::RenderLoop();
		REQUIRE(Rml::Lua::Interpreter::DoString("rmlui.contexts['main']:CloseDataModel('lua_test')", "test"));
	}

	TestsShell::ShutdownShell();
	lua_close(L);
}
