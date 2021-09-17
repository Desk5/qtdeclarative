/****************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the tools applications of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qmltccodewriter.h"

#include <QtCore/qfileinfo.h>
#include <QtCore/qstringbuilder.h>

QT_BEGIN_NAMESPACE

static QString urlToMacro(const QString &url)
{
    QFileInfo fi(url);
    return u"Q_QMLTC_" + fi.baseName().toUpper();
}

void QmltcCodeWriter::writeGlobalHeader(QmltcOutputWrapper &code, const QString &sourcePath,
                                        const QString &hPath, const QString &cppPath,
                                        const QSet<QString> &requiredCppIncludes)
{
    Q_UNUSED(cppPath);
    const QString preamble = u"// This code is auto-generated by the qmltc tool from the file '"
            + sourcePath + u"'\n// WARNING! All changes made in this file will be lost!\n";
    code.rawAppendToHeader(preamble);
    code.rawAppendToCpp(preamble);
    code.rawAppendToHeader(
            u"// NOTE: This generated API is to be considered implementation detail.");
    code.rawAppendToHeader(
            u"//       It may change from version to version and should not be relied upon.");

    const QString headerMacro = urlToMacro(sourcePath);
    code.rawAppendToHeader(u"#ifndef %1_H"_qs.arg(headerMacro));
    code.rawAppendToHeader(u"#define %1_H"_qs.arg(headerMacro));

    code.rawAppendToHeader(u"#include <QtCore/qproperty.h>");
    code.rawAppendToHeader(u"#include <QtCore/qobject.h>");
    code.rawAppendToHeader(u"#include <QtCore/qcoreapplication.h>");
    code.rawAppendToHeader(u"#include <QtQml/qqmlengine.h>");
    code.rawAppendToHeader(u"#include <QtCore/qurl.h>"); // used in engine execution
    code.rawAppendToHeader(u"#include <QtQml/qqml.h>"); // used for attached properties

    code.rawAppendToHeader(u"#include <private/qqmlengine_p.h>"); // executeRuntimeFunction(), etc.

    code.rawAppendToHeader(u"#include <QtQml/qqmllist.h>"); // QQmlListProperty

    // include custom C++ includes required by used types
    code.rawAppendToHeader(u"// BEGIN(custom_cpp_includes)");
    for (const auto &requiredInclude : requiredCppIncludes)
        code.rawAppendToHeader(u"#include \"" + requiredInclude + u"\"");
    code.rawAppendToHeader(u"// END(custom_cpp_includes)");

    code.rawAppendToCpp(u"#include \"" + hPath + u"\""); // include own .h file

    code.rawAppendToCpp(u""); // blank line
    code.rawAppendToCpp(u"#include <private/qobject_p.h>"); // NB: for private properties

    code.rawAppendToHeader(u""); // blank line
    code.rawAppendToHeader(u"namespace q_qmltc {");
    code.rawAppendToCpp(u""); // blank line
    code.rawAppendToCpp(u"namespace q_qmltc {");
}

void QmltcCodeWriter::writeGlobalFooter(QmltcOutputWrapper &code, const QString &sourcePath)
{
    code.rawAppendToCpp(u"} // namespace q_qmltc");
    code.rawAppendToCpp(u""); // blank line

    code.rawAppendToHeader(u"} // namespace q_qmltc");
    code.rawAppendToHeader(u""); // blank line
    code.rawAppendToHeader(u"#endif // %1_H"_qs.arg(urlToMacro(sourcePath)));
    code.rawAppendToHeader(u""); // blank line
}

static void writeToFile(const QString &path, const QByteArray &data)
{
    // When not using dependency files, changing a single qml invalidates all
    // qml files and would force the recompilation of everything. To avoid that,
    // we check if the data is equal to the existing file, if yes, don't touch
    // it so the build system will not recompile unnecessary things.
    //
    // If the build system use dependency file, we should anyway touch the file
    // so qmltc is not re-run
    QFileInfo fi(path);
    if (fi.exists() && fi.size() == data.size()) {
        QFile oldFile(path);
        oldFile.open(QIODevice::ReadOnly);
        if (oldFile.readAll() == data)
            return;
    }
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(data);
}

void QmltcCodeWriter::write(QmltcOutputWrapper &code, const QmltcProgram &program)
{
    writeGlobalHeader(code, program.url, program.hPath, program.cppPath, program.includes);
    code.rawAppendToHeader(u"/* QMLTC: NOT IMPLEMENTED */");
    code.rawAppendToCpp(u"/* QMLTC: NOT IMPLEMENTED */");
    writeGlobalFooter(code, program.url);

    writeToFile(program.hPath, code.code().header.toUtf8());
    writeToFile(program.cppPath, code.code().cpp.toUtf8());
}

QT_END_NAMESPACE
