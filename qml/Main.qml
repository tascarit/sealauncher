import QtQuick
import QtQuick.Window
import QtMultimedia
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

Item {
    id: root
    anchors.fill: parent
    property bool settingsOpen: false

    FontLoader {
        id: minecraftFont
        source: "qrc:/fonts/minecraft.ttf"
    }

    Video {
        objectName: "background"
        id: backgroundVideo
        source: "qrc:/resources/background_opt.webm"
        loops: MediaPlayer.Infinite
        anchors.fill: parent
        clip: true
        muted: true
        fillMode: VideoOutput.PreserveAspectCrop
        autoPlay: true
        smooth: true
    }

    Rectangle {
        id: titleBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 32
        color: Qt.rgba(0, 0, 0, 0.8)
        z: 100

        MouseArea {
            acceptedButtons: Qt.LeftButton
            anchors.fill: parent
            onPressed: {
                if (Window.window) Window.window.startSystemMove()
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 15
            anchors.rightMargin: 0
            spacing: 10

            Item {
                Layout.fillWidth: true
            }

            component SysButton : Button {
                id: sysBtn
                property color hoverColor: Qt.rgba(1, 1, 1, 0.1)
                Layout.preferredWidth: 44
                Layout.fillHeight: true
                hoverEnabled: true
                flat: true
                padding: 0

                background: Rectangle {
                    color: sysBtn.hovered ? sysBtn.hoverColor : "transparent"
                    Behavior on color { ColorAnimation { duration: 100 } }
                }
            }

            SysButton {
                objectName: "minimizeButton"
                contentItem: Item {
                    anchors.fill: parent
                    Rectangle {
                        width: 14; height: 2
                        color: parent.parent.hovered ? "#FFFFFF" : Qt.rgba(1, 1, 1, 0.6)
                        anchors.centerIn: parent
                    }
                }
            }

            SysButton {
                objectName: "closeButton"
                contentItem: Item {
                    anchors.fill: parent
                    Image {
                        source: "qrc:/resources/x-button.png"
                        width: 15
                        height: 15
                        anchors.centerIn: parent
                    }
                }
            }
        }
    }

    Item {
        id: rootBuildsFrame
        height: root.height-32
        width: root.width / 4
        anchors.left: root.left
        anchors.top: titleBar.bottom
        anchors.bottom: root.bottom
        clip: true

        Image {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: -32
            source: "qrc:/resources/leftBlur.png"
            width: root.width
            height: root.height
            cache: true
            smooth: true
            asynchronous: true
            fillMode: Image.PreserveAspectCrop
        }

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.2)
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 15

            Text {
                text: "SeaLauncher"
                color: "#FFFFFF"
                font.pixelSize: 24
                font.bold: true
                font.letterSpacing: 1
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignCenter
                horizontalAlignment: Text.AlignLeft
                Layout.topMargin: 10
                Layout.bottomMargin: 10
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ListView {
                    id: buildListView
                    model: buildModel
                    spacing: 8
                    currentIndex: 0

                    section.property: "category"
                    section.delegate: Rectangle {
                        width: buildListView.width
                        height: 34
                        color: "transparent"

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 5

                            Text {
                                text: section === "local" ? "ЛОКАЛЬНЫЕ СБОРКИ" : "ДОСТУПНЫЕ СБОРКИ"
                                color: Qt.rgba(1, 1, 1, 0.4)
                                font.pixelSize: 11
                                font.bold: true
                                font.letterSpacing: 0.5
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: Qt.rgba(1, 1, 1, 0.1)
                            }
                        }
                    }

                    delegate: Button {
                        id: buildButton
                        width: buildListView.width
                        height: 52
                        hoverEnabled: true
                        flat: true
                        objectName: isSelected ? "selectedBuild" : ""

                        padding: 0
                        topPadding: 0
                        bottomPadding: 0

                        property bool isSelected: buildListView.currentIndex === index

                        contentItem: RowLayout {
                            id: delegateLayout
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.centerIn: parent
                            spacing: 12

                            Image {
                                source: model.icon || "qrc:/resources/wheat.png"
                                sourceSize.width: 24
                                sourceSize.height: 24
                                Layout.alignment: Qt.AlignVCenter
                                opacity: buildButton.isSelected ? 1.0 : (buildButton.hovered ? 0.8 : 0.6)
                                Behavior on opacity { NumberAnimation { duration: 120 } }
                            }

                            ColumnLayout {
                                spacing: 2
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter

                                Text {
                                    text: model.name
                                    objectName: "buttonText"
                                    color: "#FFFFFF"
                                    font.pixelSize: 14
                                    font.weight: buildButton.isSelected ? Font.DemiBold : Font.Normal
                                    opacity: buildButton.isSelected ? 1.0 : (buildButton.hovered ? 0.9 : 0.7)
                                    Behavior on opacity { NumberAnimation { duration: 120 } }
                                }
                                Text {
                                    text: model.packVersion ? model.version + "  |  pack " + model.packVersion : model.version
                                    color: buildButton.isSelected ? Qt.rgba(1, 1, 1, 0.6) : Qt.rgba(1, 1, 1, 0.4)
                                    font.pixelSize: 11
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }

                        background: Rectangle {
                            radius: buildButton.hovered ? 16 : 8
                            color: buildButton.isSelected
                                ? Qt.rgba(1, 1, 1, 0.12)
                                : (buildButton.hovered ? Qt.rgba(2, 2, 2, 0.06) : "transparent")

                            border.color: buildButton.isSelected
                                ? Qt.rgba(1, 1, 1, 0.2)
                                : (buildButton.hovered ? Qt.rgba(0, 0, 0, 0.1) : "transparent")
                            border.width: 1

                            Behavior on color { ColorAnimation { duration: 120 } }
                            Behavior on border.color { ColorAnimation { duration: 120 } }
                            Behavior on radius {NumberAnimation {duration: 120 }}

                            Rectangle {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: 3
                                height: buildButton.isSelected ? 24 : 0
                                radius: 1.5
                                color: "#FFF0B5"
                                Behavior on height { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }
                            }
                        }

                        onClicked: {
                            buildListView.currentIndex = index
                            minecraftHandler.reCheckBuilds(model.name, model.v, model.loader, model.mcV, model.git, model.parts, model.packVersion || "0.1")
                        }

                    }
                }
            }

            ListModel {
                id: buildModel
            }

            Text {
                text: "НАСТРОЙКИ"
                color: Qt.rgba(1, 1, 1, 0.4)
                font.pixelSize: 11
                font.bold: true
                font.letterSpacing: 0.5
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Qt.rgba(1, 1, 1, 0.1)
                Layout.topMargin: 5
                Layout.bottomMargin: 5
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10
                Layout.bottomMargin: 5

                TextField {
                    id: usernameInput
                    Layout.fillWidth: true
                    height: 44
                    placeholderText: "Введите ваш ник..."
                    placeholderTextColor: Qt.rgba(1, 1, 1, 0.3)
                    color: "#FFFFFF"
                    font.pixelSize: 14
                    selectByMouse: true
                    text: settings.username
                    onTextEdited: settings.username = text

                    maximumLength: 16

                    background: Rectangle {
                        radius: 8
                        color: usernameInput.activeFocus ? Qt.rgba(0, 0, 0, 0.3) : Qt.rgba(0, 0, 0, 0.15)
                        border.color: usernameInput.activeFocus ? "#00A2FF" : Qt.rgba(1, 1, 1, 0.15)
                        border.width: 1

                        Behavior on color { ColorAnimation { duration: 150 } }
                        Behavior on border.color { ColorAnimation { duration: 150 } }
                    }
                }

                Button {
                    id: createBuildButton
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    hoverEnabled: true
                    flat: true

                    contentItem: Text {
                        text: "+ Создать сборку"
                        color: "#FFFFFF"
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        opacity: createBuildButton.hovered ? 1.0 : 0.7
                    }

                    background: Rectangle {
                        radius: 8
                        color: createBuildButton.down ? Qt.rgba(0.2, 0.5, 1, 0.25)
                                                      : (createBuildButton.hovered ? Qt.rgba(0.2, 0.5, 1, 0.15) : Qt.rgba(0.2, 0.5, 1, 0.08))
                        border.color: Qt.rgba(0.4, 0.6, 1, 0.3)
                        border.width: 1
                    }

                    onClicked: buildCreatorDialog.open()
                }

                Button {
                    id: settingsButton
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    hoverEnabled: true

                    contentItem: RowLayout {
                        spacing: 10
                        anchors.centerIn: parent

                        Image {
                            source: "qrc:/resources/settings.png"
                            sourceSize.width: 16
                            sourceSize.height: 16
                            opacity: settingsButton.hovered ? 1.0 : 0.7
                            Behavior on opacity { NumberAnimation { duration: 120 } }
                        }

                        Text {
                            text: "Настройки лаунчера"
                            color: "#FFFFFF"
                            font.pixelSize: 13
                            opacity: settingsButton.hovered ? 1.0 : 0.7
                            Behavior on opacity { NumberAnimation { duration: 120 } }
                        }
                    }

                    background: Rectangle {
                        radius: 8
                        color: settingsButton.down
                            ? Qt.rgba(1, 1, 1, 0.15)
                            : (settingsButton.hovered ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.03))

                        border.color: settingsButton.hovered ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.05)
                        border.width: 1

                        Behavior on color { ColorAnimation { duration: 120 } }
                        Behavior on border.color { ColorAnimation { duration: 120 } }
                    }

                    onClicked: root.settingsOpen = !root.settingsOpen
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Qt.rgba(1, 1, 1, 0.1)
                Layout.topMargin: 5
                Layout.bottomMargin: 5
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "v" + settings.version
                    color: Qt.rgba(1, 1, 1, 0.4)
                    font.pixelSize: 11
                    font.bold: true
                    font.letterSpacing: 0.5
                }

                Button {
                    id: discordButton
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    hoverEnabled: true
                    flat: true
                    padding: 0

                    contentItem: Image {
                        source: "qrc:/resources/discord.png"
                        sourceSize.width: 120
                        sourceSize.height: 100
                        anchors.centerIn: parent
                        opacity: discordButton.hovered ? 1.0 : 0.6
                        Behavior on opacity { NumberAnimation { duration: 120 } }
                    }

                    background: Rectangle {
                        radius: 6
                        color: discordButton.hovered ? "#5865F2" : Qt.rgba(1, 1, 1, 0.05)
                        border.color: discordButton.hovered ? "#5865F2" : Qt.rgba(1, 1, 1, 0.1)
                        border.width: 1
                        Behavior on color { ColorAnimation { duration: 120 } }
                        Behavior on border.color { ColorAnimation { duration: 120 } }
                    }

                    onClicked: Qt.openUrlExternally("https://discord.gg/8jwbtANvsD")
                }
            }
        }
    }

    Item {
        id: rightContentArea
        anchors.left: rootBuildsFrame.right
        anchors.right: root.right
        anchors.top: titleBar.bottom
        anchors.bottom: root.bottom
        z: 10

        Item {
            id: mainGameContent
            anchors.fill: parent
            opacity: root.settingsOpen ? 0.0 : 1.0
            visible: opacity > 0.0
            Behavior on opacity { NumberAnimation { duration: 250 } }

            component NewsCard : Item {
                id: card
                property string titleText: ""
                property string dateText: ""
                property string bgImage: ""

                width: parent ? parent.width / 2 - 8 : 200
                height: 140

                Item {
                    id: cardContainer
                    anchors.fill: parent
                    layer.enabled: true
                    layer.effect: OpacityMask {
                        maskSource: Rectangle {
                            width: cardContainer.width
                            height: cardContainer.height
                            radius: 8
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        color: "#1E1E24"
                    }

                    Image {
                        id: cardImg
                        source: card.bgImage
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        visible: card.bgImage !== ""
                        scale: cardMouse.hovered ? 1.05 : 1.0
                        Behavior on scale { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                    }

                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.2) }
                            GradientStop { position: 0.6; color: Qt.rgba(0, 0, 0, 0.6) }
                            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.85) }
                        }
                    }

                    HoverHandler { id: cardMouse }

                    Rectangle {
                        id: borderRect
                        anchors.fill: parent
                        color: "transparent"
                        border.color: cardMouse.hovered ? "#FFDE7D" : Qt.rgba(1, 1, 1, 0.05)
                        border.width: 3
                        Behavior on border.color { ColorAnimation { duration: 150 } }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 4

                        Item { Layout.fillHeight: true }

                        Text {
                            text: card.dateText
                            color: Qt.rgba(1, 1, 1, 0.5)
                            font.pixelSize: 10
                            font.weight: Font.Medium
                        }
                        Text {
                            text: card.titleText
                            color: "#FFFFFF"
                            font.pixelSize: 13
                            font.bold: true
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            Layout.bottomMargin: cardMouse.hovered ? 2 : 0
                            Behavior on Layout.bottomMargin { NumberAnimation { duration: 150 } }
                        }
                    }
                }
            }

            ColumnLayout {
                id: newsSection
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 24
                spacing: 12

                Text {
                    text: "НОВОСТИ"
                    color: "#FFF2C9"
                    font.pixelSize: 12
                    font.bold: true
                    font.letterSpacing: 1
                }

                GridLayout {
                    id: newsGrid
                    columns: 2
                    rowSpacing: 16
                    columnSpacing: 16
                    Layout.fillWidth: true

                    Repeater {
                        id: newsRepeater
                        model: []

                        delegate: NewsCard {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 140
                            titleText: modelData.title || ""
                            dateText: modelData.date || ""
                            bgImage: modelData.image || ""
                        }
                    }
                }
            }

            Rectangle {
                id: launchBar
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 90

                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.6) }
                }

                Button {
                    objectName: "playButton"
                    id: playButton
                    anchors.centerIn: parent
                    width: 260
                    height: 48
                    hoverEnabled: true
                    flat: true
                    padding: 0

                    readonly property string caption: minecraftHandler.installation ? "УСТАНОВКА" : (minecraftHandler.updateAvailable ? "ОБНОВИТЬ"
                                                : (minecraftHandler.buildExists ? "ИГРАТЬ" : "СКАЧАТЬ"))
                    readonly property bool isUpdate: minecraftHandler.updateAvailable

                    contentItem: Item {
                        anchors.fill: parent

                        Text {
                            text: playButton.caption
                            color: playButton.hovered ? "#000000" : "#2e2e2e"
                            font.family: minecraftFont.name
                            font.pixelSize: 18
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: 2
                            anchors.verticalCenterOffset: 2
                        }
                        Text {
                            text: playButton.caption
                            color: "#FFFFFF"
                            font.family: minecraftFont.name
                            font.pixelSize: 18
                            anchors.centerIn: parent
                        }
                    }

                    background: Rectangle {
                        id: btnBg
                        radius: 2

                        color: playButton.isUpdate
                            ? (playButton.down ? "#6B5A17" : (playButton.hovered ? "#9A822F" : "#857027"))
                            : (playButton.down ? "#2E671E" : (playButton.hovered ? "#479A2F" : "#3C8527"))

                        border.color: playButton.isUpdate
                            ? (playButton.down ? "#3D3111" : "#B6A035")
                            : (playButton.down ? "#1A3D11" : "#55B635")
                        border.width: 2

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 3
                            color: playButton.isUpdate ? "#524518" : "#235218"
                            radius: 1
                        }

                        Behavior on color { ColorAnimation { duration: 80 } }
                    }

                    onClicked: {
                        if (!checkSettings()) {
                            settingsOpen = true
                            return
                        }
                        minecraftHandler.mainButtonClick()
                    }
                }

                Button {
                    id: editButton
                    anchors.right: playButton.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: playButton.verticalCenter
                    width: 180
                    height: 48
                    hoverEnabled: true
                    flat: true
                    padding: 0
                    visible: minecraftHandler.buildExists && buildListView.currentIndex >= 0 && buildModel.get(buildListView.currentIndex).category === "local"

                    contentItem: Item {
                        anchors.fill: parent
                        Text {
                            text: "РЕДАКТИРОВАТЬ"
                            color: editButton.hovered ? "#000000" : "#2e2e2e"
                            font.family: minecraftFont.name
                            font.pixelSize: 15
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: 2
                            anchors.verticalCenterOffset: 2
                        }
                        Text {
                            text: "РЕДАКТИРОВАТЬ"
                            color: "#FFFFFF"
                            font.family: minecraftFont.name
                            font.pixelSize: 15
                            anchors.centerIn: parent
                        }
                    }

                    background: Rectangle {
                        id: editBtnBg
                        radius: 2
                        color: editButton.down
                            ? "#173B6B"
                            : (editButton.hovered ? "#2A5FA8" : "#1F4A85")
                        border.color: editButton.down ? "#0F2647" : "#3A7BD5"
                        border.width: 2

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 3
                            color: "#0F2647"
                            radius: 1
                        }
                        Behavior on color { ColorAnimation { duration: 80 } }
                    }

                    onClicked: {
                        var cur = buildModel.get(buildListView.currentIndex)
                        if (cur && cur.category === "local") {
                            buildCreatorDialog.loadBuildForEditing(cur.name)
                            buildCreatorDialog.open()
                        }
                    }
                }

                Button {
                    id: deleteButton
                    anchors.left: playButton.right
                    anchors.leftMargin: 12
                    anchors.verticalCenter: playButton.verticalCenter
                    width: 180
                    height: 48
                    hoverEnabled: true
                    flat: true
                    padding: 0
                    visible: minecraftHandler.buildExists

                    contentItem: Item {
                        anchors.fill: parent

                        Text {
                            text: "УДАЛИТЬ"
                            color: deleteButton.hovered ? "#000000" : "#2e2e2e"
                            font.family: minecraftFont.name
                            font.pixelSize: 15
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: 2
                            anchors.verticalCenterOffset: 2
                        }
                        Text {
                            text: "УДАЛИТЬ"
                            color: "#FFFFFF"
                            font.family: minecraftFont.name
                            font.pixelSize: 15
                            anchors.centerIn: parent
                        }
                    }

                    background: Rectangle {
                        id: delBtnBg
                        radius: 2
                        color: deleteButton.down ? "#671E1E" : (deleteButton.hovered ? "#9A3434" : "#852727")
                        border.color: deleteButton.down ? "#3D1111" : "#B64545"
                        border.width: 2

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 3
                            color: "#521818"
                            radius: 1
                        }
                        Behavior on color { ColorAnimation { duration: 80 } }
                    }

                    onClicked: {
                        var cur = buildModel.get(buildListView.currentIndex)
                        if (!cur) return
                        if (cur.category === "local")
                            localBuildsManager.deleteBuild(cur.name)
                        else
                            minecraftHandler.deleteCurrentBuild()
                        rebuildBuildsModel()
                    }
                }
            }
        }

        Item {
            id: settingsPanel
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right

            width: root.settingsOpen ? parent.width : 0
            clip: true

            Behavior on width {
                NumberAnimation { duration: 300; easing.type: Easing.OutCubic }
            }

            MultiEffect {
                width: root.width
                height: root.height
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: -32
                autoPaddingEnabled: false
                blur: 1.0
                blurEnabled: true
                blurMax: 32
                source: backgroundVideo
                visible: root.settingsOpen
            }

            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(0, 0, 0, 0.4)
                anchors.margins: 10
                radius: 8
                border.color: Qt.rgba(1, 1, 1, 0.05)
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 18

                    Text {
                        text: "Настройки лаунчера"
                        color: "#FFFFFF"
                        font.pixelSize: 18
                        font.bold: true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Qt.rgba(1, 1, 1, 0.1)
                        Layout.bottomMargin: 5
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Выделение оперативной памяти:"; color: Qt.rgba(1, 1, 1, 0.6); font.pixelSize: 12 }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: (ramSlider.value / 1024).toFixed(0) + " ГБ"
                                color: "#FFF0B5"
                                font.pixelSize: 13
                                font.bold: true
                            }
                        }

                        Slider {
                            id: ramSlider
                            objectName: "ramSlider"
                            Layout.fillWidth: true
                            from: 2048
                            to: settings.maxRam
                            stepSize: 1024
                            value: settings.ramMb
                            live: true

                            onMoved: settings.ramMb = value

                            background: Rectangle {
                                x: ramSlider.leftPadding
                                y: ramSlider.topPadding + ramSlider.availableHeight / 2 - height / 2
                                implicitWidth: 200
                                implicitHeight: 4
                                width: ramSlider.availableWidth
                                height: implicitHeight
                                radius: 2
                                color: Qt.rgba(1, 1, 1, 0.1)

                                Rectangle {
                                    width: ramSlider.visualPosition * parent.width
                                    height: parent.height
                                    color: "#FFF0B5"
                                    radius: 2
                                }
                            }

                            handle: Rectangle {
                                x: ramSlider.leftPadding + ramSlider.visualPosition * (ramSlider.availableWidth - width)
                                y: ramSlider.topPadding + ramSlider.availableHeight / 2 - height / 2
                                implicitWidth: 14
                                implicitHeight: 14
                                radius: 7
                                color: ramSlider.pressed ? "#BDB386" : (ramSlider.hovered ? "#FFF7D1" : "#FFF0B5")
                                border.color: "#FFFFFF"
                                border.width: 1.5
                                Behavior on color { ColorAnimation { duration: 100 } }
                            }
                        }
                    }

                    component SettingsInput : TextField {
                        id: input
                        Layout.fillWidth: true
                        color: "#FFFFFF"
                        font.pixelSize: 13
                        selectByMouse: true
                        background: Rectangle {
                            implicitHeight: 36
                            radius: 6
                            color: input.activeFocus ? Qt.rgba(0, 0, 0, 0.3) : Qt.rgba(0, 0, 0, 0.15)
                            border.color: input.activeFocus ? "#00A2FF" : Qt.rgba(1, 1, 1, 0.1)
                            border.width: 1
                            Behavior on border.color { ColorAnimation { duration: 150 } }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text { text: "Путь к Java (исполняемый файл):"; color: Qt.rgba(1, 1, 1, 0.6); font.pixelSize: 12 }
                        RowLayout {
                            spacing: 8
                            SettingsInput {
                                id: javaPathInput
                                text: settings.javaPath
                                placeholderText: "Например C:/Program Files/Java/jdk-17/bin/java.exe"
                                onTextEdited: settings.javaPath = text
                            }
                            Button {
                                text: "Обзор"
                                Layout.preferredHeight: 36
                                hoverEnabled: true
                                flat: true
                                contentItem: Text { text: parent.text; color: "#FFFFFF"; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle {
                                    radius: 6
                                    color: parent.hovered ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.04)
                                    border.color: Qt.rgba(1, 1, 1, 0.1)
                                }
                                onClicked: {
                                    let path = settingsController.pickJavaExecutable(settings.javaPath)
                                    if (path !== "") settings.javaPath = path
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text { text: "Директория установки сборок:"; color: Qt.rgba(1, 1, 1, 0.6); font.pixelSize: 12 }
                        RowLayout {
                            spacing: 8
                            SettingsInput {
                                id: gameDirInput
                                text: settings.gameDir
                                placeholderText: "Например C:/Users/Clove/AppData/Roaming/.sealauncher"
                                onTextEdited: settings.gameDir = text
                            }
                            Button {
                                text: "Обзор"
                                Layout.preferredHeight: 36
                                hoverEnabled: true
                                flat: true
                                contentItem: Text { text: parent.text; color: "#FFFFFF"; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle {
                                    radius: 6
                                    color: parent.hovered ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.04)
                                    border.color: Qt.rgba(1, 1, 1, 0.1)
                                }
                                onClicked: {
                                    let path = settingsController.pickDirectory(settings.gameDir)
                                    if (path !== "") settings.gameDir = path
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text { text: "Аргументы запуска JVM (Minecraft):"; color: Qt.rgba(1, 1, 1, 0.6); font.pixelSize: 12 }
                        SettingsInput {
                            id: jvmArgsInput
                            text: settings.jvmArgs
                            onTextEdited: settings.jvmArgs = text
                            placeholderText: "Например: -Xmx4G -XX:+UseG1GC"
                        }
                    }

                    Button {
                        id: openFolderButton
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        hoverEnabled: true
                        flat: true

                        contentItem: RowLayout {
                            spacing: 10
                            anchors.centerIn: parent
                            Image {
                                source: "qrc:/resources/folder.png"
                                sourceSize.width: 16
                                sourceSize.height: 16
                                opacity: openFolderButton.hovered ? 1.0 : 0.7
                            }
                            Text {
                                text: "Открыть папку лаунчера"
                                color: "#FFFFFF"
                                font.pixelSize: 13
                                opacity: openFolderButton.hovered ? 1.0 : 0.7
                            }
                        }

                        background: Rectangle {
                            radius: 6
                            color: openFolderButton.down ? Qt.rgba(1, 1, 1, 0.15)
                                                         : (openFolderButton.hovered ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.03))
                            border.color: Qt.rgba(1, 1, 1, 0.1)
                        }

                        onClicked: minecraftHandler.openLauncherDir()
                    }

                    Item { Layout.fillHeight: true }

                    Button {
                        id: saveSettingsButton
                        text: "Сохранить изменения"
                        Layout.alignment: Qt.AlignRight
                        Layout.preferredWidth: 160
                        Layout.preferredHeight: 38
                        hoverEnabled: true
                        flat: true
                        padding: 0

                        contentItem: Text {
                            text: parent.text
                            color: "#575757"
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            radius: 6
                            color: saveSettingsButton.down
                                ? "#BDB386"
                                : (saveSettingsButton.hovered ? "#FFF8DB" : "#FFF1BA")

                            border.color: saveSettingsButton.hovered ? "#FFFFFF" : "transparent"
                            border.width: 1

                            Behavior on color { ColorAnimation { duration: 100 } }
                        }

                        onClicked: {
                            root.settingsOpen = false
                            minecraftHandler.fetchBuildsList()
                            minecraftHandler.fetchNews()
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: settingsBanner
        visible: !settingsOk
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 34
        color: "#8a2222"
        z: 90

        Text {
            anchors.centerIn: parent
            color: "#ffffff"
            text: "Проверьте настройки: " + settingsProblems
        }

        MouseArea {
            anchors.fill: parent
            onClicked: settingsOpen = true
        }
    }

    ProgressPanel {
        id: progressPanel
        objectName: "progressPanel"
        onCancelled: progressPanel.close()
    }

    ErrorDialog {
        id: errorDialog
        objectName: "errorDialog"
        //onRetried: minecraftHandler.retryLastOperation()
        onClosed: console.log("Ошибка закрыта")
    }

    BuildCreatorDialog {
        id: buildCreatorDialog
    }

    property var serverBuilds: []
    property bool settingsOk: true
    property string settingsProblems: ""

    function checkSettings() {
        var problems = []

        var u = settings.username
        if (u === undefined || String(u).trim() === "")
            problems.push("не задан username")

        var gd = settings.gameDir
        if (gd === undefined || String(gd).trim() === "")
            problems.push("не задан путь к игре")

        var java = settings.javaPath
        if (java === undefined || String(java).trim() === "")
            problems.push("не задан путь к java")

        settingsProblems = problems.join(", ")
        settingsOk = (problems.length === 0)
        return settingsOk
    }

    function rebuildBuildsModel() {
        var prevName = ""
        if (buildListView.currentIndex >= 0 && buildListView.currentIndex < buildModel.count) {
            var cur = buildModel.get(buildListView.currentIndex)
            if (cur) prevName = cur.name
        }

        buildModel.clear()

        for (var i = 0; i < serverBuilds.length; i++) {
            buildModel.append({
                name: serverBuilds[i].name || "",
                version: serverBuilds[i].version || "",
                packVersion: serverBuilds[i].packVersion || "",
                icon: serverBuilds[i].icon || "",
                v: serverBuilds[i].v || "",
                loader: serverBuilds[i].loader || "vanilla",
                mcV: serverBuilds[i].mcV || "",
                git: serverBuilds[i].git || "",
                parts: serverBuilds[i].parts || 1,
                category: "server"
            })
        }

        var local = localBuildsManager.buildsArray()
        for (var j = 0; j < local.length; j++) {
            buildModel.append({
                name: local[j].name,
                version: local[j].version,
                packVersion: "0.1",
                icon: local[j].icon,
                v: local[j].v,
                loader: local[j].loader,
                mcV: local[j].mcV,
                git: local[j].git,
                parts: local[j].parts,
                category: "local"
            })
        }

        var idx = 0
        if (prevName !== "") {
            for (var k = 0; k < buildModel.count; k++) {
                if (buildModel.get(k).name === prevName) { idx = k; break }
            }
        }

        if (buildModel.count > 0) {
            buildListView.currentIndex = idx
            var b = buildModel.get(idx)
            minecraftHandler.reCheckBuilds(b.name, b.v, b.loader, b.mcV, b.git, b.parts, b.packVersion)
        }
    }

    Connections {
        target: qmlHandler

        function onInstallProgress(stage, details, progress) {
            progressPanel.title = "Установка сборки"
            progressPanel.stage = stage
            progressPanel.details = details

            if (progress < 0) {
                progressPanel.indeterminate = true
            } else {
                progressPanel.indeterminate = false
                progressPanel.progress = progress
            }

            if (progressPanel.opacity < 1.0) progressPanel.open()
        }

        function onInstallError(title, message, details, canRetry) {
            progressPanel.close()
            errorDialog.show(title, message, details, canRetry)
        }
    }

    Connections {
        target: localBuildsManager

        function onBuildsChanged() {
            rebuildBuildsModel()
        }

        function onCreationFinished(name) {
            progressPanel.close()
            rebuildBuildsModel()
        }
    }

    Connections {
        target: minecraftHandler

        function onBuildsListReady(builds) {
            serverBuilds = builds
            rebuildBuildsModel()
        }

        function onNewsReady(news) {
            console.log("QML: onNewsReady called, count:", news.length)
            let items = []
            for (let i = 0; i < news.length && i < 8; i++) {
                items.push({
                    title: news[i].title || "Без заголовка",
                    date: news[i].date || "",
                    image: news[i].image || ""
                })
            }
            console.log("QML: Setting newsRepeater.model to", items.length, "items")
            newsRepeater.model = items
        }
    }

    function selectFirstBuild() {
        if (buildModel.count === 0) return
        buildListView.currentIndex = 0
        var b = buildModel.get(0)
        minecraftHandler.reCheckBuilds(b.name, b.v, b.loader, b.mcV, b.git, b.parts, b.packVersion)
    }

    Timer {
        interval: 400
        running: true
        repeat: false
        onTriggered: {
            minecraftHandler.fetchBuildsList()
            minecraftHandler.fetchNews()
        }
    }
}


