/****************************************************************************
**
** Copyright (C) 2016 Research In Motion.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtQml module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QQMLAPPLICATIONENGINE_H
#define QQMLAPPLICATIONENGINE_H

#include <QtQml/qqmlengine.h>

#include <QtCore/qurl.h>
#include <QtCore/qobject.h>
#include <QtCore/qlist.h>

QT_BEGIN_NAMESPACE

class QQmlApplicationEnginePrivate;
class Q_QML_EXPORT QQmlApplicationEngine : public QQmlEngine
{
    Q_OBJECT
public:
    QQmlApplicationEngine(QObject *parent = nullptr);
    QQmlApplicationEngine(const QUrl &url, QObject *parent = nullptr);
    QQmlApplicationEngine(const QString &filePath, QObject *parent = nullptr);
    ~QQmlApplicationEngine();

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QList<QObject*> rootObjects(); // ### Qt 6: remove
#endif
    QList<QObject*> rootObjects() const;

public Q_SLOTS:
    void load(const QUrl &url);
    void load(const QString &filePath);
    void loadData(const QByteArray &data, const QUrl &url = QUrl());

Q_SIGNALS:
    void objectCreated(QObject *object, const QUrl &url);

private:
    Q_DISABLE_COPY(QQmlApplicationEngine)
    Q_DECLARE_PRIVATE(QQmlApplicationEngine)
};

QT_END_NAMESPACE

#endif
