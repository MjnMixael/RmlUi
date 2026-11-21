#pragma once

#include <RmlUi/Core/Types.h>

typedef struct lua_State lua_State;

namespace Rml {
namespace Lua {

/** Loads a Lua file and leaves the compiled chunk on top of the stack, without running it.
    @return False if the file couldn't be loaded, in which case the error is reported and nothing is pushed. */
bool LoadFileChunk(lua_State* L, const String& file);

} // namespace Lua
} // namespace Rml
