/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the tools applications of the Qt Toolkit.
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

#include "qqmlcodegenerator_p.h"

#include <private/qv4compileddata_p.h>
#include <private/qqmljsparser_p.h>
#include <private/qqmljslexer_p.h>
#include <private/qqmlcompiler_p.h>
#include <private/qqmlglobal_p.h>
#include <QCoreApplication>

#ifdef CONST
#undef CONST
#endif

QT_USE_NAMESPACE

static const quint32 emptyStringIndex = 0;

DEFINE_BOOL_CONFIG_OPTION(lookupHints, QML_LOOKUP_HINTS);

using namespace QtQml;

#define COMPILE_EXCEPTION(location, desc) \
    { \
        recordError(location, desc); \
        return false; \
    }

void QmlObject::init(QQmlJS::MemoryPool *pool, int typeNameIndex, int id, const QQmlJS::AST::SourceLocation &loc)
{
    inheritedTypeNameIndex = typeNameIndex;

    location.line = loc.startLine;
    location.column = loc.startColumn;

    idIndex = id;
    indexOfDefaultProperty = -1;
    properties = pool->New<PoolList<QmlProperty> >();
    qmlSignals = pool->New<PoolList<Signal> >();
    bindings = pool->New<PoolList<Binding> >();
    functions = pool->New<PoolList<Function> >();
    functionsAndExpressions = pool->New<PoolList<CompiledFunctionOrExpression> >();
    runtimeFunctionIndices = 0;
    declarationsOverride = 0;
}

void QmlObject::dump(DebugStream &out)
{
    out << inheritedTypeNameIndex << " {" << endl;
    out.indent++;

    out.indent--;
    out << "}" << endl;
}

QString QmlObject::sanityCheckFunctionNames(const QSet<QString> &illegalNames, QQmlJS::AST::SourceLocation *errorLocation)
{
    QSet<int> functionNames;
    for (Function *f = functions->first; f; f = f->next) {
        QQmlJS::AST::FunctionDeclaration *function = f->functionDeclaration;
        Q_ASSERT(function);
        *errorLocation = function->identifierToken;
        if (functionNames.contains(f->nameIndex))
            return tr("Duplicate method name");
        functionNames.insert(f->nameIndex);

        for (QtQml::Signal *s = qmlSignals->first; s; s = s->next) {
            if (s->nameIndex == f->nameIndex)
                return tr("Duplicate method name");
        }

        const QString name = function->name.toString();
        if (name.at(0).isUpper())
            return tr("Method names cannot begin with an upper case letter");
        if (illegalNames.contains(name))
            return tr("Illegal method name");
    }
    return QString(); // no error
}

QString QmlObject::appendSignal(Signal *signal)
{
    QmlObject *target = declarationsOverride;
    if (!target)
        target = this;

    for (Signal *s = qmlSignals->first; s; s = s->next) {
        if (s->nameIndex == signal->nameIndex)
            return tr("Duplicate signal name");
    }

    target->qmlSignals->append(signal);
    return QString(); // no error
}

QString QmlObject::appendProperty(QmlProperty *prop, const QString &propertyName, bool isDefaultProperty, const QQmlJS::AST::SourceLocation &defaultToken, QQmlJS::AST::SourceLocation *errorLocation)
{
    QmlObject *target = declarationsOverride;
    if (!target)
        target = this;

    for (QmlProperty *p = target->properties->first; p; p = p->next)
        if (p->nameIndex == prop->nameIndex)
            return tr("Duplicate property name");

    if (propertyName.constData()->isUpper())
        return tr("Property names cannot begin with an upper case letter");

    const int index = target->properties->append(prop);
    if (isDefaultProperty) {
        if (target->indexOfDefaultProperty != -1) {
            *errorLocation = defaultToken;
            return tr("Duplicate default property");
        }
        target->indexOfDefaultProperty = index;
    }
    return QString(); // no error
}

void QmlObject::appendFunction(QtQml::Function *f)
{
    QmlObject *target = declarationsOverride;
    if (!target)
        target = this;
    target->functions->append(f);
}

QString QmlObject::appendBinding(Binding *b, bool isListBinding)
{
    const bool bindingToDefaultProperty = (b->propertyNameIndex == 0);
    if (!isListBinding && !bindingToDefaultProperty
        && b->type != QV4::CompiledData::Binding::Type_GroupProperty
        && b->type != QV4::CompiledData::Binding::Type_AttachedProperty
        && !(b->flags & QV4::CompiledData::Binding::IsOnAssignment)) {
        Binding *existing = findBinding(b->propertyNameIndex);
        if (existing && existing->isValueBinding() == b->isValueBinding() && !(existing->flags & QV4::CompiledData::Binding::IsOnAssignment))
            return tr("Property value set multiple times");
    }
    if (isListBinding) {
        bindings->append(b);
    } else if (bindingToDefaultProperty) {
        Binding *insertionPoint = bindings->findSortedInsertionPoint<QV4::CompiledData::Location, QV4::CompiledData::Binding, &QV4::CompiledData::Binding::location>(b);
        bindings->insertAfter(insertionPoint, b);
    } else {
        bindings->prepend(b);
    }
    return QString(); // no error
}

Binding *QmlObject::findBinding(quint32 nameIndex) const
{
    for (Binding *b = bindings->first; b; b = b->next)
        if (b->propertyNameIndex == nameIndex)
            return b;
    return 0;
}

QStringList Signal::parameterStringList(const QStringList &stringPool) const
{
    QStringList result;
    result.reserve(parameters->count);
    for (SignalParameter *param = parameters->first; param; param = param->next)
        result << stringPool.at(param->nameIndex);
    return result;
}

QQmlCodeGenerator::QQmlCodeGenerator(const QSet<QString> &illegalNames)
    : illegalNames(illegalNames)
    , _object(0)
    , _propertyDeclaration(0)
    , jsGenerator(0)
{
}

bool QQmlCodeGenerator::generateFromQml(const QString &code, const QUrl &url, const QString &urlString, ParsedQML *output)
{
    this->url = url;
    QQmlJS::AST::UiProgram *program = 0;
    {
        QQmlJS::Lexer lexer(&output->jsParserEngine);
        lexer.setCode(code, /*line = */ 1);

        QQmlJS::Parser parser(&output->jsParserEngine);

        if (! parser.parse() || !parser.diagnosticMessages().isEmpty()) {

            // Extract errors from the parser
            foreach (const QQmlJS::DiagnosticMessage &m, parser.diagnosticMessages()) {

                if (m.isWarning()) {
                    qWarning("%s:%d : %s", qPrintable(urlString), m.loc.startLine, qPrintable(m.message));
                    continue;
                }

                QQmlError error;
                error.setUrl(url);
                error.setDescription(m.message);
                error.setLine(m.loc.startLine);
                error.setColumn(m.loc.startColumn);
                errors << error;
            }
            return false;
        }
        program = parser.ast();
        Q_ASSERT(program);
    }

    output->code = code;
    output->program = program;

    qSwap(_imports, output->imports);
    qSwap(_pragmas, output->pragmas);
    qSwap(_objects, output->objects);
    qSwap(_typeReferences, output->typeReferences);
    this->pool = output->jsParserEngine.pool();
    this->jsGenerator = &output->jsGenerator;

    Q_ASSERT(registerString(QString()) == emptyStringIndex);

    sourceCode = code;

    accept(program->headers);

    if (program->members->next) {
        QQmlError error;
        error.setDescription(QCoreApplication::translate("QQmlParser", "Unexpected object definition"));
        QQmlJS::AST::SourceLocation loc = program->members->next->firstSourceLocation();
        error.setLine(loc.startLine);
        error.setColumn(loc.startColumn);
        errors << error;
        return false;
    }

    QQmlJS::AST::UiObjectDefinition *rootObject = QQmlJS::AST::cast<QQmlJS::AST::UiObjectDefinition*>(program->members->member);
    Q_ASSERT(rootObject);
    if (defineQMLObject(&output->indexOfRootObject, rootObject))
        collectTypeReferences();

    qSwap(_imports, output->imports);
    qSwap(_pragmas, output->pragmas);
    qSwap(_objects, output->objects);
    qSwap(_typeReferences, output->typeReferences);
    return errors.isEmpty();
}

bool QQmlCodeGenerator::isSignalPropertyName(const QString &name)
{
    if (name.length() < 3) return false;
    if (!name.startsWith(QStringLiteral("on"))) return false;
    int ns = name.length();
    for (int i = 2; i < ns; ++i) {
        const QChar curr = name.at(i);
        if (curr.unicode() == '_') continue;
        if (curr.isUpper()) return true;
        return false;
    }
    return false; // consists solely of underscores - invalid.
}

bool QQmlCodeGenerator::visit(QQmlJS::AST::UiArrayMemberList *ast)
{
    return QQmlJS::AST::Visitor::visit(ast);
}

bool QQmlCodeGenerator::visit(QQmlJS::AST::UiProgram *)
{
    Q_ASSERT(!"should not happen");
    return false;
}

bool QQmlCodeGenerator::visit(QQmlJS::AST::UiObjectDefinition *node)
{
    // The grammar can't distinguish between two different definitions here:
    //     Item { ... }
    // versus
    //     font { ... }
    // The former is a new binding with no property name and "Item" as type name,
    // and the latter is a binding to the font property with no type name but
    // only initializer.

    QQmlJS::AST::UiQualifiedId *lastId = node->qualifiedTypeNameId;
    while (lastId->next)
        lastId = lastId->next;
    bool isType = lastId->name.unicode()->isUpper();
    if (isType) {
        int idx = 0;
        if (!defineQMLObject(&idx, node))
            return false;
        const QQmlJS::AST::SourceLocation nameLocation = node->qualifiedTypeNameId->identifierToken;
        appendBinding(nameLocation, nameLocation, emptyStringIndex, idx);
    } else {
        int idx = 0;
        if (!defineQMLObject(&idx, /*qualfied type name id*/0, node->qualifiedTypeNameId->firstSourceLocation(), node->initializer, /*declarations should go here*/_object))
            return false;
        appendBinding(node->qualifiedTypeNameId, idx);
    }
    return false;
}

