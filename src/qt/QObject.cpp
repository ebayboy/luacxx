#include "qobject.hpp"

#include "type/QVariant.hpp"

#include "LuaStack.hpp"
#include "LuaValue.hpp"
#include "QObjectSlot.hpp"

#include <QObject>
#include <QMetaObject>
#include <QMetaMethod>
#include <functional>

namespace {
    void __index(LuaStack& stack);
    void __newindex(LuaStack& stack);

    void metaInvokeDirectMethod(LuaStack& stack, QObject* const obj, const QMetaMethod& method);
    void metaInvokeLuaCallableMethod(LuaStack& stack, QObject* const obj, const QMetaMethod& method);
    void callMethod(LuaStack& stack);
    void connectSlot(LuaStack& stack);

} // namespace anonymous

void lua::qobject(LuaStack& stack, QObject& obj)
{
    stack.pushMetatable();
    stack.pushPointer(&obj);
    stack.push(__index, 1);
    stack.pushedSet("__index", -2);

    stack.pushPointer(&obj);
    stack.push(__newindex, 1);
    stack.pushedSet("__newindex", -2);
    stack.setMetatable();
}

namespace {

bool retrieveArgs(LuaStack& stack, LuaUserdata** savedUserdata, QObject** obj, const char** name)
{
    void* validatingUserdata = stack.pointer(1);
    stack.shift();

    LuaUserdata* userdata = *savedUserdata = stack.get<LuaUserdata*>(1);
    if (!userdata) {
        goto fail;
    }

    if (!userdata->rawData()) {
        goto fail;
    }

    *obj = static_cast<QObject*>(userdata->rawData());

    if (validatingUserdata != *obj) {
        // The metamethod was not called with the expected userdata object.
        goto fail;
    }

    stack.at(2) >> *name;
    if (!name) {
        goto fail;
    }
    stack.shift(2);

    return true;

    fail:
        *savedUserdata = nullptr;
        *obj = nullptr;
        *name = nullptr;
        stack.clear();
        lua::push(stack, lua::value::nil);
        return false;
}

QString getSignature(const QMetaMethod& method)
{
    #if QT_VERSION >= 0x050000
    return QString::fromLatin1(method.methodSignature());
    #else
    return QString::fromLatin1(method.signature());
    #endif
}

void __index(LuaStack& stack)
{
    LuaUserdata* userdata;
    QObject* obj;
    const char* name;
    if (!retrieveArgs(stack, &userdata, &obj, &name)) {
        return;
    }

    if (userdata->hasMethod(name)) {
        stack.pushPointer(obj);
        lua::push(stack, name);
        lua::push(stack, callMethod, 2);
        return;
    }

    // First, check for properties
    QVariant propValue = obj->property(name);

    if (propValue.isValid()) {
        lua::push(stack, propValue);
        return;
    }

    if (QString(name) == "connect") {
        stack.pushPointer(obj);
        lua::push(stack, connectSlot, 1);
        return;
    }

    // Not a property, so look for a method for the given the name.
    const QMetaObject* const metaObject = obj->metaObject();
    for(int i = 0; i < metaObject->methodCount(); ++i) {
        QString sig = getSignature(metaObject->method(i));
        if (sig.startsWith(QString(name) + "(")) {
            stack.pushPointer(obj);
            lua::push(stack, name);
            lua::push(stack, callMethod, 2);
            return;
        }
    }

    // Still couldn't find anything, so ignore case
    for(int i = 0; i < metaObject->methodCount(); ++i) {
        QString sig = getSignature(metaObject->method(i));
        if (sig.startsWith(QString(name) + "(", Qt::CaseInsensitive)) {
            stack.pushPointer(obj);
            lua::push(stack, name);
            lua::push(stack, callMethod, 2);
            return;
        }
    }
    lua::push(stack, lua::value::nil);
}

void __newindex(LuaStack& stack)
{
    LuaUserdata* userdata;
    QObject* obj;
    const char* name;
    if (!retrieveArgs(stack, &userdata, &obj, &name)) {
        return;
    }
    QVariant prop = obj->property(name);
    if (!prop.isValid()) {
        throw lua::exception("New properties must not be added to this userdata");
    }
    stack.begin() >> prop;
    obj->setProperty(name, prop);
}

void connectSlot(LuaStack& stack)
{
    QObject* validatingUserdata = static_cast<QObject*>(stack.pointer(1));
    stack.shift();

    auto userdata = stack.get<LuaUserdata*>(1);
    if (!userdata) {
        throw lua::exception("Method must be invoked with a valid userdata");
    }
    if (userdata->rawData() != validatingUserdata) {
        if (!userdata->data()) {
            throw lua::exception("Userdata must have an associated internal object");
        }
        if (userdata->dataType() != "QObject") {
            throw lua::exception(
                QString("Userdata must be of type QObject, but was given: '%1'")
                    .arg(userdata->dataType().c_str())
                    .toStdString()
            );
        }
        throw lua::exception("Userdata provided with method call must match the userdata used to access that method");
    }
    QObject* const obj = validatingUserdata;
    stack.shift();

    if (stack.size() != 2) {
        throw lua::exception(
            QString("Exactly 2 arguments must be provided. Given %1").arg(stack.size()).toStdString()
        );
    }

    if (stack.typestring(1) != "string") {
        throw lua::exception("signal must be a string");
    }
    auto signal = stack.get<std::string>(1);
    stack.shift();

    // TODO Make this use a cleaner function, like, lua::get<LuaReference>
    LuaReference slot = LuaReference(
        stack.luaState(),
        LuaReferenceAccessible(stack.luaState(), stack.saveAndPop())
    );
    if (slot.typestring() != "function") {
        throw lua::exception("Provided slot must be a function");
    }

    const QMetaObject* const metaObject = obj->metaObject();

    // Find the signal

    int signalId = -1;
    if (signal.find("(") != std::string::npos) {
        QByteArray signalSig = QMetaObject::normalizedSignature(signal.c_str());
        signalId = metaObject->indexOfSignal(signalSig);
    } else {
        for (int i = 0; i < metaObject->methodCount(); ++i) {
            if (getSignature(metaObject->method(i)).startsWith(signal.c_str())) {
                if (signalId != -1) {
                    throw lua::exception(std::string("Ambiguous signal name: ") + signal);
                }
                signalId = i;
            }
        }
    }
    if (signalId == -1) {
        throw lua::exception(std::string("No signal for name: ") + signal);
    }

    auto slotWrapper = new lua::QObjectSlot(
        obj,
        metaObject->method(signalId),
        slot
    );
    lua::QObjectSlot::connect(slotWrapper);

    QMetaObject::connect(obj, signalId, slotWrapper, 0);

    stack.clear();

    lua::push(stack, std::function<void()>([=]() {
        lua::QObjectSlot::disconnect(slotWrapper);
    }));
}

void callMethod(LuaStack& stack)
{
    QObject* validatingUserdata = static_cast<QObject*>(stack.pointer(1));
    stack.shift();

    auto name = stack.get<const char*>(1);
    if (stack.size() < 2) {
        throw lua::exception("Method must be invoked with a valid userdata");
    }
    auto userdata = stack.get<LuaUserdata*>(2);
    if (!userdata) {
        throw lua::exception("Method must be invoked with a valid userdata");
    }
    if (userdata->rawData() != validatingUserdata) {
        if (!userdata->data()) {
            throw lua::exception("Userdata must have an associated internal object");
        }
        if (userdata->dataType() != "QObject") {
            throw lua::exception(
                QString("Userdata must be of type QObject, but was given: '%1'")
                    .arg(userdata->dataType().c_str())
                    .toStdString()
            );
        }
        throw lua::exception("Userdata provided with method call must match the userdata used to access that method");
    }
    QObject* const obj = validatingUserdata;
    stack.shift(2);

    if (userdata->hasMethod(name)) {
        userdata->invoke(name, stack);
        return;
    }

    const QMetaObject* const metaObject = obj->metaObject();

    // Prefer methods that handle the stack directly.
    for (int i = 0; i < metaObject->methodCount(); ++i) {
        QMetaMethod method(metaObject->method(i));
        QString sig = getSignature(method);
        if (sig == QString(name) + "(LuaStack&)") {
            // The method is capable of handling the Lua stack directly, so invoke it
            metaInvokeLuaCallableMethod(stack, obj, method);
            userdata->addMethod(
                name,
                lua::LuaCallable([obj, method](LuaStack& stack) {
                    metaInvokeLuaCallableMethod(stack, obj, method);
                })
            );
            return;
        }
    }

    // Ignore case and perform the above search again
    for (int i = 0; i < metaObject->methodCount(); ++i) {
        QMetaMethod method(metaObject->method(i));
        QString sig = getSignature(method);
        if (sig.endsWith("(LuaStack&)") && sig.startsWith(QString(name) + "(", Qt::CaseInsensitive)) {
            // The method is capable of handling the Lua stack directly, so invoke it
            metaInvokeLuaCallableMethod(stack, obj, method);
            userdata->addMethod(
                name,
                lua::LuaCallable([obj, method](LuaStack& stack) {
                    metaInvokeLuaCallableMethod(stack, obj, method);
                })
            );
            return;
        }
    }

    // Look for any method that matches the requested name
    for (int i = 0; i < metaObject->methodCount(); ++i) {
        QMetaMethod method(metaObject->method(i));
        QString sig = getSignature(method);
        if (sig.startsWith(QString(name) + "(")) {
            metaInvokeDirectMethod(stack, obj, method);
            userdata->addMethod(
                name,
                lua::LuaCallable([obj, method](LuaStack& stack) {
                    metaInvokeDirectMethod(stack, obj, method);
                })
            );
            return;
        }
    }

    // Still can't find anything, so ignore case and look again
    for (int i = 0; i < metaObject->methodCount(); ++i) {
        QMetaMethod method(metaObject->method(i));
        QString sig = getSignature(method);
        if (sig.startsWith(QString(name) + "(", Qt::CaseInsensitive)) {
            metaInvokeDirectMethod(stack, obj, method);
            userdata->addMethod(
                name,
                lua::LuaCallable([obj, method](LuaStack& stack) {
                    metaInvokeDirectMethod(stack, obj, method);
                })
            );
            return;
        }
    }

    throw lua::exception(QString("No method found with name '%1'").arg(name).toStdString());
}

void metaInvokeDirectMethod(LuaStack& stack, QObject* const obj, const QMetaMethod& method)
{
    QList<QVariant> variants;
    auto returnType = QMetaType::type(method.typeName());
    if (returnType != QMetaType::Void) {
        variants << QVariant(QMetaType::type(method.typeName()), nullptr);
    }
    else {
        variants << QVariant();
    }
    QList<QByteArray> params = method.parameterTypes();
    for (int i = 0; i < params.count(); ++i) {
        int type = QMetaType::type(params.at(i));
        if (!type) {
            std::stringstream str;
            str << "I don't know how to convert the object type, "
                << params.at(i).constData()
                << ", into a QVariant, so I can't directly invoke the method: "
                << getSignature(method).toStdString();
            throw std::logic_error(str.str());
        }
        QVariant p(type, (void*)0);
        stack.at(i + 1) >> p;
        p.convert((QVariant::Type)type);
        variants << p;
    }
    stack.clear();
    void* vvargs[11];
    for (int i = 0; i < variants.size(); ++i) {
        vvargs[i] = const_cast<void*>(variants.at(i).data());
    }
    QMetaObject::metacall(
        obj,
        QMetaObject::InvokeMetaMethod,
        method.methodIndex(),
        vvargs);
    if (variants.at(0).isValid()) {
        lua::push(stack, variants.at(0));
    }
}

void metaInvokeLuaCallableMethod(LuaStack& stack, QObject* const obj, const QMetaMethod& method)
{
    auto returnType = QMetaType::type(method.typeName());
    QVariant rv;
    if (returnType != QMetaType::Void) {
        rv = QVariant(QMetaType::type(method.typeName()), nullptr);
    }
    void* vvargs[2];
    vvargs[0] = const_cast<void*>(rv.data());
    vvargs[1] = &stack;
    QMetaObject::metacall(
        obj,
        QMetaObject::InvokeMetaMethod,
        method.methodIndex(),
        vvargs);
    if (rv.isValid()) {
        stack.clear();
        lua::push(stack, rv);
    }
}

} // namespace anonymous
