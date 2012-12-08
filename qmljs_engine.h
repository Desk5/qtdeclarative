/****************************************************************************
**
** Copyright (C) 2012 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the V4VM module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/
#ifndef QMLJS_ENGINE_H
#define QMLJS_ENGINE_H

#include <qv4isel_p.h>
#include <qmljs_objects.h>
#include <qmljs_environment.h>
#include <setjmp.h>

namespace QQmlJS {

namespace Debugging {
class Debugger;
} // namespace Debugging

namespace VM {

struct Value;
class Array;
struct Object;
struct BooleanObject;
struct NumberObject;
struct StringObject;
struct ArrayObject;
struct DateObject;
struct FunctionObject;
struct RegExpObject;
struct ErrorObject;
struct ArgumentsObject;
struct ExecutionContext;
struct ExecutionEngine;
class MemoryManager;

struct ObjectPrototype;
struct StringPrototype;
struct NumberPrototype;
struct BooleanPrototype;
struct ArrayPrototype;
struct FunctionPrototype;
struct DatePrototype;
struct RegExpPrototype;
struct ErrorPrototype;
struct EvalErrorPrototype;
struct RangeErrorPrototype;
struct ReferenceErrorPrototype;
struct SyntaxErrorPrototype;
struct TypeErrorPrototype;
struct URIErrorPrototype;

struct ExecutionEngine
{
    MemoryManager *memoryManager;
    EvalISelFactory *iselFactory;
    ExecutionContext *current;
    ExecutionContext *rootContext;

    Debugging::Debugger *debugger;

    Value globalObject;

    Value objectCtor;
    Value stringCtor;
    Value numberCtor;
    Value booleanCtor;
    Value arrayCtor;
    Value functionCtor;
    Value dateCtor;
    Value regExpCtor;
    Value errorCtor;
    Value evalErrorCtor;
    Value rangeErrorCtor;
    Value referenceErrorCtor;
    Value syntaxErrorCtor;
    Value typeErrorCtor;
    Value uRIErrorCtor;

    ObjectPrototype *objectPrototype;
    StringPrototype *stringPrototype;
    NumberPrototype *numberPrototype;
    BooleanPrototype *booleanPrototype;
    ArrayPrototype *arrayPrototype;
    FunctionPrototype *functionPrototype;
    DatePrototype *datePrototype;
    RegExpPrototype *regExpPrototype;
    ErrorPrototype *errorPrototype;
    EvalErrorPrototype *evalErrorPrototype;
    RangeErrorPrototype *rangeErrorPrototype;
    ReferenceErrorPrototype *referenceErrorPrototype;
    SyntaxErrorPrototype *syntaxErrorPrototype;
    TypeErrorPrototype *typeErrorPrototype;
    URIErrorPrototype *uRIErrorPrototype;

    QHash<QString, String *> identifiers;

    String *id_length;
    String *id_prototype;
    String *id_constructor;
    String *id_arguments;
    String *id___proto__;
    String *id_enumerable;
    String *id_configurable;
    String *id_writable;
    String *id_value;
    String *id_get;
    String *id_set;

    struct ExceptionHandler {
        ExecutionContext *context;
        ExecutionContext::With *with;
        const uchar *code; // Interpreter state
        int targetTempIndex; // Interpreter state
        jmp_buf stackFrame;
    };

    QVector<ExceptionHandler> unwindStack;
    Value exception;

    struct StringPool *stringPool;

    ExecutionEngine(MemoryManager *memoryManager, EvalISelFactory *iselFactory);
    ~ExecutionEngine();

    ExecutionContext *newContext();

    String *identifier(const QString &s);

    FunctionObject *newNativeFunction(ExecutionContext *scope, String *name, Value (*code)(ExecutionContext *));
    FunctionObject *newScriptFunction(ExecutionContext *scope, IR::Function *function);

    Object *newObject();
    FunctionObject *newObjectCtor(ExecutionContext *ctx);

    String *newString(const QString &s);
    Object *newStringObject(const Value &value);
    FunctionObject *newStringCtor(ExecutionContext *ctx);

    Object *newNumberObject(const Value &value);
    FunctionObject *newNumberCtor(ExecutionContext *ctx);

    Object *newBooleanObject(const Value &value);
    FunctionObject *newBooleanCtor(ExecutionContext *ctx);

    Object *newFunctionObject(ExecutionContext *ctx);

    ArrayObject *newArrayObject();
    ArrayObject *newArrayObject(const Array &value);
    FunctionObject *newArrayCtor(ExecutionContext *ctx);

    Object *newDateObject(const Value &value);
    FunctionObject *newDateCtor(ExecutionContext *ctx);

    Object *newRegExpObject(const QString &pattern, int flags);
    FunctionObject *newRegExpCtor(ExecutionContext *ctx);

    Object *newErrorObject(const Value &value);
    Object *newSyntaxErrorObject(ExecutionContext *ctx, DiagnosticMessage *message);
    Object *newReferenceErrorObject(ExecutionContext *ctx, const QString &message);
    Object *newTypeErrorObject(ExecutionContext *ctx, const QString &message);

    Object *newMathObject(ExecutionContext *ctx);
    Object *newActivationObject();

    Object *newForEachIteratorObject(Object *o);
};

} // namespace VM
} // namespace QQmlJS

#endif