bool QQmlCodeGenerator::visit(QQmlJS::AST::UiObjectBinding *node)
{
    int idx = 0;
    if (!defineQMLObject(&idx, node->qualifiedTypeNameId, node->qualifiedTypeNameId->firstSourceLocation(), node->initializer))
        return false;
    appendBinding(node->qualifiedId, idx, node->hasOnToken);
    return false;
}

bool QQmlCodeGenerator::visit(QQmlJS::AST::UiScriptBinding *node)
{
    appendBinding(node->qualifiedId, node->statement);
    return false;
}

bool QQmlCodeGenerator::visit(QQmlJS::AST::UiArrayBinding *node)
{
    const QQmlJS::AST::SourceLocation qualifiedNameLocation = node->qualifiedId->identifierToken;
    QmlObject *object = 0;
    QQmlJS::AST::UiQualifiedId *name = node->qualifiedId;
    if (!resolveQualifiedId(&name, &object))
        return false;

    qSwap(_object, object);

    const int propertyNameIndex = registerString(name->name.toString());

    if (bindingsTarget()->findBinding(propertyNameIndex) != 0) {
        recordError(name->identifierToken, tr("Property value set multiple times"));
        return false;
    }

    QQmlJS::AST::UiArrayMemberList *member = node->members;
    while (member) {
        QQmlJS::AST::UiObjectDefinition *def = QQmlJS::AST::cast<QQmlJS::AST::UiObjectDefinition*>(member->member);

        int idx = 0;
        if (!defineQMLObject(&idx, def))
            return false;
        appendBinding(qualifiedNameLocation, name->identifierToken, propertyNameIndex, idx, /*isListItem*/ true);

        member = member->next;
    }

    qSwap(_object, object);
    return false;
}

bool QQmlCodeGenerator::visit(QQmlJS::AST::UiHeaderItemList *list)
{
    return QQmlJS::AST::Visitor::visit(list);
}

bool QQmlCodeGenerator::visit(QQmlJS::AST::UiObjectInitializer *ast)
{
    return QQmlJS::AST::Visitor::visit(ast);
}

bool QQmlCodeGenerator::visit(QQmlJS::AST::UiObjectMemberList *ast)
{
    return QQmlJS::AST::Visitor::visit(ast);
}

bool QQmlCodeGenerator::visit(QQmlJS::AST::UiParameterList *ast)
{
    return QQmlJS::AST::Visitor::visit(ast);
}

bool QQmlCodeGenerator::visit(QQmlJS::AST::UiQualifiedId *id)
{
    return QQmlJS::AST::Visitor::visit(id);
}

void QQmlCodeGenerator::accept(QQmlJS::AST::Node *node)
{
    QQmlJS::AST::Node::acceptChild(node, this);
}

bool QQmlCodeGenerator::defineQMLObject(int *objectIndex, QQmlJS::AST::UiQualifiedId *qualifiedTypeNameId, const QQmlJS::AST::SourceLocation &location, QQmlJS::AST::UiObjectInitializer *initializer, QmlObject *declarationsOverride)
{
    if (QQmlJS::AST::UiQualifiedId *lastName = qualifiedTypeNameId) {
        while (lastName->next)
            lastName = lastName->next;
        if (!lastName->name.constData()->isUpper()) {
            recordError(lastName->identifierToken, tr("Expected type name"));
            return false;
        }
    }

    QmlObject *obj = New<QmlObject>();
    _objects.append(obj);
    *objectIndex = _objects.size() - 1;
    qSwap(_object, obj);

    _object->init(pool, registerString(asString(qualifiedTypeNameId)), emptyStringIndex, location);
    _object->declarationsOverride = declarationsOverride;

    // A new object is also a boundary for property declarations.
    QmlProperty *declaration = 0;
    qSwap(_propertyDeclaration, declaration);

    accept(initializer);

    qSwap(_propertyDeclaration, declaration);

    qSwap(_object, obj);

    if (!errors.isEmpty())
        return false;

    QQmlJS::AST::SourceLocation loc;
    QString error = obj->sanityCheckFunctionNames(illegalNames, &loc);
    if (!error.isEmpty()) {
        recordError(loc, error);
        return false;
    }

    return true;
}

bool QQmlCodeGenerator::visit(QQmlJS::AST::UiImport *node)
{
    QString uri;
    QV4::CompiledData::Import *import = New<QV4::CompiledData::Import>();

    if (!node->fileName.isNull()) {
        uri = node->fileName.toString();

        if (uri.endsWith(QLatin1String(".js"))) {
            import->type = QV4::CompiledData::Import::ImportScript;
        } else {
            import->type = QV4::CompiledData::Import::ImportFile;
        }
    } else {
        import->type = QV4::CompiledData::Import::ImportLibrary;
        uri = asString(node->importUri);
    }

    import->qualifierIndex = emptyStringIndex;

    // Qualifier
    if (!node->importId.isNull()) {
        QString qualifier = node->importId.toString();
        if (!qualifier.at(0).isUpper()) {
            QQmlError error;
            error.setDescription(QCoreApplication::translate("QQmlParser","Invalid import qualifier ID"));
            error.setLine(node->importIdToken.startLine);
            error.setColumn(node->importIdToken.startColumn);
            errors << error;
            return false;
        }
        if (qualifier == QLatin1String("Qt")) {
            QQmlError error;
            error.setDescription(QCoreApplication::translate("QQmlParser","Reserved name \"Qt\" cannot be used as an qualifier"));
            error.setLine(node->importIdToken.startLine);
            error.setColumn(node->importIdToken.startColumn);
            errors << error;
            return false;
        }
        import->qualifierIndex = registerString(qualifier);

        // Check for script qualifier clashes
        bool isScript = import->type == QV4::CompiledData::Import::ImportScript;
        for (int ii = 0; ii < _imports.count(); ++ii) {
            QV4::CompiledData::Import *other = _imports.at(ii);
            bool otherIsScript = other->type == QV4::CompiledData::Import::ImportScript;

            if ((isScript || otherIsScript) && qualifier == jsGenerator->strings.at(other->qualifierIndex)) {
                QQmlError error;
                error.setDescription(QCoreApplication::translate("QQmlParser","Script import qualifiers must be unique."));
                error.setLine(node->importIdToken.startLine);
                error.setColumn(node->importIdToken.startColumn);
                errors << error;
                return false;
            }
        }

    } else if (import->type == QV4::CompiledData::Import::ImportScript) {
        QQmlError error;
        error.setDescription(QCoreApplication::translate("QQmlParser","Script import requires a qualifier"));
        error.setLine(node->fileNameToken.startLine);
        error.setColumn(node->fileNameToken.startColumn);
        errors << error;
        return false;
    }

    if (node->versionToken.isValid()) {
        extractVersion(textRefAt(node->versionToken), &import->majorVersion, &import->minorVersion);
    } else if (import->type == QV4::CompiledData::Import::ImportLibrary) {
        QQmlError error;
        error.setDescription(QCoreApplication::translate("QQmlParser","Library import requires a version"));
        error.setLine(node->importIdToken.startLine);
        error.setColumn(node->importIdToken.startColumn);
        errors << error;
        return false;
    } else {
        // For backward compatibility in how the imports are loaded we
        // must otherwise initialize the major and minor version to -1.
        import->majorVersion = -1;
        import->minorVersion = -1;
    }

    import->location.line = node->importToken.startLine;
    import->location.column = node->importToken.startColumn;

    import->uriIndex = registerString(uri);

    _imports.append(import);

    return false;
}

bool QQmlCodeGenerator::visit(QQmlJS::AST::UiPragma *node)
{
    Pragma *pragma = New<Pragma>();

    // For now the only valid pragma is Singleton, so lets validate the input
    if (!node->pragmaType->name.isNull())
    {
        if (QLatin1String("Singleton") == node->pragmaType->name)
        {
            pragma->type = Pragma::PragmaSingleton;
        } else {
            QQmlError error;
            error.setDescription(QCoreApplication::translate("QQmlParser","Pragma requires a valid qualifier"));
            error.setLine(node->pragmaToken.startLine);
            error.setColumn(node->pragmaToken.startColumn);
            errors << error;
            return false;
        }
    } else {
        QQmlError error;
        error.setDescription(QCoreApplication::translate("QQmlParser","Pragma requires a valid qualifier"));
        error.setLine(node->pragmaToken.startLine);
        error.setColumn(node->pragmaToken.startColumn);
        errors << error;
        return false;
    }

    pragma->location.line = node->pragmaToken.startLine;
    pragma->location.column = node->pragmaToken.startColumn;
    _pragmas.append(pragma);

    return false;
}

static QStringList astNodeToStringList(QQmlJS::AST::Node *node)
{
    if (node->kind == QQmlJS::AST::Node::Kind_IdentifierExpression) {
        QString name =
            static_cast<QQmlJS::AST::IdentifierExpression *>(node)->name.toString();
        return QStringList() << name;
    } else if (node->kind == QQmlJS::AST::Node::Kind_FieldMemberExpression) {
        QQmlJS::AST::FieldMemberExpression *expr = static_cast<QQmlJS::AST::FieldMemberExpression *>(node);

        QStringList rv = astNodeToStringList(expr->base);
        if (rv.isEmpty())
            return rv;
        rv.append(expr->name.toString());
        return rv;
    }
    return QStringList();
}

