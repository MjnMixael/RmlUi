#include "../Common/TestsInterface.h"
#include "../Common/TestsShell.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Lua/IncludeLua.h>
#include <RmlUi/Lua/Interpreter.h>
#include <RmlUi/Lua/Lua.h>
#include <doctest.h>

using namespace Rml;

static bool Contains(const String& haystack, const char* needle)
{
	return haystack.find(needle) != String::npos;
}

static const String lua_environment_rml = R"RML(
<rml>
<head>
	<title>Test</title>
	<link type="text/rcss" href="/assets/rml.rcss"/>
	<style>
		body { font-family: LatoLatin; }
	</style>
	<script>
		name = '{name}'
		function GetName() return name end

		-- Track this script's environment in a weak table, to check that it's freed with the document.
		local environment = getfenv and getfenv(1) or _ENV
		_G.environments = _G.environments or setmetatable({}, { __mode = 'k' })
		_G.environments[environment] = true
	</script>
	<script src="/../Tests/Data/UnitTests/lua_environment.lua"/>
</head>
<body>
	<div id="button" onclick="_G.clicked = GetName() .. ':' .. string.upper(name) .. ':' .. GetFileName()"/>
	<div id="no_globals" onclick="local x = 1"/>
</body>
</rml>
)RML";

// Returns the environment test document, with the script's 'name' set to the given value.
static String EnvironmentRml(const char* name)
{
	String rml = lua_environment_rml;
	const size_t position = rml.find("{name}");
	rml.replace(position, 6, name);
	return rml;
}

static String GetGlobalString(lua_State* L, const char* name)
{
	lua_getglobal(L, name);
	const char* value = lua_tostring(L, -1);
	const String result = value ? value : "nil";
	lua_pop(L, 1);
	return result;
}

static String CountEnvironments(lua_State* L)
{
	REQUIRE(Lua::Interpreter::DoString("collectgarbage() collectgarbage() "
									   "environment_count = 0 "
									   "for _ in pairs(environments) do environment_count = environment_count + 1 end",
		"count"));
	return GetGlobalString(L, "environment_count");
}

// Loads two documents that each set the global 'name' and read it back from an inline event handler, through functions
// defined in an inline and an external script. Returns what each document's handler saw, the value of 'name' in the
// shared global table, and the number of distinct script environments before and after the documents are closed.
static String RunDocumentEnvironments(const Lua::Options& options)
{
	TestsShell::ShutdownShell();

	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	Lua::Initialise(L, options);

	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	ElementDocument* one = context->LoadDocumentFromMemory(EnvironmentRml("one"));
	ElementDocument* two = context->LoadDocumentFromMemory(EnvironmentRml("two"));
	REQUIRE(one);
	REQUIRE(two);
	one->Show();
	two->Show();
	TestsShell::RenderLoop();

	one->GetElementById("button")->Click();
	const String clicked_one = GetGlobalString(L, "clicked");
	two->GetElementById("button")->Click();
	const String clicked_two = GetGlobalString(L, "clicked");
	const String global_name = GetGlobalString(L, "name");

	// A handler that uses no global names has no environment to set on Lua 5.2 and later.
	one->GetElementById("no_globals")->Click();

	const String environments_open = CountEnvironments(L);
	one->Close();
	two->Close();
	TestsShell::RenderLoop();
	const String environments_closed = CountEnvironments(L);

	TestsShell::ShutdownShell();
	lua_close(L);

	return clicked_one + "," + clicked_two + "," + global_name + "," + environments_open + "," + environments_closed;
}

TEST_CASE("lua.options.document_environments")
{
	SUBCASE("disabled")
	{
		// All documents share the global table, so the second document overwrites the first one's 'name'.
		CHECK(RunDocumentEnvironments(Lua::Options()) == "two:TWO:two+file,two:TWO:two+file,two,1,1");
	}
	SUBCASE("enabled")
	{
		Lua::Options options;
		options.document_environments = true;
		CHECK(RunDocumentEnvironments(options) == "one:ONE:one+file,two:TWO:two+file,nil,2,0");
	}
}

static int error_handler_calls = 0;
static String error_handler_message;
static String error_handler_lines;

static int RecordError(lua_State* L)
{
	error_handler_calls++;
	const char* message = lua_tostring(L, 1);
	error_handler_message = message ? message : "";

	// Record the current line of each Lua function on the call stack, to check that the handler runs at the point of
	// the error.
	error_handler_lines.clear();
	lua_Debug ar;
	for (int level = 1; lua_getstack(L, level, &ar); level++)
	{
		lua_getinfo(L, "l", &ar);
		if (ar.currentline > 0)
			error_handler_lines += std::to_string(ar.currentline) + " ";
	}

	lua_pushstring(L, "handled");
	return 1;
}

static int ThrowError(lua_State* L)
{
	return luaL_error(L, "error handler failed");
}

static const String lua_error_rml = R"RML(
<rml>
<head>
	<title>Test</title>
	<link type="text/rcss" href="/assets/rml.rcss"/>
	<style>
		body { font-family: LatoLatin; }
	</style>
</head>
<body data-model="error_model">
	<div id="button" onclick="error('from handler')"/>
	<div id="model_button" data-event-click="fail()"/>
