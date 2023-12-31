// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

//! [document]
import QtQuick 2.0

Item {
    width: 200; height: 250

    //! [model]
    ListModel {
        id: fruitModel

        ListElement {
            name: "Apple"
            cost: 2.45
        }
        ListElement {
            name: "Orange"
            cost: 3.25
        }
        ListElement {
            name: "Banana"
            cost: 1.95
        }
    }
    //! [model]

    //! [view]
    ListView {
        anchors.fill: parent
        model: fruitModel
        delegate: Row {
            id: delegate
            required property string name
            required property real cost

            Text { text: "Fruit: " + delegate.name }
            Text { text: "Cost: $" + delegate.cost }
        }
    }
    //! [view]
}
//! [document]