bool QQmlCodeGenerator::visit(QQmlJS::AST::UiPublicMember *node)
{
    static const struct TypeNameToType {
        const char *name;
        size_t nameLength;
        QV4::CompiledData::Property::Type type;
    } propTypeNameToTypes[] = {
        { "int", strlen("int"), QV4::CompiledData::Property::Int },
        { "bool", strlen("bool"), QV4::CompiledData::Property::Bool },
        { "double", strlen("double"), QV4::CompiledData::Property::Real },
        { "real", strlen("real"), QV4::CompiledData::Property::Real },
        { "string", strlen("string"), QV4::CompiledData::Property::String },
        { "url", strlen("url"), QV4::CompiledData::Property::Url },
        { "color", strlen("color"), QV4::CompiledData::Property::Color },
        // Internally QTime, QDate and QDateTime are all supported.
        // To be more consistent with JavaScript we expose only
        // QDateTime as it matches closely with the Date JS type.
        // We also call it "date" to match.
        // { "time", strlen("time"), Property::Time },
        // { "date", strlen("date"), Property::Date },
        { "date", strlen("date"), QV4::CompiledData::Property::DateTime },
        { "rect", strlen("rect"), QV4::CompiledData::Property::Rect },
        { "point", strlen("point"), QV4::CompiledData::Property::Point },
        { "size", strlen("size"), QV4::CompiledData::Property::Size },
        { "font", strlen("font"), QV4::CompiledData::Property::Font },
        { "vector2d", strlen("vector2d"), QV4::CompiledData::Property::Vector2D },
        { "vector3d", strlen("vector3d"), QV4::CompiledData::Property::Vector3D },
        { "vector4d", strlen("vector4d"), QV4::CompiledData::Property::Vector4D },
        { "quaternion", strlen("quaternion"), QV4::CompiledData::Property::Quaternion },
        { "matrix4x4", strlen("matrix4x4"), QV4::CompiledData::Property::Matrix4x4 },
        { "variant", strlen("variant"), QV4::CompiledData::Property::Variant },
        { "var", strlen("var"), QV4::CompiledData::Property::Var }
    };
    static const int propTypeNameToTypesCount = sizeof(propTypeNameToTypes) /
                                                sizeof(propTypeNameToTypes[0]);

    if (node->type == QQmlJS::AST::UiPublicMember::Signal) {
        Signal *signal = New<Signal>();
        QString signalName = node->name.toString();
        signal->nameIndex = registerString(signalName);

        QQmlJS::AST::SourceLocation loc = node->typeToken;
        signal->location.line = loc.startLine;
        signal->location.column = loc.startColumn;

        signal->parameters = New<PoolList<SignalParameter> >();

        QQmlJS::AST::UiParameterList *p = node->parameters;
        while (p) {
            const QStringRef &memberType = p->type;

            if (memberType.isEmpty()) {
                QQmlError error;
                error.setDescription(QCoreApplication::translate("QQmlParser","Expected parameter type"));
                error.setLine(node->typeToken.startLine);
                error.setColumn(node->typeToken.startColumn);
                errors << error;
                return false;
            }

            const TypeNameToType *type = 0;
            for (int typeIndex = 0; typeIndex < propTypeNameToTypesCount; ++typeIndex) {
                const TypeNameToType *t = propTypeNameToTypes + typeIndex;
                if (t->nameLength == size_t(memberType.length()) &&
                    QHashedString::compare(memberType.constData(), t->name, static_cast<int>(t->nameLength))) {
                    type = t;
                    break;
                }
            }

            SignalParameter *param = New<SignalParameter>();

            if (!type) {
                if (memberType.at(0).isUpper()) {
                    // Must be a QML object type.
                    // Lazily determine type during compilation.
                    param->type = QV4::CompiledData::Property::Custom;
                    param->customTypeNameIndex = registerString(p->type.toString());
                } else {
                    QQmlError error;
                    QString errStr = QCoreApplication::translate("QQmlParser","Invalid signal parameter type: ");
                    errStr.append(memberType.toString());
                    error.setDescription(errStr);
                    error.setLine(node->typeToken.startLine);
                    error.setColumn(node->typeToken.startColumn);
                    errors << error;
                    return false;
                }
            } else {
                // the parameter is a known basic type
                param->type = type->type;
                param->customTypeNameIndex = emptyStringIndex;
            }

            param->nameIndex = registerString(p->name.toString());
            param->location.line = p->identifierToken.startLine;
            param->location.column = p->identifierToken.startColumn;
            signal->parameters->append(param);
            p = p->next;
        }

        if (signalName.at(0).isUpper())
            COMPILE_EXCEPTION(node->identifierToken, tr("Signal names cannot begin with an upper case letter"));

        if (illegalNames.contains(signalName))
            COMPILE_EXCEPTION(node->identifierToken, tr("Illegal signal name"));

        QString error = _object->appendSignal(signal);
        if (!error.isEmpty()) {
            recordError(node->identifierToken, error);
            return false;
        }
    } else {
        const QStringRef &memberType = node->memberType;
        const QStringRef &name = node->name;

        bool typeFound = false;
        QV4::CompiledData::Property::Type type;

        if ((unsigned)memberType.length() == strlen("alias") &&
            QHashedString::compare(memberType.constData(), "alias", static_cast<int>(strlen("alias")))) {
            type = QV4::CompiledData::Property::Alias;
            typeFound = true;
        }

        for (int ii = 0; !typeFound && ii < propTypeNameToTypesCount; ++ii) {
            const TypeNameToType *t = propTypeNameToTypes + ii;
            if (t->nameLength == size_t(memberType.length()) &&
                QHashedString::compare(memberType.constData(), t->name, static_cast<int>(t->nameLength))) {
                type = t->type;
                typeFound = true;
            }
        }

        if (!typeFound && memberType.at(0).isUpper()) {
            const QStringRef &typeModifier = node->typeModifier;

            if (typeModifier.isEmpty()) {
                type = QV4::CompiledData::Property::Custom;
            } else if ((unsigned)typeModifier.length() == strlen("list") &&
                      QHashedString::compare(typeModifier.constData(), "list", static_cast<int>(strlen("list")))) {
                type = QV4::CompiledData::Property::CustomList;
            } else {
                QQmlError error;
                error.setDescription(QCoreApplication::translate("QQmlParser","Invalid property type modifier"));
                error.setLine(node->typeModifierToken.startLine);
                error.setColumn(node->typeModifierToken.startColumn);
                errors << error;
                return false;
            }
            typeFound = true;
        } else if (!node->typeModifier.isNull()) {
            QQmlError error;
            error.setDescription(QCoreApplication::translate("QQmlParser","Unexpected property type modifier"));
            error.setLine(node->typeModifierToken.startLine);
            error.setColumn(node->typeModifierToken.startColumn);
            errors << error;
            return false;
        }

        if (!typeFound) {
            QQmlError error;
            error.setDescription(QCoreApplication::translate("QQmlParser","Expected property type"));
            error.setLine(node->typeToken.startLine);
            error.setColumn(node->typeToken.startColumn);
            errors << error;
            return false;
        }

        QmlProperty *property = New<QmlProperty>();
        property->flags = 0;
        if (node->isReadonlyMember)
            property->flags |= QV4::CompiledData::Property::IsReadOnly;
        property->type = type;
        if (type >= QV4::CompiledData::Property::Custom)
            property->customTypeNameIndex = registerString(memberType.toString());
        else
            property->customTypeNameIndex = emptyStringIndex;

        const QString propName = name.toString();
        property->nameIndex = registerString(propName);

        QQmlJS::AST::SourceLocation loc = node->firstSourceLocation();
        property->location.line = loc.startLine;
        property->location.column = loc.startColumn;

        property->aliasPropertyValueIndex = emptyStringIndex;

        if (type == QV4::CompiledData::Property::Alias) {
            if (!node->statement && !node->binding)
                COMPILE_EXCEPTION(loc, tr("No property alias location"));

            QQmlJS::AST::SourceLocation rhsLoc;
            if (node->binding)
                rhsLoc = node->binding->firstSourceLocation();
            else if (node->statement)
                rhsLoc = node->statement->firstSourceLocation();
            else
                rhsLoc = node->semicolonToken;
            property->aliasLocation.line = rhsLoc.startLine;
            property->aliasLocation.column = rhsLoc.startColumn;

            QStringList alias;

            if (QQmlJS::AST::ExpressionStatement *stmt = QQmlJS::AST::cast<QQmlJS::AST::ExpressionStatement*>(node->statement)) {
                alias = astNodeToStringList(stmt->expression);
                if (alias.isEmpty()) {
                    if (isStatementNodeScript(node->statement)) {
                        COMPILE_EXCEPTION(rhsLoc, tr("Invalid alias reference. An alias reference must be specified as <id>, <id>.<property> or <id>.<value property>.<property>"));
                    } else {
                        COMPILE_EXCEPTION(rhsLoc, tr("Invalid alias location"));
                    }
                }
            } else {
                COMPILE_EXCEPTION(rhsLoc, tr("Invalid alias reference. An alias reference must be specified as <id>, <id>.<property> or <id>.<value property>.<property>"));
            }

            if (alias.count() < 1 || alias.count() > 3)
                COMPILE_EXCEPTION(rhsLoc, tr("Invalid alias reference. An alias reference must be specified as <id>, <id>.<property> or <id>.<value property>.<property>"));

             property->aliasIdValueIndex = registerString(alias.first());

             QString propertyValue = alias.value(1);
             if (alias.count() == 3) {
                 propertyValue += QLatin1Char('.');
                 propertyValue += alias.at(2);
             }
             property->aliasPropertyValueIndex = registerString(propertyValue);
        }
        QQmlJS::AST::SourceLocation errorLocation;
        QString error;

        if (illegalNames.contains(propName))
            error = tr("Illegal property name");
        else
            error = _object->appendProperty(property, propName, node->isDefaultMember, node->defaultToken, &errorLocation);

        if (!error.isEmpty()) {
            if (errorLocation.startLine == 0)
                errorLocation = node->identifierToken;

            QQmlError qmlError;
            qmlError.setDescription(error);
            qmlError.setLine(errorLocation.startLine);
            qmlError.setColumn(errorLocation.startColumn);
            errors << qmlError;
            return false;
        }

        qSwap(_propertyDeclaration, property);
        if (node->binding) {
            // process QML-like initializers (e.g. property Object o: Object {})
            QQmlJS::AST::Node::accept(node->binding, this);
        } else if (node->statement && type != QV4::CompiledData::Property::Alias) {
            appendBinding(node->identifierToken, node->identifierToken, _propertyDeclaration->nameIndex, node->statement);
        }
        qSwap(_propertyDeclaration, property);
    }

    return false;
}

bool QQmlCodeGenerator::visit(QQmlJS::AST::UiSourceElement *node)
{
    if (QQmlJS::AST::FunctionDeclaration *funDecl = QQmlJS::AST::cast<QQmlJS::AST::FunctionDeclaration *>(node->sourceElement)) {
        CompiledFunctionOrExpression *foe = New<CompiledFunctionOrExpression>();
        foe->node = funDecl;
        foe->nameIndex = registerString(funDecl->name.toString());
        foe->disableAcceleratedLookups = false;
        const int index = _object->functionsAndExpressions->append(foe);

        Function *f = New<Function>();
        f->functionDeclaration = funDecl;
        QQmlJS::AST::SourceLocation loc = funDecl->identifierToken;
        f->location.line = loc.startLine;
        f->location.column = loc.startColumn;
        f->index = index;
        f->nameIndex = registerString(funDecl->name.toString());
        _object->appendFunction(f);
    } else {
        QQmlError error;
        error.setDescription(QCoreApplication::translate("QQmlParser","JavaScript declaration outside Script element"));
        error.setLine(node->firstSourceLocation().startLine);
        error.setColumn(node->firstSourceLocation().startColumn);
        errors << error;
    }
    return false;
}

