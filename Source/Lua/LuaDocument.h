#pragma once
/*
    This class is an ElementDocument that overrides the LoadInlineScript and LoadExternalScript function
*/
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Lua/IncludeLua.h>

namespace Rml {
namespace Lua {

class LuaDocument : public ::Rml::ElementDocument {
public:
	RMLUI_RTTI_DeclareWithParent(LuaDocument, ElementDocument)

	LuaDocument(const String& tag);
	~LuaDocument();
	void LoadInlineScript(const String& content, const String& source_path, int source_line) override;
	void LoadExternalScript(const String& source_path) override;

	/// If document environments are enabled, makes the function at the given stack index use this document's table of
	/// global variables. Set is_chunk if the function is a chunk loaded from source, rather than a function it created.
	void ApplyEnvironment(lua_State* L, int function_index, bool is_chunk);

private:
	// Registry reference to this document's table of global variables, created when first needed.
	int environment_ref = LUA_NOREF;
};

} // namespace Lua
} // namespace Rml
