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

// Inside Rml::Lua, so that Lua 5.1 builds call the plugin's compatibility fallbacks.
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
				CHECK(lua_gettop(L) == top);
				lua_setglobal(L, "t");

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

				CHECK(traceback.find("test:1: boom\nstack traceback:") == 0);
				CHECK(Contains(traceback, "test:1:"));
				CHECK(Contains(traceback, "test:2:"));
				CHECK(Contains(traceback, "test:3:"));
				// Newer versions may write '_G.inner', and the name of the outermost function varies between versions.
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

				CHECK_FALSE(Contains(traceback, "<(tail call)"));
				CHECK(Contains(traceback, "test:3:"));
#ifdef RMLUI_LUA_51_COMPAT
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
	// The Lua plugin must be set up before RmlUi is initialized.
	TestsShell::ShutdownShell();

	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	Lua::Initialise(L);

	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	SUBCASE("script_errors")
	{
		CHECK(Lua::Interpreter::DoString("local x = 1", "test"));

		// A runtime error is logged as a warning.
		TestsShell::SetNumExpectedWarnings(1);
		CHECK_FALSE(Lua::Interpreter::DoString("error('boom')", "test"));
		TestsShell::SetNumExpectedWarnings(0);
	}

	SUBCASE("data_model")
	{
		REQUIRE(Lua::Interpreter::DoString(R"(
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

		// One element per entry, followed by the hidden template element.
		Element* list = document->GetElementById("list");
		REQUIRE(list->GetNumChildren() == 4);
		CHECK(list->GetChild(0)->GetInnerRML() == "a");
		CHECK(list->GetChild(1)->GetInnerRML() == "b");
		CHECK(list->GetChild(2)->GetInnerRML() == "c");

		REQUIRE(Lua::Interpreter::DoString("lua_test_model.title = 'Changed'", "test"));
		TestsShell::RenderLoop();
		CHECK(document->GetElementById("title")->GetInnerRML() == "Changed");

		document->Close();
		TestsShell::RenderLoop();
		REQUIRE(Lua::Interpreter::DoString("rmlui.contexts['main']:CloseDataModel('lua_test')", "test"));
	}

	TestsShell::ShutdownShell();
	lua_close(L);
}

static const String lua_iteration_rml = R"(
<rml>
<head>
	<title>Test</title>
	<link type="text/rcss" href="/assets/rml.rcss"/>
	<style>
		body { font-family: LatoLatin; }
	</style>
</head>
<body id="iteration_doc">
	<div id="parent" class="c" title="t" style="width: 10px;"><p>a</p><p>b</p><p>c</p></div>
	<select id="sel"><option value="1">One</option><option value="2">Two</option></select>
</body>
</rml>
)";

// Runs a loop that increments the global 'count' and returns the final count, or -1 if the script failed.
static int CountIterations(lua_State* L, const char* loop)
{
	const String code = "local doc = rmlui.contexts['main'].documents['iteration_doc']\n"
						"local parent = doc:GetElementById('parent')\n"
						"local options = Element.As.ElementFormControlSelect(doc:GetElementById('sel')).options\n"
						"count = 0\n" +
		String(loop);
	if (!Lua::Interpreter::DoString(code, "iteration"))
		return -1;
	lua_getglobal(L, "count");
	const int count = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);
	return count;
}

TEST_CASE("lua.iteration")
{
	TestsShell::ShutdownShell();

	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	Lua::Initialise(L);

	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	ElementDocument* document = context->LoadDocumentFromMemory(lua_iteration_rml);
	REQUIRE(document);
	document->Show();
	TestsShell::RenderLoop();

	SUBCASE("ipairs_child_nodes")
	{
		CHECK(CountIterations(L, "for _, child in ipairs(parent.child_nodes) do count = count + 1 end") == 3);
	}
	SUBCASE("pairs_child_nodes")
	{
		CHECK(CountIterations(L, "for _, child in pairs(parent.child_nodes) do count = count + 1 end") == 3);
	}
	SUBCASE("ipairs_select_options")
	{
		CHECK(CountIterations(L, "for _, option in ipairs(options) do count = count + 1 end") == 2);
	}
	SUBCASE("pairs_select_options")
	{
		CHECK(CountIterations(L, "for _, option in pairs(options) do count = count + 1 end") == 2);
	}
	SUBCASE("ipairs_contexts")
	{
		CHECK(CountIterations(L, "for _, c in ipairs(rmlui.contexts) do count = count + 1 end") == 1);
	}
	SUBCASE("pairs_contexts")
	{
		CHECK(CountIterations(L, "for _, c in pairs(rmlui.contexts) do count = count + 1 end") == 1);
	}
	SUBCASE("pairs_documents")
	{
		CHECK(CountIterations(L, "for _, d in pairs(doc.context.documents) do count = count + 1 end") == context->GetNumDocuments());
	}
	SUBCASE("pairs_attributes")
	{
		CHECK(CountIterations(L, "for name, value in pairs(parent.attributes) do count = count + 1 end") == 4);
	}
	SUBCASE("pairs_style")
	{
		CHECK(CountIterations(L, "for name, value in pairs(parent.style) do if name == 'width' then count = count + 1 end end") == 1);
	}

	document->Close();
	TestsShell::ShutdownShell();
	lua_close(L);
}
