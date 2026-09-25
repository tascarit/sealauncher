import QtQuick 2.15
import QtQuick.Window 2.15
import QtMultimedia
import QtQuick.Controls 2.15
import QtQuick.Effects
import QtQuick.Layouts

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

            Image {
                source: "qrc:/icons/logo.svg"
                sourceSize.width: 16
                sourceSize.height: 16
                Layout.alignment: Qt.AlignVCenter
            }

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

            Text {
                text: "ДОСТУПНЫЕ СБОРКИ"
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

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ListView {
                    id: buildListView
                    model: buildModel
                    spacing: 8
                    currentIndex: 0

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
                                source: model.icon || "qrc:/icons/default_pack.svg"
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
                                    text: model.version
                                    color: buildButton.isSelected ? Qt.rgba(1, 1, 1, 0.6) : Qt.rgba(1, 1, 1, 0.4)
                                    font.pixelSize: 11
                                }
                            }
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
                            minecraftHandler.reCheckBuilds(model.name, model.v, model.loader, model.mcV)
                        }

                    }
                }
            }

            ListModel {
                id: buildModel
                ListElement { name: "Krevetka"; version: "1.21.1 (neoforge 21.1.250)"; icon: "qrc:/resources/wheat.png"; v: "21.1.250"; loader: "neoforge" ; mcV: "1.21.1"}
                ListElement { name: "Test"; version: "1.21.1 (neoforge 21.1.250)"; icon: "qrc:/icons/vanilla.svg"; v: "1.12.2"; loader: "forge"; mcV: "1.12.2" }
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
                    id: settingsButton
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    hoverEnabled: true

                    contentItem: RowLayout {
                        spacing: 10
                        anchors.centerIn: parent

                        Image {
                            source: "qrc:/icons/gear.png"
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

            Text {
                text: "v" + settings.version
                color: Qt.rgba(1, 1, 1, 0.4)
                font.pixelSize: 11
                font.bold: true
                font.letterSpacing: 0.5
                Layout.fillWidth: true
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
                    rowSpacing: 16
                    columnSpacing: 16
                    columns: 2
                    Layout.alignment: parent

                    component NewsCard : Rectangle {
                        id: card
                        property string titleText: ""
                        property string dateText: ""
                        property string bgImage: ""

                        Layout.fillWidth: true
                        Layout.preferredHeight: 140
                        radius: 8
                        color: "#1E1E24"
                        clip: true

                        HoverHandler { id: cardMouse }

                        MultiEffect {
                            clip: true
                            source: cardImg
                            anchors.fill: parent
                            maskEnabled: true
                            maskSource: cardMask
                        }

                        Image {
                            id: cardImg
                            source: card.bgImage
                            anchors.fill: parent
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            visible: false

                            scale: cardMouse.hovered ? 1.05 : 1.0
                            Behavior on scale { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                        }

                        Item {
                            clip: true
                            id: cardMask
                            anchors.fill: cardImg; layer.enabled: true; visible: false
                            Rectangle {anchors.fill: parent; radius: parent.parent.radius}
                        }

                        Rectangle {
                            id: borderRect
                            anchors.fill: parent
                            radius: card.radius

                            color: "transparent"
                            border.color: cardMouse.hovered ? "#FFDE7D" : Qt.rgba(1, 1, 1, 0.05)
                            border.width: 2

                            z: 10

                            Behavior on border.color { ColorAnimation { duration: 150 } }
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: parent.radius
                            color: "black"
                            clip: true

                            gradient: Gradient {
                                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.2) }
                                GradientStop { position: 0.6; color: Qt.rgba(0, 0, 0, 0.6) }
                                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.85) }
                            }
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

                    NewsCard {
                        titleText: "Сенсация! Владелец сервера Krevetka захлебнулся спермой!!!"
                        dateText: "22 сентября 2026"
                        bgImage: "https://avatars.mds.yandex.net/i?id=ef1218e3961c3db0273eef921db68a53_l-5221319-images-thumbs&n=13"
                    }

                    NewsCard {
                        titleText: "Где то под мостом умер некий \"toast\" наевшись залетных карасей"
                        dateText: "21 сентября 2026 г."
                        bgImage: "https://i.imgflip.com/7w38au.jpg"
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
                    id: playButton
                    anchors.centerIn: parent
                    width: 260
                    height: 48
                    hoverEnabled: true
                    flat: true
                    padding: 0

                    contentItem: Item {
                        anchors.fill: parent

                        Text {
                            text: minecraftHandler.buildExists ? "ИГРАТЬ" : "СКАЧАТЬ"
                            color: playButton.hovered ? "#000000" : "#2e2e2e"

                            font.family: minecraftFont.name
                            font.pixelSize: 18

                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: 2
                            anchors.verticalCenterOffset: 2
                        }
                        Text {
                            text: minecraftHandler.buildExists ? "ИГРАТЬ" : "СКАЧАТЬ"
                            color: "#FFFFFF"
                            font.family: minecraftFont.name
                            font.pixelSize: 18
                            anchors.centerIn: parent
                        }
                    }

                    background: Rectangle {
                        id: btnBg
                        radius: 2

                        color: playButton.down
                            ? "#2E671E"
                            : (playButton.hovered ? "#479A2F" : "#3C8527")

                        border.color: playButton.down ? "#1A3D11" : "#55B635"
                        border.width: 2

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 3
                            color: "#235218"
                            radius: 1
                        }

                        Behavior on color { ColorAnimation { duration: 80 } }
                    }

                    onClicked: {
                        minecraftHandler.mainButtonClick()
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
                            console.log("Сохранено ОЗУ:", ramSlider.value, "МБ")
                            console.log("Аргументы JVM:", jvmArgsInput.text)
                        }
                    }
                }
            }
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
}


