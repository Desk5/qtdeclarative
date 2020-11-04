/****************************************************************************
**
** Copyright (C) 2020 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtQuick module of the Qt Toolkit.
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
#include "qquickcolorgroup_p.h"

#include <QScopeGuard>

#include <QtQuick/private/qquickabstractpaletteprovider_p.h>
#include <QtQuick/private/qquickpalette_p.h>
#include <QtQuick/private/qquickpalettecolorprovider_p.h>

QT_BEGIN_NAMESPACE

/*!
    \class QQuickColorGroup
    \brief The QQuickColorGroup class represents a set of colors.
    \inmodule QtQuick
    \since 6.0

    Used by QQuickPalette to provide different groups of colors by roles.

    \sa QPalette::ColorRole
    //! internal \sa QQuickPalette, QQuickAbstractPaletteProvider
 */

/*!
    \qmltype ColorGroup
    \instantiates QQuickColorGroup
    \inherits QtObject
    \inqmlmodule QtQuick
    \ingroup qtquick-visual
    \brief The set of colors by roles.

    The ColorGroup type is used to define a set of colors with certain roles. Although a ColorGroup
    has no visual appearance, it defines colors used to customize rendered items.

    Default values of colors are equal to active group colors of default-constructed \c QPalette.

    The following code can be used to create a color group with some colors specified:

    \code
    ColorGroup {
        alternateBase: "red"
        base: "green"
    }
    \endcode
 */

/*!
    \qmlproperty color QtQuick::ColorGroup::alternateBase

    Used as the alternate background color in item views with alternating row colors.
*/

/*!
    \qmlproperty color QtQuick::ColorGroup::base

    Used mostly as the background color for text editor controls and item views.
    It is usually white or another light color.
*/

/*!
    \qmlproperty color QtQuick::ColorGroup::brightText

    A text color that is very different from \c windowText, and contrasts
    well with e.g. \c dark. Typically used for text that needs to be drawn
    where \c text, \c windowText or \c buttonText would
    give poor contrast, such as on highlighted buttons.
*/

/*!
    \qmlproperty color QtQuick::ColorGroup::button

    The general button background color. This background can be different from
    \c window as some styles require a different background color for buttons.
*/

/*!
    \qmlproperty color QtQuick::ColorGroup::buttonText

    A foreground color used with the \c palette color.
*/

/*!
    \qmlproperty color QtQuick::ColorGroup::dark

    A foreground color used with the \c palette color.
*/

/*!
    \qmlproperty color QtQuick::ColorGroup::highlight

    A color to indicate a selected item or the current item.
*/

/*!
    \qmlproperty color QtQuick::ColorGroup::highlightedText

    A text color that contrasts with \c highlight.
*/

/*!
    \qmlproperty color QtQuick::ColorGroup::light

    Lighter than \c button.
*/

/*!
    \qmlproperty color QtQuick::ColorGroup::link

    A text color used for hyperlinks.
*/

/*!
    \qmlproperty color QtQuick::ColorGroup::linkVisited

    A text color used for already visited hyperlinks.
*/

/*!
    \qmlproperty color QtQuick::ColorGroup::mid

    Between \c palette.button and \c dark.
*/

/*!
    \qmlproperty color QtQuick::ColorGroup::midlight

    Between \c button and \c light.
*/

/*!
    \qmlproperty color QtQuick::ColorGroup::shadow

    A very dark color.
*/

/*!
    \qmlproperty color QtQuick::ColorGroup::text

    The foreground color used with \c base. This is usually the same as
    the \c windowText, in which case it must provide good contrast with
    \c window and \c base.
*/

/*!
    \qmlproperty color QtQuick::ColorGroup::toolTipBase

    Used as the background color for tooltips.
*/

/*!
    \qmlproperty color QtQuick::ColorGroup::toolTipText

    Used as the foreground color for tooltips.
*/

/*!
    \qmlproperty color QtQuick::ColorGroup::window

    A general background color.
*/

/*!
    \qmlproperty color QtQuick::ColorGroup::windowText

    A general foreground color.
*/

/*!
    \qmlsignal QtQuick::ColorGroup::changed

    Additional signal indicates that the current state of this color group
    has been changed. Usually it means that one of the colors is changed.

    \sa Palette
*/

/*!
    Construct object in default state.
 */
QQuickColorGroup::QQuickColorGroup(QQuickPalette &parent)
    : QObject(&parent)
    , m_groupTag(defaultGroupTag())
    , m_colorProvider(parent.colorProvider().shared_from_this())
{
}

QPalette::ColorGroup QQuickColorGroup::currentColorGroup() const
{
    return groupTag();
}

QColor QQuickColorGroup::alternateBase() const
{
    return color(QPalette::AlternateBase);
}