QString QQmlCodeGenerator::asString(QQmlJS::AST::UiQualifiedId *node)
{
    QString s;

    for (QQmlJS::AST::UiQualifiedId *it = node; it; it = it->next) {
        s.append(it->name);

        if (it->next)
            s.append(QLatin1Char('.'));
    }

    return s;
}

QStringRef QQmlCodeGenerator::asStringRef(QQmlJS::AST::Node *node)
{
    if (!node)
        return QStringRef();

    return textRefAt(node->firstSourceLocation(), node->lastSourceLocation());
}

void QQmlCodeGenerator::extractVersion(QStringRef string, int *maj, int *min)
{
    *maj = -1; *min = -1;

    if (!string.isEmpty()) {

        int dot = string.indexOf(QLatin1Char('.'));

        if (dot < 0) {
            *maj = string.toInt();
            *min = 0;
        } else {
            *maj = string.left(dot).toInt();
            *min = string.mid(dot + 1).toInt();
        }
    }
}

QStringRef QQmlCodeGenerator::textRefAt(const QQmlJS::AST::SourceLocation &first, const QQmlJS::AST::SourceLocation &last) const
{
    return QStringRef(&sourceCode, first.offset, last.offset + last.length - first.offset);
}

void QQmlCodeGenerator::setBindingValue(QV4::CompiledData::Binding *binding, QQmlJS::AST::Statement *statement)
{
    QQmlJS::AST::SourceLocation loc = statement->firstSourceLocation();
    binding->valueLocation.line = loc.startLine;
    binding->valueLocation.column = loc.startColumn;
    binding->type = QV4::CompiledData::Binding::Type_Invalid;
    if (_propertyDeclaration && (_propertyDeclaration->flags & QV4::CompiledData::Property::IsReadOnly))
        binding->flags |= QV4::CompiledData::Binding::InitializerForReadOnlyDeclaration;

    QQmlJS::AST::ExpressionStatement *exprStmt = QQmlJS::AST::cast<QQmlJS::AST::ExpressionStatement *>(statement);
    if (exprStmt) {
        QQmlJS::AST::ExpressionNode * const expr = exprStmt->expression;
        if (QQmlJS::AST::StringLiteral *lit = QQmlJS::AST::cast<QQmlJS::AST::StringLiteral *>(expr)) {
            binding->type = QV4::CompiledData::Binding::Type_String;
            binding->stringIndex = registerString(lit->value.toString());
        } else if (expr->kind == QQmlJS::AST::Node::Kind_TrueLiteral) {
            binding->type = QV4::CompiledData::Binding::Type_Boolean;
            binding->value.b = true;
        } else if (expr->kind == QQmlJS::AST::Node::Kind_FalseLiteral) {
            binding->type = QV4::CompiledData::Binding::Type_Boolean;
            binding->value.b = false;
        } else if (QQmlJS::AST::NumericLiteral *lit = QQmlJS::AST::cast<QQmlJS::AST::NumericLiteral *>(expr)) {
            binding->type = QV4::CompiledData::Binding::Type_Number;
            binding->value.d = lit->value;
        } else {

            if (QQmlJS::AST::UnaryMinusExpression *unaryMinus = QQmlJS::AST::cast<QQmlJS::AST::UnaryMinusExpression *>(expr)) {
               if (QQmlJS::AST::NumericLiteral *lit = QQmlJS::AST::cast<QQmlJS::AST::NumericLiteral *>(unaryMinus->expression)) {
                   binding->type = QV4::CompiledData::Binding::Type_Number;
                   binding->value.d = -lit->value;
               }
            }
        }
    }

    // Do binding instead
    if (binding->type == QV4::CompiledData::Binding::Type_Invalid) {
        binding->type = QV4::CompiledData::Binding::Type_Script;

        CompiledFunctionOrExpression *expr = New<CompiledFunctionOrExpression>();
        expr->node = statement;
        expr->nameIndex = 0;
        expr->disableAcceleratedLookups = false;
        const int index = bindingsTarget()->functionsAndExpressions->append(expr);
        binding->value.compiledScriptIndex = index;

        QQmlJS::AST::Node *nodeForString = statement;
        if (exprStmt)
            nodeForString = exprStmt->expression;
        binding->stringIndex = registerString(asStringRef(nodeForString).toString());
    }
}

void QQmlCodeGenerator::appendBinding(QQmlJS::AST::UiQualifiedId *name, QQmlJS::AST::Statement *value)
{
    const QQmlJS::AST::SourceLocation qualifiedNameLocation = name->identifierToken;
    QmlObject *object = 0;
    if (!resolveQualifiedId(&name, &object))
        return;
    qSwap(_object, object);
    appendBinding(qualifiedNameLocation, name->identifierToken, registerString(name->name.toString()), value);
    qSwap(_object, object);
}

void QQmlCodeGenerator::appendBinding(QQmlJS::AST::UiQualifiedId *name, int objectIndex, bool isOnAssignment)
{
    const QQmlJS::AST::SourceLocation qualifiedNameLocation = name->identifierToken;
    QmlObject *object = 0;
    if (!resolveQualifiedId(&name, &object))
        return;
    qSwap(_object, object);
    appendBinding(qualifiedNameLocation, name->identifierToken, registerString(name->name.toString()), objectIndex, /*isListItem*/false, isOnAssignment);
    qSwap(_object, object);
}

void QQmlCodeGenerator::appendBinding(const QQmlJS::AST::SourceLocation &qualifiedNameLocation, const QQmlJS::AST::SourceLocation &nameLocation, quint32 propertyNameIndex, QQmlJS::AST::Statement *value)
{
    if (stringAt(propertyNameIndex) == QStringLiteral("id")) {
        setId(nameLocation, value);
        return;
    }

    Binding *binding = New<Binding>();
    binding->propertyNameIndex = propertyNameIndex;
    binding->location.line = nameLocation.startLine;
    binding->location.column = nameLocation.startColumn;
    binding->flags = 0;
    setBindingValue(binding, value);
    QString error = bindingsTarget()->appendBinding(binding, /*isListBinding*/false);
    if (!error.isEmpty()) {
        recordError(qualifiedNameLocation, error);
    }
}

void QQmlCodeGenerator::appendBinding(const QQmlJS::AST::SourceLocation &qualifiedNameLocation, const QQmlJS::AST::SourceLocation &nameLocation, quint32 propertyNameIndex, int objectIndex, bool isListItem, bool isOnAssignment)
{
    if (stringAt(propertyNameIndex) == QStringLiteral("id")) {
        recordError(nameLocation, tr("Invalid component id specification"));
        return;
    }

    Binding *binding = New<Binding>();
    binding->propertyNameIndex = propertyNameIndex;
    binding->location.line = nameLocation.startLine;
    binding->location.column = nameLocation.startColumn;

    const QmlObject *obj = _objects.at(objectIndex);
    binding->valueLocation = obj->location;

    binding->flags = 0;

    if (_propertyDeclaration && (_propertyDeclaration->flags & QV4::CompiledData::Property::IsReadOnly))
        binding->flags |= QV4::CompiledData::Binding::InitializerForReadOnlyDeclaration;

    // No type name on the initializer means it must be a group property
    if (_objects.at(objectIndex)->inheritedTypeNameIndex == emptyStringIndex)
        binding->type = QV4::CompiledData::Binding::Type_GroupProperty;
    else
        binding->type = QV4::CompiledData::Binding::Type_Object;

    if (isOnAssignment)
        binding->flags |= QV4::CompiledData::Binding::IsOnAssignment;
    if (isListItem)
        binding->flags |= QV4::CompiledData::Binding::IsListItem;

    binding->value.objectIndex = objectIndex;
    QString error = bindingsTarget()->appendBinding(binding, isListItem);
    if (!error.isEmpty()) {
        recordError(qualifiedNameLocation, error);
    }
}

QmlObject *QQmlCodeGenerator::bindingsTarget() const
{
    if (_propertyDeclaration && _object->declarationsOverride)
        return _object->declarationsOverride;
    return _object;
}

bool QQmlCodeGenerator::setId(const QQmlJS::AST::SourceLocation &idLocation, QQmlJS::AST::Statement *value)
{
    QQmlJS::AST::SourceLocation loc = value->firstSourceLocation();
    QStringRef str;

    QQmlJS::AST::Node *node = value;
    if (QQmlJS::AST::ExpressionStatement *stmt = QQmlJS::AST::cast<QQmlJS::AST::ExpressionStatement *>(node)) {
        if (QQmlJS::AST::StringLiteral *lit = QQmlJS::AST::cast<QQmlJS::AST::StringLiteral *>(stmt->expression)) {
            str = lit->value;
            node = 0;
        } else
            node = stmt->expression;
    }

    if (node && str.isEmpty())
        str = asStringRef(node);

    if (str.isEmpty())
        COMPILE_EXCEPTION(loc, tr( "Invalid empty ID"));

    QChar ch = str.at(0);
    if (ch.isLetter() && !ch.isLower())
        COMPILE_EXCEPTION(loc, tr( "IDs cannot start with an uppercase letter"));

    QChar u(QLatin1Char('_'));
    if (!ch.isLetter() && ch != u)
        COMPILE_EXCEPTION(loc, tr( "IDs must start with a letter or underscore"));

    for (int ii = 1; ii < str.count(); ++ii) {
        ch = str.at(ii);
        if (!ch.isLetterOrNumber() && ch != u)
            COMPILE_EXCEPTION(loc, tr( "IDs must contain only letters, numbers, and underscores"));
    }

    QString idQString(str.toString());
    if (illegalNames.contains(idQString))
        COMPILE_EXCEPTION(loc, tr( "ID illegally masks global JavaScript property"));

    if (_object->idIndex != emptyStringIndex)
        COMPILE_EXCEPTION(idLocation, tr("Property value set multiple times"));

    _object->idIndex = registerString(idQString);
    _object->locationOfIdProperty.line = idLocation.startLine;
    _object->locationOfIdProperty.column = idLocation.startColumn;

    return true;
}

