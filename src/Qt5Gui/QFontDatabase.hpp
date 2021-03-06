#ifndef luacxx_QFontDatabase_INCLUDED
#define luacxx_QFontDatabase_INCLUDED

#include "../stack.hpp"
#include "../enum.hpp"

#include <QFontDatabase>

LUA_METATABLE_BUILT(QFontDatabase)
LUA_METATABLE_ENUM(QFontDatabase::SystemFont)
LUA_METATABLE_ENUM(QFontDatabase::WritingSystem)

extern "C" int luaopen_Qt5Gui_QFontDatabase(lua_State* const);

#endif // luacxx_QFontDatabase_INCLUDED