void QQuickColorGroup::setAlternateBase(const QColor &color)
{
    setColor(QPalette::AlternateBase, color, &QQuickColorGroup::alternateBaseChanged);
}

void QQuickColorGroup::resetAlternateBase()
{
    resetColor(QPalette::AlternateBase, &QQuickColorGroup::alternateBaseChanged);
}

QColor QQuickColorGroup::base() const
{
    return color(QPalette::Base);
}

void QQuickColorGroup::setBase(const QColor &color)
{
    setColor(QPalette::Base, color, &QQuickColorGroup::baseChanged);
}

void QQuickColorGroup::resetBase()
{
    resetColor(QPalette::Base, &QQuickColorGroup::baseChanged);
}

QColor QQuickColorGroup::brightText() const
{
    return color(QPalette::BrightText);
}

void QQuickColorGroup::setBrightText(const QColor &color)
{
    setColor(QPalette::BrightText, color, &QQuickColorGroup::brightTextChanged);
}

void QQuickColorGroup::resetBrightText()
{
    resetColor(QPalette::BrightText, &QQuickColorGroup::brightTextChanged);
}

QColor QQuickColorGroup::button() const
{
    return color(QPalette::Button);
}

void QQuickColorGroup::setButton(const QColor &color)
{
    setColor(QPalette::Button, color, &QQuickColorGroup::buttonChanged);
}

void QQuickColorGroup::resetButton()
{
    resetColor(QPalette::Button, &QQuickColorGroup::buttonChanged);
}

QColor QQuickColorGroup::buttonText() const
{
    return color(QPalette::ButtonText);
}

void QQuickColorGroup::setButtonText(const QColor &color)
{
    setColor(QPalette::ButtonText, color, &QQuickColorGroup::buttonTextChanged);
}

void QQuickColorGroup::resetButtonText()
{
    resetColor(QPalette::ButtonText, &QQuickColorGroup::buttonTextChanged);
}

QColor QQuickColorGroup::dark() const
{
    return color(QPalette::Dark);
}

void QQuickColorGroup::setDark(const QColor &color)
{
    setColor(QPalette::Dark, color, &QQuickColorGroup::darkChanged);
}

void QQuickColorGroup::resetDark()
{
    resetColor(QPalette::Dark, &QQuickColorGroup::darkChanged);
}

QColor QQuickColorGroup::highlight() const
{
    return color(QPalette::Highlight);
}

void QQuickColorGroup::setHighlight(const QColor &color)
{
    setColor(QPalette::Highlight, color, &QQuickColorGroup::highlightChanged);
}

void QQuickColorGroup::resetHighlight()
{
    resetColor(QPalette::Highlight, &QQuickColorGroup::highlightChanged);
}

QColor QQuickColorGroup::highlightedText() const
{
    return color(QPalette::HighlightedText);
}

void QQuickColorGroup::setHighlightedText(const QColor &color)
{
    setColor(QPalette::HighlightedText, color, &QQuickColorGroup::highlightedTextChanged);
}

void QQuickColorGroup::resetHighlightedText()
{
    resetColor(QPalette::HighlightedText, &QQuickColorGroup::highlightedTextChanged);
}

QColor QQuickColorGroup::light() const
{
    return color(QPalette::Light);
}

void QQuickColorGroup::setLight(const QColor &color)
{
    setColor(QPalette::Light, color, &QQuickColorGroup::lightChanged);
}

void QQuickColorGroup::resetLight()
{
    resetColor(QPalette::Light, &QQuickColorGroup::lightChanged);
}

QColor QQuickColorGroup::link() const
{
    return color(QPalette::Link);
}

void QQuickColorGroup::setLink(const QColor &color)
{
    setColor(QPalette::Link, color, &QQuickColorGroup::linkChanged);
}

void QQuickColorGroup::resetLink()
{
    resetColor(QPalette::Link, &QQuickColorGroup::linkChanged);
}

QColor QQuickColorGroup::linkVisited() const
{
    return color(QPalette::LinkVisited);
}

void QQuickColorGroup::setLinkVisited(const QColor &color)
{
    setColor(QPalette::LinkVisited, color, &QQuickColorGroup::linkVisitedChanged);
}

void QQuickColorGroup::resetLinkVisited()
{
    resetColor(QPalette::LinkVisited, &QQuickColorGroup::linkVisitedChanged);
}

QColor QQuickColorGroup::mid() const
{
    return color(QPalette::Mid);
}

void QQuickColorGroup::setMid(const QColor &color)
{
    setColor(QPalette::Mid, color, &QQuickColorGroup::midChanged);
}