bool QQmlCodeGenerator::resolveQualifiedId(QQmlJS::AST::UiQualifiedId **nameToResolve, QmlObject **object)
{
    QQmlJS::AST::UiQualifiedId *qualifiedIdElement = *nameToResolve;

    if (qualifiedIdElement->name == QStringLiteral("id") && qualifiedIdElement->next)
        COMPILE_EXCEPTION(qualifiedIdElement->identifierToken, tr( "Invalid use of id property"));

    // If it's a namespace, prepend the qualifier and we'll resolve it later to the correct type.
    QString currentName = qualifiedIdElement->name.toString();
    if (qualifiedIdElement->next) {
        foreach (QV4::CompiledData::Import* import, _imports)
            if (import->qualifierIndex != emptyStringIndex
                && stringAt(import->qualifierIndex) == currentName) {
                qualifiedIdElement = qualifiedIdElement->next;
                currentName += QLatin1Char('.');
                currentName += qualifiedIdElement->name;

                if (!qualifiedIdElement->name.unicode()->isUpper())
                    COMPILE_EXCEPTION(qualifiedIdElement->firstSourceLocation(), tr("Expected type name"));

                break;
            }
    }

    *object = _object;
    while (qualifiedIdElement->next) {
        const quint32 propertyNameIndex = registerString(currentName);
        const bool isAttachedProperty = qualifiedIdElement->name.unicode()->isUpper();

        Binding *binding = (*object)->findBinding(propertyNameIndex);
        if (binding) {
            if (isAttachedProperty) {
                if (!binding->isAttachedProperty())
                    binding = 0;
            } else if (!binding->isGroupProperty()) {
                binding = 0;
            }
        }
        if (!binding) {
            binding = New<Binding>();
            binding->propertyNameIndex = propertyNameIndex;
            binding->location.line = qualifiedIdElement->identifierToken.startLine;
            binding->location.column = qualifiedIdElement->identifierToken.startColumn;
            binding->valueLocation.line = qualifiedIdElement->next->identifierToken.startLine;
            binding->valueLocation.column = qualifiedIdElement->next->identifierToken.startColumn;
            binding->flags = 0;

            if (isAttachedProperty)
                binding->type = QV4::CompiledData::Binding::Type_AttachedProperty;
            else
                binding->type = QV4::CompiledData::Binding::Type_GroupProperty;

            int objIndex = 0;
            if (!defineQMLObject(&objIndex, 0, QQmlJS::AST::SourceLocation(), 0, 0))
                return false;
            binding->value.objectIndex = objIndex;

            QString error = (*object)->appendBinding(binding, /*isListBinding*/false);
            if (!error.isEmpty()) {
                recordError(qualifiedIdElement->identifierToken, error);
                return false;
            }
            *object = _objects.at(objIndex);
        } else {
            Q_ASSERT(binding->isAttachedProperty() || binding->isGroupProperty());
            *object = _objects.at(binding->value.objectIndex);
        }

        qualifiedIdElement = qualifiedIdElement->next;
        if (qualifiedIdElement)
            currentName = qualifiedIdElement->name.toString();
    }
    *nameToResolve = qualifiedIdElement;
    return true;
}

void QQmlCodeGenerator::recordError(const QQmlJS::AST::SourceLocation &location, const QString &description)
{
    QQmlError error;
    error.setUrl(url);
    error.setLine(location.startLine);
    error.setColumn(location.startColumn);
    error.setDescription(description);
    errors << error;
}

void QQmlCodeGenerator::collectTypeReferences()
{
    foreach (QmlObject *obj, _objects) {
        if (obj->inheritedTypeNameIndex != emptyStringIndex) {
            QV4::CompiledData::TypeReference &r = _typeReferences.add(obj->inheritedTypeNameIndex, obj->location);
            r.needsCreation = true;
            r.errorWhenNotFound = true;
        }

        for (const QmlProperty *prop = obj->firstProperty(); prop; prop = prop->next) {
            if (prop->type >= QV4::CompiledData::Property::Custom) {
                // ### FIXME: We could report the more accurate location here by using prop->location, but the old
                // compiler can't and the tests expect it to be the object location right now.
                QV4::CompiledData::TypeReference &r = _typeReferences.add(prop->customTypeNameIndex, obj->location);
                r.needsCreation = true;
                r.errorWhenNotFound = true;
            }
        }

        for (const Binding *binding = obj->firstBinding(); binding; binding = binding->next) {
            if (binding->type == QV4::CompiledData::Binding::Type_AttachedProperty)
                _typeReferences.add(binding->propertyNameIndex, binding->location);
        }
    }
}

QQmlScript::LocationSpan QQmlCodeGenerator::location(QQmlJS::AST::SourceLocation start, QQmlJS::AST::SourceLocation end)
{
    QQmlScript::LocationSpan rv;
    rv.start.line = start.startLine;
    rv.start.column = start.startColumn;
    rv.end.line = end.startLine;
    rv.end.column = end.startColumn + end.length - 1;
    rv.range.offset = start.offset;
    rv.range.length = end.offset + end.length - start.offset;
    return rv;
}

bool QQmlCodeGenerator::isStatementNodeScript(QQmlJS::AST::Statement *statement)
{
    if (QQmlJS::AST::ExpressionStatement *stmt = QQmlJS::AST::cast<QQmlJS::AST::ExpressionStatement *>(statement)) {
        QQmlJS::AST::ExpressionNode *expr = stmt->expression;
        if (QQmlJS::AST::cast<QQmlJS::AST::StringLiteral *>(expr))
            return false;
        else if (expr->kind == QQmlJS::AST::Node::Kind_TrueLiteral)
            return false;
        else if (expr->kind == QQmlJS::AST::Node::Kind_FalseLiteral)
            return false;
        else if (QQmlJS::AST::cast<QQmlJS::AST::NumericLiteral *>(expr))
            return false;
        else {

            if (QQmlJS::AST::UnaryMinusExpression *unaryMinus = QQmlJS::AST::cast<QQmlJS::AST::UnaryMinusExpression *>(expr)) {
               if (QQmlJS::AST::cast<QQmlJS::AST::NumericLiteral *>(unaryMinus->expression)) {
                   return false;
               }
            }
        }
    }

    return true;
}

QV4::CompiledData::QmlUnit *QmlUnitGenerator::generate(ParsedQML &output)
{
    jsUnitGenerator = &output.jsGenerator;
    int unitSize = 0;
    QV4::CompiledData::Unit *jsUnit = jsUnitGenerator->generateUnit(&unitSize);

    const int importSize = sizeof(QV4::CompiledData::Import) * output.imports.count();
    const int objectOffsetTableSize = output.objects.count() * sizeof(quint32);

    QHash<QmlObject*, quint32> objectOffsets;

    int objectsSize = 0;
    foreach (QmlObject *o, output.objects) {
        objectOffsets.insert(o, unitSize + importSize + objectOffsetTableSize + objectsSize);
        objectsSize += QV4::CompiledData::Object::calculateSizeExcludingSignals(o->functionCount(), o->propertyCount(), o->signalCount(), o->bindingCount());

        int signalTableSize = 0;
        for (const Signal *s = o->firstSignal(); s; s = s->next)
            signalTableSize += QV4::CompiledData::Signal::calculateSize(s->parameters->count);

        objectsSize += signalTableSize;
    }

    const int totalSize = unitSize + importSize + objectOffsetTableSize + objectsSize;
    char *data = (char*)malloc(totalSize);
    memcpy(data, jsUnit, unitSize);
    free(jsUnit);
    jsUnit = 0;

    QV4::CompiledData::QmlUnit *qmlUnit = reinterpret_cast<QV4::CompiledData::QmlUnit *>(data);
    qmlUnit->header.flags |= QV4::CompiledData::Unit::IsQml;
    qmlUnit->offsetToImports = unitSize;
    qmlUnit->nImports = output.imports.count();
    qmlUnit->offsetToObjects = unitSize + importSize;
    qmlUnit->nObjects = output.objects.count();
    qmlUnit->indexOfRootObject = output.indexOfRootObject;

    // write imports
    char *importPtr = data + qmlUnit->offsetToImports;
    foreach (QV4::CompiledData::Import *imp, output.imports) {
        QV4::CompiledData::Import *importToWrite = reinterpret_cast<QV4::CompiledData::Import*>(importPtr);
        *importToWrite = *imp;
        importPtr += sizeof(QV4::CompiledData::Import);
    }

    // write objects
    quint32 *objectTable = reinterpret_cast<quint32*>(data + qmlUnit->offsetToObjects);
    char *objectPtr = data + qmlUnit->offsetToObjects + objectOffsetTableSize;
    foreach (QmlObject *o, output.objects) {
        *objectTable++ = objectOffsets.value(o);

        QV4::CompiledData::Object *objectToWrite = reinterpret_cast<QV4::CompiledData::Object*>(objectPtr);
        objectToWrite->inheritedTypeNameIndex = o->inheritedTypeNameIndex;
        objectToWrite->indexOfDefaultProperty = o->indexOfDefaultProperty;
        objectToWrite->idIndex = o->idIndex;
        objectToWrite->location = o->location;
        objectToWrite->locationOfIdProperty = o->locationOfIdProperty;

        quint32 nextOffset = sizeof(QV4::CompiledData::Object);

        objectToWrite->nFunctions = o->functionCount();
        objectToWrite->offsetToFunctions = nextOffset;
        nextOffset += objectToWrite->nFunctions * sizeof(quint32);

        objectToWrite->nProperties = o->propertyCount();
        objectToWrite->offsetToProperties = nextOffset;
        nextOffset += objectToWrite->nProperties * sizeof(QV4::CompiledData::Property);

        objectToWrite->nSignals = o->signalCount();
        objectToWrite->offsetToSignals = nextOffset;
        nextOffset += objectToWrite->nSignals * sizeof(quint32);

        objectToWrite->nBindings = o->bindingCount();
        objectToWrite->offsetToBindings = nextOffset;
        nextOffset += objectToWrite->nBindings * sizeof(QV4::CompiledData::Binding);

        quint32 *functionsTable = reinterpret_cast<quint32*>(objectPtr + objectToWrite->offsetToFunctions);
        for (const Function *f = o->firstFunction(); f; f = f->next)
            *functionsTable++ = o->runtimeFunctionIndices->at(f->index);

        char *propertiesPtr = objectPtr + objectToWrite->offsetToProperties;
        for (const QmlProperty *p = o->firstProperty(); p; p = p->next) {
            QV4::CompiledData::Property *propertyToWrite = reinterpret_cast<QV4::CompiledData::Property*>(propertiesPtr);
            *propertyToWrite = *p;
            propertiesPtr += sizeof(QV4::CompiledData::Property);
        }

        char *bindingPtr = objectPtr + objectToWrite->offsetToBindings;
        bindingPtr = writeBindings(bindingPtr, o, &QV4::CompiledData::Binding::isValueBindingNoAlias);
        bindingPtr = writeBindings(bindingPtr, o, &QV4::CompiledData::Binding::isSignalHandler);
        bindingPtr = writeBindings(bindingPtr, o, &QV4::CompiledData::Binding::isAttachedProperty);
        bindingPtr = writeBindings(bindingPtr, o, &QV4::CompiledData::Binding::isGroupProperty);
        bindingPtr = writeBindings(bindingPtr, o, &QV4::CompiledData::Binding::isValueBindingToAlias);
        Q_ASSERT((bindingPtr - objectToWrite->offsetToBindings - objectPtr) / sizeof(QV4::CompiledData::Binding) == unsigned(o->bindingCount()));

        quint32 *signalOffsetTable = reinterpret_cast<quint32*>(objectPtr + objectToWrite->offsetToSignals);
        quint32 signalTableSize = 0;
        char *signalPtr = objectPtr + nextOffset;
        for (const Signal *s = o->firstSignal(); s; s = s->next) {
            *signalOffsetTable++ = signalPtr - objectPtr;
            QV4::CompiledData::Signal *signalToWrite = reinterpret_cast<QV4::CompiledData::Signal*>(signalPtr);

            signalToWrite->nameIndex = s->nameIndex;
            signalToWrite->location = s->location;
            signalToWrite->nParameters = s->parameters->count;

            QV4::CompiledData::Parameter *parameterToWrite = reinterpret_cast<QV4::CompiledData::Parameter*>(signalPtr + sizeof(*signalToWrite));
            for (SignalParameter *param = s->parameters->first; param; param = param->next, ++parameterToWrite)
                *parameterToWrite = *param;

            int size = QV4::CompiledData::Signal::calculateSize(s->parameters->count);
            signalTableSize += size;
            signalPtr += size;
        }

        objectPtr += QV4::CompiledData::Object::calculateSizeExcludingSignals(o->functionCount(), o->propertyCount(), o->signalCount(), o->bindingCount());
        objectPtr += signalTableSize;
    }

    // enable flag if we encountered pragma Singleton
    foreach (Pragma *p, output.pragmas) {
        if (p->type == Pragma::PragmaSingleton) {
            qmlUnit->header.flags |= QV4::CompiledData::Unit::IsSingleton;
            break;
        }
    }

    return qmlUnit;
}

