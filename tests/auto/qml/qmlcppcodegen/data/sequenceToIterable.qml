pragma Strict
import QtQuick
import TestTypes

Item {
    Component.onCompleted: () => {
        repeater.model = EntrySource.getEntries()
    }

    Repeater {
        id: repeater
        Item {
            required property int index
            required property QtObject modelData
        }
    }

    property int c: children.length
}
