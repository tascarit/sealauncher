import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Effects
import QtQuick.Layouts

Item {
    id: root
    anchors.fill: parent
    visible: opacity > 0.0
    opacity: 0.0
    z: 600

    // === Публичный API ===
    property string title: "Ошибка"
    property string message: "Что-то пошло не так."
    property string details: ""                    // разворачиваемая стопка
    property bool detailsExpanded: false
    property bool canRetry: false

    signal closed()
    signal retried()

    function show(t, m, d, retry) {
        title = t
        message = m
        details = d || ""
        canRetry = retry || false
        detailsExpanded = false
        opacity = 1.0
    }

    function hide() { opacity = 0.0 }

    Behavior on opacity { NumberAnimation { duration: 200 } }

    // Затемнение
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.65)

        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
    }

    // Карточка
    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(520, root.width - 60)
        height: dialogColumn.implicitHeight + 48
        radius: 12
        color: Qt.rgba(0.10, 0.07, 0.07, 0.95)
        border.color: Qt.rgba(1, 0.35, 0.35, 0.25)
        border.width: 1

        scale: root.opacity
        Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

        ColumnLayout {
            id: dialogColumn
            anchors.fill: parent
            anchors.margins: 24
            spacing: 14

            // Заголовок с иконкой
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    radius: 16
                    color: Qt.rgba(1, 0.4, 0.4, 0.15)
                    border.color: Qt.rgba(1, 0.4, 0.4, 0.4)
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "!"
                        color: "#FF9B9B"
                        font.pixelSize: 18
                        font.bold: true
                    }
                }

                Text {
                    text: root.title
                    color: "#FFFFFF"
                    font.pixelSize: 16
                    font.bold: true
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Qt.rgba(1, 1, 1, 0.08)
            }

            // Сообщение
            Text {
                text: root.message
                color: Qt.rgba(1, 1, 1, 0.8)
                font.pixelSize: 13
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            // Кнопка "Подробности"
            Button {
                id: detailsBtn
                visible: root.details.length > 0
                Layout.preferredHeight: 26
                Layout.preferredWidth: 120
                hoverEnabled: true
                flat: true
                padding: 0

                contentItem: RowLayout {
                    spacing: 6
                    anchors.centerIn: parent

                    Text {
                        text: root.detailsExpanded ? "▼" : "▶"
                        color: Qt.rgba(1, 1, 1, 0.5)
                        font.pixelSize: 9
                    }
                    Text {
                        text: "Подробности"
                        color: detailsBtn.hovered ? "#FFF0B5" : Qt.rgba(1, 1, 1, 0.5)
                        font.pixelSize: 12
                        Behavior on color { ColorAnimation { duration: 120 } }
                    }
                }

                background: Rectangle { color: "transparent" }
                onClicked: root.detailsExpanded = !root.detailsExpanded
            }

            // Раскрывающийся блок с деталями
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.detailsExpanded
                    ? Math.min(140, detailsText.implicitHeight + 20)
                    : 0
                visible: Layout.preferredHeight > 0
                clip: true
                radius: 6
                color: Qt.rgba(0, 0, 0, 0.35)
                border.color: Qt.rgba(1, 1, 1, 0.06)
                border.width: 1

                Behavior on Layout.preferredHeight {
                    NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
                }

                Flickable {
                    anchors.fill: parent
                    anchors.margins: 10
                    contentWidth: width
                    contentHeight: detailsText.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Text {
                        id: detailsText
                        width: parent.width
                        text: root.details
                        color: Qt.rgba(1, 1, 1, 0.55)
                        font.pixelSize: 11
                        font.family: "Consolas"
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }

            // Кнопки действий
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 10

                Item { Layout.fillWidth: true }

                // "Закрыть"
                Button {
                    id: closeBtn
                    Layout.preferredWidth: 130
                    Layout.preferredHeight: 36
                    hoverEnabled: true
                    flat: true
                    padding: 0

                    contentItem: Text {
                        text: "Закрыть"
                        color: closeBtn.hovered ? "#FFFFFF" : Qt.rgba(1, 1, 1, 0.7)
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        Behavior on color { ColorAnimation { duration: 120 } }
                    }

                    background: Rectangle {
                        radius: 6
                        color: closeBtn.down
                            ? Qt.rgba(1, 1, 1, 0.18)
                            : (closeBtn.hovered ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.04))
                        border.color: Qt.rgba(1, 1, 1, 0.10)
                        border.width: 1
                        Behavior on color { ColorAnimation { duration: 120 } }
                    }

                    onClicked: { root.hide(); root.closed() }
                }

                // "Повторить"
                Button {
                    id: retryBtn
                    visible: root.canRetry
                    Layout.preferredWidth: 140
                    Layout.preferredHeight: 36
                    hoverEnabled: true
                    flat: true
                    padding: 0

                    contentItem: Text {
                        text: "Повторить"
                        color: "#575757"
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 6
                        color: retryBtn.down
                            ? "#BDB386"
                            : (retryBtn.hovered ? "#FFF8DB" : "#FFF1BA")
                        border.color: retryBtn.hovered ? "#FFFFFF" : "transparent"
                        border.width: 1
                        Behavior on color { ColorAnimation { duration: 100 } }
                    }

                    onClicked: { root.hide(); root.retried() }
                }
            }
        }
    }
}