char *QmlUnitGenerator::writeBindings(char *bindingPtr, QmlObject *o, BindingFilter filter) const
{
    for (const Binding *b = o->firstBinding(); b; b = b->next) {
        if (!(b->*(filter))())
            continue;
        QV4::CompiledData::Binding *bindingToWrite = reinterpret_cast<QV4::CompiledData::Binding*>(bindingPtr);
        *bindingToWrite = *b;
        if (b->type == QV4::CompiledData::Binding::Type_Script)
            bindingToWrite->value.compiledScriptIndex = o->runtimeFunctionIndices->at(b->value.compiledScriptIndex);
        bindingPtr += sizeof(QV4::CompiledData::Binding);
    }
    return bindingPtr;
}

int QmlUnitGenerator::getStringId(const QString &str) const
{
    return jsUnitGenerator->getStringId(str);
}

JSCodeGen::JSCodeGen(const QString &fileName, const QString &sourceCode, IR::Module *jsModule, QQmlJS::Engine *jsEngine,
                     QQmlJS::AST::UiProgram *qmlRoot, QQmlTypeNameCache *imports, const QStringList &stringPool)
    : QQmlJS::Codegen(/*strict mode*/false)
    , sourceCode(sourceCode)
    , jsEngine(jsEngine)
    , qmlRoot(qmlRoot)
    , imports(imports)
    , stringPool(stringPool)
    , _disableAcceleratedLookups(false)
    , _contextObject(0)
    , _scopeObject(0)
    , _contextObjectTemp(-1)
    , _scopeObjectTemp(-1)
    , _importedScriptsTemp(-1)
    , _idArrayTemp(-1)
{
    _module = jsModule;
    _module->setFileName(fileName);
    _fileNameIsUrl = true;
}

void JSCodeGen::beginContextScope(const JSCodeGen::ObjectIdMapping &objectIds, QQmlPropertyCache *contextObject)
{
    _idObjects = objectIds;
    _contextObject = contextObject;
    _scopeObject = 0;
}

void JSCodeGen::beginObjectScope(QQmlPropertyCache *scopeObject)
{
    _scopeObject = scopeObject;
}

QVector<int> JSCodeGen::generateJSCodeForFunctionsAndBindings(const QList<CompiledFunctionOrExpression> &functions)
{
    QVector<int> runtimeFunctionIndices(functions.size());

    ScanFunctions scan(this, sourceCode, GlobalCode);
    scan.enterEnvironment(0, QmlBinding);
    scan.enterQmlScope(qmlRoot, QStringLiteral("context scope"));
    foreach (const CompiledFunctionOrExpression &f, functions) {
        Q_ASSERT(f.node != qmlRoot);
        QQmlJS::AST::FunctionDeclaration *function = QQmlJS::AST::cast<QQmlJS::AST::FunctionDeclaration*>(f.node);

        if (function)
            scan.enterQmlFunction(function);
        else
            scan.enterEnvironment(f.node, QmlBinding);

        scan(function ? function->body : f.node);
        scan.leaveEnvironment();
    }
    scan.leaveEnvironment();
    scan.leaveEnvironment();

    _env = 0;
    _function = _module->functions.at(defineFunction(QStringLiteral("context scope"), qmlRoot, 0, 0));

    for (int i = 0; i < functions.count(); ++i) {
        const CompiledFunctionOrExpression &qmlFunction = functions.at(i);
        QQmlJS::AST::Node *node = qmlFunction.node;
        Q_ASSERT(node != qmlRoot);

        QQmlJS::AST::FunctionDeclaration *function = QQmlJS::AST::cast<QQmlJS::AST::FunctionDeclaration*>(node);

        QString name;
        if (function)
            name = function->name.toString();
        else if (qmlFunction.nameIndex != 0)
            name = stringPool.value(qmlFunction.nameIndex);
        else
            name = QStringLiteral("%qml-expression-entry");

        QQmlJS::AST::SourceElements *body;
        if (function)
            body = function->body ? function->body->elements : 0;
        else {
            // Synthesize source elements.
            QQmlJS::MemoryPool *pool = jsEngine->pool();

            QQmlJS::AST::Statement *stmt = node->statementCast();
            if (!stmt) {
                Q_ASSERT(node->expressionCast());
                QQmlJS::AST::ExpressionNode *expr = node->expressionCast();
                stmt = new (pool) QQmlJS::AST::ExpressionStatement(expr);
            }
            QQmlJS::AST::SourceElement *element = new (pool) QQmlJS::AST::StatementSourceElement(stmt);
            body = new (pool) QQmlJS::AST::SourceElements(element);
            body = body->finish();
        }

        _disableAcceleratedLookups = qmlFunction.disableAcceleratedLookups;
        int idx = defineFunction(name, node,
                                 function ? function->formals : 0,
                                 body);
        runtimeFunctionIndices[i] = idx;
    }

    qDeleteAll(_envMap);
    _envMap.clear();
    return runtimeFunctionIndices;
}

QQmlPropertyData *JSCodeGen::lookupQmlCompliantProperty(QQmlPropertyCache *cache, const QString &name, bool *propertyExistsButForceNameLookup)
{
    if (propertyExistsButForceNameLookup)
        *propertyExistsButForceNameLookup = false;
    QQmlPropertyData *pd = cache->property(name, /*object*/0, /*context*/0);

    // Q_INVOKABLEs can't be FINAL, so we have to look them up at run-time
    if (pd && pd->isFunction()) {
        if (propertyExistsButForceNameLookup)
            *propertyExistsButForceNameLookup = true;
        pd = 0;
    }

    if (pd && !cache->isAllowedInRevision(pd))
        pd = 0;

    // Return a copy allocated from our memory pool. Property data pointers can change
    // otherwise when the QQmlPropertyCache changes later in the QML type compilation process.
    if (pd) {
        QQmlPropertyData *copy = pd;
        pd = _function->New<QQmlPropertyData>();
        *pd = *copy;
    }
    return pd;
}

static void initMetaObjectResolver(IR::MemberExpressionResolver *resolver, QQmlPropertyCache *metaObject);

enum MetaObjectResolverFlags {
    AllPropertiesAreFinal      = 0x1,
    LookupsIncludeEnums        = 0x2,
    LookupsExcludeProperties   = 0x4,
    ResolveTypeInformationOnly = 0x8
};

static void initMetaObjectResolver(IR::MemberExpressionResolver *resolver, QQmlPropertyCache *metaObject);

static IR::Type resolveQmlType(QQmlEnginePrivate *qmlEngine, IR::MemberExpressionResolver *resolver, IR::Member *member)
{
    IR::Type result = IR::VarType;

    QQmlType *type = static_cast<QQmlType*>(resolver->data);

    if (member->name->constData()->isUpper()) {
        bool ok = false;
        int value = type->enumValue(*member->name, &ok);
        if (ok) {
            member->setEnumValue(value);
            resolver->clear();
            return IR::SInt32Type;
        }
    }

    if (type->isCompositeSingleton()) {
        QQmlTypeData *tdata = qmlEngine->typeLoader.getType(type->singletonInstanceInfo()->url);
        Q_ASSERT(tdata);
        Q_ASSERT(tdata->isComplete());
        initMetaObjectResolver(resolver, qmlEngine->propertyCacheForType(tdata->compiledData()->metaTypeId));
        resolver->flags |= AllPropertiesAreFinal;
        return resolver->resolveMember(qmlEngine, resolver, member);
    } else if (const QMetaObject *attachedMeta = type->attachedPropertiesType()) {
        QQmlPropertyCache *cache = qmlEngine->cache(attachedMeta);
        initMetaObjectResolver(resolver, cache);
        member->setAttachedPropertiesId(type->attachedPropertiesId());
        return resolver->resolveMember(qmlEngine, resolver, member);
    }

    resolver->clear();
    return result;
}

