#ifndef LUACXX_QTEXTDOCUMENT_INCLUDED
#define LUACXX_QTEXTDOCUMENT_INCLUDED

#include "Qt5Gui.hpp"
#include "../enum.hpp"

#include <QTextDocument>

#include "../Qt5Core/QObject.hpp"
LUA_METATABLE_INHERIT(QTextDocument, QObject)

LUA_METATABLE_ENUM(QTextDocument::FindFlag)
LUA_METATABLE_ENUM(QTextDocument::MetaInformation)
LUA_METATABLE_ENUM(QTextDocument::ResourceType)
LUA_METATABLE_ENUM(QTextDocument::Stacks)

extern "C" int luaopen_Qt5Gui_QTextDocument(lua_State* const);

#endif // LUACXX_QTEXTDOCUMENT_INCLUDED