void QQuickColorGroup::resetMid()
{
    resetColor(QPalette::Mid, &QQuickColorGroup::midChanged);
}

QColor QQuickColorGroup::midlight() const
{
    return color(QPalette::Midlight);
}

void QQuickColorGroup::setMidlight(const QColor &color)
{
    setColor(QPalette::Midlight, color, &QQuickColorGroup::midlightChanged);
}

void QQuickColorGroup::resetMidlight()
{
    resetColor(QPalette::Midlight, &QQuickColorGroup::midlightChanged);
}

QColor QQuickColorGroup::shadow() const
{
    return color(QPalette::Shadow);
}

void QQuickColorGroup::setShadow(const QColor &color)
{
    setColor(QPalette::Shadow, color, &QQuickColorGroup::shadowChanged);
}

void QQuickColorGroup::resetShadow()
{
    resetColor(QPalette::Shadow, &QQuickColorGroup::shadowChanged);
}

QColor QQuickColorGroup::text() const
{
    return color(QPalette::Text);
}

void QQuickColorGroup::setText(const QColor &color)
{
    setColor(QPalette::Text, color, &QQuickColorGroup::textChanged);
}

void QQuickColorGroup::resetText()
{
    resetColor(QPalette::Text, &QQuickColorGroup::textChanged);
}

QColor QQuickColorGroup::toolTipBase() const
{
    return color(QPalette::ToolTipBase);
}

void QQuickColorGroup::setToolTipBase(const QColor &color)
{
    setColor(QPalette::ToolTipBase, color, &QQuickColorGroup::toolTipBaseChanged);
}

void QQuickColorGroup::resetToolTipBase()
{
    resetColor(QPalette::ToolTipBase, &QQuickColorGroup::toolTipBaseChanged);
}

QColor QQuickColorGroup::toolTipText() const
{
    return color(QPalette::ToolTipText);
}

void QQuickColorGroup::setToolTipText(const QColor &color)
{
    setColor(QPalette::ToolTipText, color, &QQuickColorGroup::toolTipTextChanged);
}

void QQuickColorGroup::resetToolTipText()
{
    resetColor(QPalette::ToolTipText, &QQuickColorGroup::toolTipTextChanged);
}

QColor QQuickColorGroup::window() const
{
    return color(QPalette::Window);
}

void QQuickColorGroup::setWindow(const QColor &color)
{
    setColor(QPalette::Window, color, &QQuickColorGroup::windowChanged);
}

void QQuickColorGroup::resetWindow()
{
    resetColor(QPalette::Window, &QQuickColorGroup::windowChanged);
}

QColor QQuickColorGroup::windowText() const
{
    return color(QPalette::WindowText);
}

void QQuickColorGroup::setWindowText(const QColor &color)
{
    setColor(QPalette::WindowText, color, &QQuickColorGroup::windowTextChanged);
}

void QQuickColorGroup::resetWindowText()
{
    resetColor(QPalette::WindowText, &QQuickColorGroup::windowTextChanged);
}

QPalette::ColorGroup QQuickColorGroup::groupTag() const
{
    return m_groupTag;
}

QQuickColorGroup::QQuickColorGroup(QObject *parent)
    : QObject(parent)
    , m_groupTag(defaultGroupTag())
    , m_colorProvider(std::make_shared<QQuickPaletteColorProvider>())
{
}

void QQuickColorGroup::setGroupTag(QPalette::ColorGroup tag)
{
    if (m_groupTag != tag) {
        m_groupTag = tag;
        Q_EMIT changed();
    }
}

const QQuickPaletteColorProvider &QQuickColorGroup::colorProvider() const
{
    Q_ASSERT(m_colorProvider);
    return *m_colorProvider;
}

QQuickPaletteColorProvider &QQuickColorGroup::colorProvider()
{
    return const_cast<QQuickPaletteColorProvider &>(
                const_cast<const QQuickColorGroup*>(this)->colorProvider());
}

QQuickColorGroup *QQuickColorGroup::createWithParent(QQuickPalette &parent)
{
    return new QQuickColorGroup(parent);
}

QColor QQuickColorGroup::color(QPalette::ColorRole role) const
{
    return colorProvider().color(currentColorGroup(), role);
}

void QQuickColorGroup::setColor(QPalette::ColorRole role, QColor color, Notifier notifier)
{
    if (colorProvider().setColor(groupTag(), role, color)) {
        Q_EMIT (this->*notifier)();
        Q_EMIT changed();
    }
}

void QQuickColorGroup::resetColor(QPalette::ColorRole role, Notifier notifier)
{
    if (colorProvider().resetColor(groupTag(), role)) {
        Q_EMIT (this->*notifier)();
        Q_EMIT changed();
    }
}

QT_END_NAMESPACE