static void initQmlTypeResolver(IR::MemberExpressionResolver *resolver, QQmlType *qmlType)
{
    resolver->resolveMember = &resolveQmlType;
    resolver->data = qmlType;
    resolver->extraData = 0;
    resolver->flags = 0;
}

static IR::Type resolveImportNamespace(QQmlEnginePrivate *, IR::MemberExpressionResolver *resolver, IR::Member *member)
{
    IR::Type result = IR::VarType;
    QQmlTypeNameCache *typeNamespace = static_cast<QQmlTypeNameCache*>(resolver->extraData);
    void *importNamespace = resolver->data;

    QQmlTypeNameCache::Result r = typeNamespace->query(*member->name, importNamespace);
    if (r.isValid()) {
        member->freeOfSideEffects = true;
        if (r.scriptIndex != -1) {
            // TODO: remember the index and replace with subscript later.
            result = IR::VarType;
        } else if (r.type) {
            // TODO: Propagate singleton information, so that it is loaded
            // through the singleton getter in the run-time. Until then we
            // can't accelerate access :(
            if (!r.type->isSingleton()) {
                initQmlTypeResolver(resolver, r.type);
                return IR::QObjectType;
            }
        } else {
            Q_ASSERT(false); // How can this happen?
        }
    }

    resolver->clear();
    return result;
}

static void initImportNamespaceResolver(IR::MemberExpressionResolver *resolver, QQmlTypeNameCache *imports, const void *importNamespace)
{
    resolver->resolveMember = &resolveImportNamespace;
    resolver->data = const_cast<void*>(importNamespace);
    resolver->extraData = imports;
    resolver->flags = 0;
}

static IR::Type resolveMetaObjectProperty(QQmlEnginePrivate *qmlEngine, IR::MemberExpressionResolver *resolver, IR::Member *member)
{
    IR::Type result = IR::VarType;
    QQmlPropertyCache *metaObject = static_cast<QQmlPropertyCache*>(resolver->data);

    if (member->name->constData()->isUpper() && (resolver->flags & LookupsIncludeEnums)) {
        const QMetaObject *mo = metaObject->createMetaObject();
        QByteArray enumName = member->name->toUtf8();
        for (int ii = mo->enumeratorCount() - 1; ii >= 0; --ii) {
            QMetaEnum metaEnum = mo->enumerator(ii);
            bool ok;
            int value = metaEnum.keyToValue(enumName.constData(), &ok);
            if (ok) {
                member->setEnumValue(value);
                resolver->clear();
                return IR::SInt32Type;
            }
        }
    }

    if (qmlEngine && !(resolver->flags & LookupsExcludeProperties)) {
        QQmlPropertyData *property = member->property;
        if (!property && metaObject) {
            if (QQmlPropertyData *candidate = metaObject->property(*member->name, /*object*/0, /*context*/0)) {
                const bool isFinalProperty = (candidate->isFinal() || (resolver->flags & AllPropertiesAreFinal))
                                             && !candidate->isFunction();

                if (lookupHints()
                    && !(resolver->flags & AllPropertiesAreFinal)
                    && !candidate->isFinal()
                    && !candidate->isFunction()
                    && candidate->isDirect()) {
                    qWarning() << "Hint: Access to property" << *member->name << "of" << metaObject->className() << "could be accelerated if it was marked as FINAL";
                }

                if (isFinalProperty && metaObject->isAllowedInRevision(candidate)) {
                    property = candidate;
                    member->inhibitTypeConversionOnWrite = true;
                    if (!(resolver->flags & ResolveTypeInformationOnly))
                        member->property = candidate; // Cache for next iteration and isel needs it.
                }
            }
        }

        if (property) {
            // Enums cannot be mapped to IR types, they need to go through the run-time handling
            // of accepting strings that will then be converted to the right values.
            if (property->isEnum())
                return IR::VarType;

            switch (property->propType) {
            case QMetaType::Bool: result = IR::BoolType; break;
            case QMetaType::Int: result = IR::SInt32Type; break;
            case QMetaType::Double: result = IR::DoubleType; break;
            case QMetaType::QString: result = IR::StringType; break;
            default:
                if (property->isQObject()) {
                    if (QQmlPropertyCache *cache = qmlEngine->propertyCacheForType(property->propType)) {
                        initMetaObjectResolver(resolver, cache);
                        return IR::QObjectType;
                    }
                } else if (QQmlValueType *valueType = QQmlValueTypeFactory::valueType(property->propType)) {
                    if (QQmlPropertyCache *cache = qmlEngine->cache(valueType->metaObject())) {
                        initMetaObjectResolver(resolver, cache);
                        resolver->flags |= ResolveTypeInformationOnly;
                        return IR::QObjectType;
                    }
                }
                break;
            }
        }
    }
    resolver->clear();
    return result;
}

static void initMetaObjectResolver(IR::MemberExpressionResolver *resolver, QQmlPropertyCache *metaObject)
{
    resolver->resolveMember = &resolveMetaObjectProperty;
    resolver->data = metaObject;
    resolver->flags = 0;
    resolver->isQObjectResolver = true;
}

void JSCodeGen::beginFunctionBodyHook()
{
    _contextObjectTemp = _block->newTemp();
    _scopeObjectTemp = _block->newTemp();
    _importedScriptsTemp = _block->newTemp();
    _idArrayTemp = _block->newTemp();

    IR::Temp *temp = _block->TEMP(_contextObjectTemp);
    initMetaObjectResolver(&temp->memberResolver, _contextObject);
    move(temp, _block->NAME(IR::Name::builtin_qml_context_object, 0, 0));

    temp = _block->TEMP(_scopeObjectTemp);
    initMetaObjectResolver(&temp->memberResolver, _scopeObject);
    move(temp, _block->NAME(IR::Name::builtin_qml_scope_object, 0, 0));

    move(_block->TEMP(_importedScriptsTemp), _block->NAME(IR::Name::builtin_qml_imported_scripts_object, 0, 0));
    move(_block->TEMP(_idArrayTemp), _block->NAME(IR::Name::builtin_qml_id_array, 0, 0));
}

IR::Expr *JSCodeGen::fallbackNameLookup(const QString &name, int line, int col)
{
    if (_disableAcceleratedLookups)
        return 0;

    Q_UNUSED(line)
    Q_UNUSED(col)
    // Implement QML lookup semantics in the current file context.
    //
    // Note: We do not check if properties of the qml scope object or context object
    // are final. That's because QML tries to get as close as possible to lexical scoping,
    // which means in terms of properties that only those visible at compile time are chosen.
    // I.e. access to a "foo" property declared within the same QML component as "property int foo"
    // will always access that instance and as integer. If a sub-type implements its own property string foo,
    // then that one is not chosen for accesses from within this file, because it wasn't visible at compile
    // time. This corresponds to the logic in QQmlPropertyCache::findProperty to find the property associated
    // with the correct QML context.

    // Look for IDs first.
    foreach (const IdMapping &mapping, _idObjects)
        if (name == mapping.name) {
            _function->idObjectDependencies.insert(mapping.idIndex);
            IR::Expr *s = subscript(_block->TEMP(_idArrayTemp), _block->CONST(IR::SInt32Type, mapping.idIndex));
            IR::Temp *result = _block->TEMP(_block->newTemp());
            _block->MOVE(result, s);
            result = _block->TEMP(result->index);
            if (mapping.type) {
                initMetaObjectResolver(&result->memberResolver, mapping.type);
                result->memberResolver.flags |= AllPropertiesAreFinal;
            }
            result->isReadOnly = true; // don't allow use as lvalue
            return result;
        }

    {
        QQmlTypeNameCache::Result r = imports->query(name);
        if (r.isValid()) {
            if (r.scriptIndex != -1) {
                return subscript(_block->TEMP(_importedScriptsTemp), _block->CONST(IR::SInt32Type, r.scriptIndex));
            } else if (r.type) {
                IR::Name *typeName = _block->NAME(name, line, col);
                // Make sure the run-time loads this through the more efficient singleton getter.
                typeName->qmlSingleton = r.type->isCompositeSingleton();
                typeName->freeOfSideEffects = true;
                IR::Temp *result = _block->TEMP(_block->newTemp());
                _block->MOVE(result, typeName);

                result = _block->TEMP(result->index);
                initQmlTypeResolver(&result->memberResolver, r.type);
                return result;
            } else {
                Q_ASSERT(r.importNamespace);
                IR::Name *namespaceName = _block->NAME(name, line, col);
                namespaceName->freeOfSideEffects = true;
                IR::Temp *result = _block->TEMP(_block->newTemp());
                initImportNamespaceResolver(&result->memberResolver, imports, r.importNamespace);

                _block->MOVE(result, namespaceName);
                return _block->TEMP(result->index);
            }
        }
    }

    if (_scopeObject) {
        bool propertyExistsButForceNameLookup = false;
        QQmlPropertyData *pd = lookupQmlCompliantProperty(_scopeObject, name, &propertyExistsButForceNameLookup);
        if (propertyExistsButForceNameLookup)
            return 0;
        if (pd) {
            IR::Temp *base = _block->TEMP(_scopeObjectTemp);
            initMetaObjectResolver(&base->memberResolver, _scopeObject);
            return _block->MEMBER(base, _function->newString(name), pd, IR::Member::MemberOfQmlScopeObject);
        }
    }

    if (_contextObject) {
        bool propertyExistsButForceNameLookup = false;
        QQmlPropertyData *pd = lookupQmlCompliantProperty(_contextObject, name, &propertyExistsButForceNameLookup);
        if (propertyExistsButForceNameLookup)
            return 0;
        if (pd) {
            IR::Temp *base = _block->TEMP(_contextObjectTemp);
            initMetaObjectResolver(&base->memberResolver, _contextObject);
            return _block->MEMBER(base, _function->newString(name), pd, IR::Member::MemberOfQmlContextObject);
        }
    }

    // fall back to name lookup at run-time.
    return 0;
}