</body>
</rml>
)RML";

static const String lua_file_error_rml = R"RML(
<rml>
<head>
	<title>Test</title>
	<link type="text/rcss" href="/assets/rml.rcss"/>
	<script src="/../Tests/Data/UnitTests/lua_syntax_error.lua"/>
</head>
<body/>
</rml>
)RML";

TEST_CASE("lua.options.error_handler")
{
	TestsShell::ShutdownShell();

	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	Lua::Options options;
	options.error_handler = RecordError;
	Lua::Initialise(L, options);

	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	error_handler_calls = 0;
	error_handler_message.clear();
	error_handler_lines.clear();

	// The handler replaces the default logging, so none of these errors log a warning.
	SUBCASE("runtime_error")
	{
		CHECK_FALSE(Lua::Interpreter::DoString("local function inner()\n"
											   "  error('boom')\n"
											   "end\n"
											   "inner()\n",
			"test"));
		CHECK(error_handler_calls == 1);
		CHECK(Contains(error_handler_message, "boom"));
		// The stack still holds 'inner' at line 2, called from line 4.
		CHECK(error_handler_lines == "2 4 ");
	}
	SUBCASE("syntax_error")
	{
		CHECK_FALSE(Lua::Interpreter::DoString("this is not lua", "test"));
		CHECK(error_handler_calls == 1);
	}
	SUBCASE("file_syntax_error")
	{
		ElementDocument* document = context->LoadDocumentFromMemory(lua_file_error_rml);
		REQUIRE(document);
		CHECK(error_handler_calls == 1);
		CHECK(Contains(error_handler_message, "lua_syntax_error.lua"));
		document->Close();
	}
	SUBCASE("event_handler_error")
	{
		REQUIRE(Lua::Interpreter::DoString(R"(
			rmlui.contexts["main"]:OpenDataModel("error_model", {
				fail = function() error('from model') end,
			})
		)",
			"test"));

		ElementDocument* document = context->LoadDocumentFromMemory(lua_error_rml);
		REQUIRE(document);
		document->Show();
		TestsShell::RenderLoop();

		document->GetElementById("button")->Click();
		CHECK(error_handler_calls == 1);
		CHECK(Contains(error_handler_message, "from handler"));

		// Data model event callbacks go through a different call path.
		document->GetElementById("model_button")->Click();
		CHECK(error_handler_calls == 2);
		CHECK(Contains(error_handler_message, "from model"));

		document->Close();
		TestsShell::RenderLoop();
		REQUIRE(Lua::Interpreter::DoString("rmlui.contexts['main']:CloseDataModel('error_model')", "test"));
	}

	TestsShell::ShutdownShell();
	lua_close(L);
}

TEST_CASE("lua.options.error_handler_fails")
{
	TestsShell::ShutdownShell();

	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	Lua::Options options;
	options.error_handler = ThrowError;
	Lua::Initialise(L, options);

	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	const StringList& warnings = TestsShell::GetTestsSystemInterface()->GetLoggedWarnings();

	// Lua can't report an error through a handler that fails itself, so the plugin logs it instead.
	SUBCASE("runtime_error")
	{
		TestsShell::SetNumExpectedWarnings(1);
		CHECK_FALSE(Lua::Interpreter::DoString("error('boom')", "test"));
		REQUIRE(warnings.size() == 1);
		CHECK(Contains(warnings[0], "error in error handling"));
		TestsShell::SetNumExpectedWarnings(0);
	}
	SUBCASE("syntax_error")
	{
		// Both the syntax error and the handler's own error are logged.
		TestsShell::SetNumExpectedWarnings(1);
		CHECK_FALSE(Lua::Interpreter::DoString("this is not lua", "test"));
		REQUIRE(warnings.size() == 1);
		CHECK(Contains(warnings[0], "near 'is'"));
		CHECK(Contains(warnings[0], "error handler failed"));
		TestsShell::SetNumExpectedWarnings(0);
	}

	TestsShell::ShutdownShell();
	lua_close(L);
}

TEST_CASE("lua.options.error_handler_unset")
{
	TestsShell::ShutdownShell();

	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	Lua::Initialise(L);

	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	const StringList& warnings = TestsShell::GetTestsSystemInterface()->GetLoggedWarnings();

	// Without a handler, each error is logged as a single warning.
	SUBCASE("runtime_error")
	{
		TestsShell::SetNumExpectedWarnings(1);
		CHECK_FALSE(Lua::Interpreter::DoString("error('boom')", "test"));
		REQUIRE(warnings.size() == 1);
		CHECK(Contains(warnings[0], "boom"));
		CHECK(Contains(warnings[0], "stack traceback"));
		TestsShell::SetNumExpectedWarnings(0);
	}
	SUBCASE("syntax_error")
	{
		TestsShell::SetNumExpectedWarnings(1);
		CHECK_FALSE(Lua::Interpreter::DoString("this is not lua", "test"));
		REQUIRE(warnings.size() == 1);
		CHECK(Contains(warnings[0], "near 'is'"));
		TestsShell::SetNumExpectedWarnings(0);
	}

	TestsShell::ShutdownShell();
	lua_close(L);
}
