import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Effects
import QtQuick.Layouts

Item {
    id: root
    anchors.fill: parent
    visible: opacity > 0.0
    opacity: 0.0
    z: 1000

    property string title: "Установка сборки"
    property string stage: "Подготовка..."
    property string details: ""
    property real progress: 0.0
    property bool indeterminate: false
    property bool cancellable: true

    signal cancelled()

    function open()  { opacity = 1.0 }
    function close() { opacity = 0.0 }

    Behavior on opacity { NumberAnimation { duration: 200 } }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.55)
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
    }

    Rectangle {
        id: card
        width: Math.min(560, root.width - 80)
        height: 240
        anchors.centerIn: parent
        radius: 14
        color: Qt.rgba(0.07, 0.07, 0.09, 0.96)
        border.color: Qt.rgba(1, 1, 1, 0.08)
        border.width: 1


        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowBlur: 1.0
            shadowColor: Qt.rgba(0, 0, 0, 0.6)
            shadowVerticalOffset: 8
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            anchors.topMargin: 22
            anchors.bottomMargin: 22
            spacing: 0

            Text {
                text: root.title
                color: "#FFFFFF"
                font.pixelSize: 17
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            Item { Layout.preferredHeight: 12 }

            Text {
                text: root.stage
                color: Qt.rgba(1, 1, 1, 0.8)
                font.pixelSize: 13
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            Item { Layout.preferredHeight: 6 }

            Text {
                text: root.details.length > 0 ? root.details : " "
                color: Qt.rgba(1, 1, 1, 0.4)
                font.pixelSize: 11
                Layout.fillWidth: true
                elide: Text.ElideMiddle
            }

            Item { Layout.preferredHeight: 18 }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 8
                clip: true

                Rectangle {
                    anchors.fill: parent
                    radius: 4
                    color: Qt.rgba(1, 1, 1, 0.08)
                }

                Rectangle {
                    visible: !root.indeterminate
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.width * Math.max(0, Math.min(1, root.progress))
                    radius: 4
                    color: "#FFF0B5"

                    Behavior on width {
                        NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
                    }
                }

                Rectangle {
                    visible: root.indeterminate
                    width: parent.width * 0.35
                    height: parent.height
                    radius: 4
                    color: "#FFF0B5"

                    SequentialAnimation on x {
                        running: root.indeterminate && root.visible
                        loops: Animation.Infinite

                        NumberAnimation {
                            from: -parent.width * 0.35
                            to: parent.width
                            duration: 1300
                            easing.type: Easing.InOutQuad
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: 10 }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    visible: !root.indeterminate
                    text: Math.round(root.progress * 100) + "%"
                    color: "#FFF0B5"
                    font.pixelSize: 12
                    font.bold: true
                }
                Text {
                    visible: root.indeterminate
                    text: "Подождите..."
                    color: Qt.rgba(1, 1, 1, 0.4)
                    font.pixelSize: 11
                }

                Item { Layout.fillWidth: true }
            }

            Item { Layout.fillHeight: true }

            Button {
                id: cancelBtn
                Layout.alignment: Qt.AlignRight
                Layout.preferredWidth: 130
                Layout.preferredHeight: 36
                hoverEnabled: true
                flat: true
                padding: 0
                visible: false

                contentItem: Text {
                    text: "Отмена"
                    color: cancelBtn.hovered ? "#FFFFFF" : Qt.rgba(1, 1, 1, 0.7)
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    Behavior on color { ColorAnimation { duration: 120 } }
                }

                background: Rectangle {
                    radius: 6
                    color: cancelBtn.down
                        ? Qt.rgba(1, 1, 1, 0.18)
                        : (cancelBtn.hovered ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.04))
                    border.color: Qt.rgba(1, 1, 1, 0.10)
                    border.width: 1
                    Behavior on color { ColorAnimation { duration: 120 } }
                }

                onClicked: root.cancelled()
            }
        }
    }
}