SignalHandlerConverter::SignalHandlerConverter(QQmlEnginePrivate *enginePrivate, ParsedQML *parsedQML,
                                               QQmlCompiledData *unit)
    : enginePrivate(enginePrivate)
    , parsedQML(parsedQML)
    , unit(unit)
    , illegalNames(QV8Engine::get(QQmlEnginePrivate::get(enginePrivate))->illegalNames())
{
}

bool SignalHandlerConverter::convertSignalHandlerExpressionsToFunctionDeclarations()
{
    for (int objectIndex = 0; objectIndex < parsedQML->objects.count(); ++objectIndex) {
        QmlObject * const obj = parsedQML->objects.at(objectIndex);
        QString elementName = stringAt(obj->inheritedTypeNameIndex);
        if (elementName.isEmpty())
            continue;
        QQmlCompiledData::TypeReference *tr = unit->resolvedTypes.value(obj->inheritedTypeNameIndex);
        QQmlCustomParser *customParser = (tr && tr->type) ? tr->type->customParser() : 0;
        if (customParser && !(customParser->flags() & QQmlCustomParser::AcceptsSignalHandlers))
            continue;
        QQmlPropertyCache *cache = unit->propertyCaches.value(objectIndex);
        if (!cache)
            continue;
        if (!convertSignalHandlerExpressionsToFunctionDeclarations(obj, elementName, cache))
            return false;
    }
    return true;
}

bool SignalHandlerConverter::convertSignalHandlerExpressionsToFunctionDeclarations(QmlObject *obj, const QString &typeName, QQmlPropertyCache *propertyCache)
{
    // map from signal name defined in qml itself to list of parameters
    QHash<QString, QStringList> customSignals;

    for (Binding *binding = obj->firstBinding(); binding; binding = binding->next) {
        QString propertyName = stringAt(binding->propertyNameIndex);
        // Attached property?
        if (binding->type == QV4::CompiledData::Binding::Type_AttachedProperty) {
            QmlObject *attachedObj = parsedQML->objects[binding->value.objectIndex];
            QQmlCompiledData::TypeReference *typeRef = unit->resolvedTypes.value(binding->propertyNameIndex);
            QQmlType *type = typeRef ? typeRef->type : 0;
            const QMetaObject *attachedType = type ? type->attachedPropertiesType() : 0;
            if (!attachedType)
                COMPILE_EXCEPTION(binding->location, tr("Non-existent attached object"));
            QQmlPropertyCache *cache = enginePrivate->cache(attachedType);
            if (!convertSignalHandlerExpressionsToFunctionDeclarations(attachedObj, propertyName, cache))
                return false;
            continue;
        }

        if (!QQmlCodeGenerator::isSignalPropertyName(propertyName))
            continue;

        PropertyResolver resolver(propertyCache);

        Q_ASSERT(propertyName.startsWith(QStringLiteral("on")));
        propertyName.remove(0, 2);

        // Note that the property name could start with any alpha or '_' or '$' character,
        // so we need to do the lower-casing of the first alpha character.
        for (int firstAlphaIndex = 0; firstAlphaIndex < propertyName.size(); ++firstAlphaIndex) {
            if (propertyName.at(firstAlphaIndex).isUpper()) {
                propertyName[firstAlphaIndex] = propertyName.at(firstAlphaIndex).toLower();
                break;
            }
        }

        QList<QString> parameters;

        bool notInRevision = false;
        QQmlPropertyData *signal = resolver.signal(propertyName, &notInRevision);
        if (signal) {
            int sigIndex = propertyCache->methodIndexToSignalIndex(signal->coreIndex);
            sigIndex = propertyCache->originalClone(sigIndex);

            bool unnamedParameter = false;

            QList<QByteArray> parameterNames = propertyCache->signalParameterNames(sigIndex);
            for (int i = 0; i < parameterNames.count(); ++i) {
                const QString param = QString::fromUtf8(parameterNames.at(i));
                if (param.isEmpty())
                    unnamedParameter = true;
                else if (unnamedParameter) {
                    COMPILE_EXCEPTION(binding->location, tr("Signal uses unnamed parameter followed by named parameter."));
                } else if (illegalNames.contains(param)) {
                    COMPILE_EXCEPTION(binding->location, tr("Signal parameter \"%1\" hides global variable.").arg(param));
                }
                parameters += param;
            }
        } else {
            if (notInRevision) {
                // Try assinging it as a property later
                if (resolver.property(propertyName, /*notInRevision ptr*/0))
                    continue;

                const QString &originalPropertyName = stringAt(binding->propertyNameIndex);

                QQmlCompiledData::TypeReference *typeRef = unit->resolvedTypes.value(obj->inheritedTypeNameIndex);
                const QQmlType *type = typeRef ? typeRef->type : 0;
                if (type) {
                    COMPILE_EXCEPTION(binding->location, tr("\"%1.%2\" is not available in %3 %4.%5.").arg(typeName).arg(originalPropertyName).arg(type->module()).arg(type->majorVersion()).arg(type->minorVersion()));
                } else {
                    COMPILE_EXCEPTION(binding->location, tr("\"%1.%2\" is not available due to component versioning.").arg(typeName).arg(originalPropertyName));
                }
            }

            // Try to look up the signal parameter names in the object itself

            // build cache if necessary
            if (customSignals.isEmpty()) {
                for (const Signal *signal = obj->firstSignal(); signal; signal = signal->next) {
                    const QString &signalName = stringAt(signal->nameIndex);
                    customSignals.insert(signalName, signal->parameterStringList(parsedQML->jsGenerator.strings));
                }

                for (const QmlProperty *property = obj->firstProperty(); property; property = property->next) {
                    const QString propName = stringAt(property->nameIndex);
                    customSignals.insert(propName, QStringList());
                }
            }

            QHash<QString, QStringList>::ConstIterator entry = customSignals.find(propertyName);
            if (entry == customSignals.constEnd() && propertyName.endsWith(QStringLiteral("Changed"))) {
                QString alternateName = propertyName.mid(0, propertyName.length() - static_cast<int>(strlen("Changed")));
                entry = customSignals.find(alternateName);
            }

            if (entry == customSignals.constEnd()) {
                // Can't find even a custom signal, then just don't do anything and try
                // keeping the binding as a regular property assignment.
                continue;
            }

            parameters = entry.value();
        }

        binding->propertyNameIndex = parsedQML->jsGenerator.registerString(propertyName);

        // Binding object to signal means connect the signal to the object's default method.
        if (binding->type == QV4::CompiledData::Binding::Type_Object) {
            binding->flags |= QV4::CompiledData::Binding::IsSignalHandlerObject;
            continue;
        }

        if (binding->type != QV4::CompiledData::Binding::Type_Script) {
            if (binding->type < QV4::CompiledData::Binding::Type_Script) {
                COMPILE_EXCEPTION(binding->location, tr("Cannot assign a value to a signal (expecting a script to be run)"));
            } else {
                COMPILE_EXCEPTION(binding->location, tr("Incorrectly specified signal assignment"));
            }
        }

        QQmlJS::Engine &jsEngine = parsedQML->jsParserEngine;
        QQmlJS::MemoryPool *pool = jsEngine.pool();

        QQmlJS::AST::FormalParameterList *paramList = 0;
        foreach (const QString &param, parameters) {
            QStringRef paramNameRef = jsEngine.newStringRef(param);

            if (paramList)
                paramList = new (pool) QQmlJS::AST::FormalParameterList(paramList, paramNameRef);
            else
                paramList = new (pool) QQmlJS::AST::FormalParameterList(paramNameRef);
        }

        if (paramList)
            paramList = paramList->finish();

        CompiledFunctionOrExpression *foe = obj->functionsAndExpressions->slowAt(binding->value.compiledScriptIndex);
        QQmlJS::AST::Statement *statement = static_cast<QQmlJS::AST::Statement*>(foe->node);
        QQmlJS::AST::SourceElement *sourceElement = new (pool) QQmlJS::AST::StatementSourceElement(statement);
        QQmlJS::AST::SourceElements *elements = new (pool) QQmlJS::AST::SourceElements(sourceElement);
        elements = elements->finish();

        QQmlJS::AST::FunctionBody *body = new (pool) QQmlJS::AST::FunctionBody(elements);

        QQmlJS::AST::FunctionDeclaration *functionDeclaration = new (pool) QQmlJS::AST::FunctionDeclaration(jsEngine.newStringRef(propertyName), paramList, body);

        foe->node = functionDeclaration;
        binding->flags |= QV4::CompiledData::Binding::IsSignalHandlerExpression;
    }
    return true;
}

void SignalHandlerConverter::recordError(const QV4::CompiledData::Location &location, const QString &description)
{
    QQmlError error;
    error.setUrl(unit->url);
    error.setLine(location.line);
    error.setColumn(location.column);
    error.setDescription(description);
    errors << error;
}

QQmlPropertyData *PropertyResolver::property(const QString &name, bool *notInRevision, QObject *object, QQmlContextData *context)
{
    if (notInRevision) *notInRevision = false;

    QQmlPropertyData *d = cache->property(name, object, context);

    // Find the first property
    while (d && d->isFunction())
        d = cache->overrideData(d);

    if (d && !cache->isAllowedInRevision(d)) {
        if (notInRevision) *notInRevision = true;
        return 0;
    } else {
        return d;
    }
}


QQmlPropertyData *PropertyResolver::signal(const QString &name, bool *notInRevision, QObject *object, QQmlContextData *context)
{
    if (notInRevision) *notInRevision = false;

    QQmlPropertyData *d = cache->property(name, object, context);
    if (notInRevision) *notInRevision = false;

    while (d && !(d->isFunction()))
        d = cache->overrideData(d);

    if (d && !cache->isAllowedInRevision(d)) {
        if (notInRevision) *notInRevision = true;
        return 0;
    } else if (d && d->isSignal()) {
        return d;
    }

    if (name.endsWith(QStringLiteral("Changed"))) {
        QString propName = name.mid(0, name.length() - static_cast<int>(strlen("Changed")));

        d = property(propName, notInRevision, object, context);
        if (d)
            return cache->signal(d->notifyIndex);
    }

    return 0;
}
