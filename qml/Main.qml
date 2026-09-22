import QtQuick 2.15
import QtQuick.Window 2.15
import QtMultimedia
import QtQuick.Controls 2.15
import QtQuick.Effects
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

Item {
    id: root
    anchors.fill: parent
    clip: true
    property bool settingsOpen: false

    FontLoader {
        id: minecraftFont
        source: "qrc:/fonts/Minecraft.ttf" // Укажите ваш путь к .ttf файлу шрифта
    }

    Video {
        id: backgroundVideo
        source: "qrc:/resources/background.webm"
        loops: MediaPlayer.Infinite
        anchors.fill: parent
        clip: true
        muted: true
        fillMode: VideoOutput.PreserveAspectCrop

        Component.onCompleted: {
            backgroundVideo.play()
        }
    }

    Rectangle {
        id: titleBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 32
        color: Qt.rgba(0, 0, 0, 0.3)
        z: 100

        MouseArea {
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

            Text {
                text: "SeaLauncher"
                color: Qt.rgba(1, 1, 1, 0.6)
                font.pixelSize: 12
                font.weight: Font.Medium
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                anchors.centerIn: titleBar
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
                onClicked: if (Window.window) Window.window.showMinimized()
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
                onClicked: if (Window.window) Window.window.close()
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

        MultiEffect {
            width: root.width
            height: root.height
            anchors.left: parent.left
            anchors.top: parent.top
            autoPaddingEnabled: false
            blur: 1.0
            blurEnabled: true
            blurMax: 48
            source: backgroundVideo
            anchors.topMargin: -32
        }

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.2)
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 15

            // =========================================================================
            // 1. ВЕРХНЯЯ ЧАСТЬ: Заголовок лаунчера
            // =========================================================================
            Text {
                text: "SeaLauncher"
                color: "#FFFFFF"
                font.pixelSize: 24
                font.bold: true
                font.letterSpacing: 1
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                Layout.topMargin: 10
                Layout.bottomMargin: 10
            }

            // =========================================================================
            // 2. СРЕДНЯЯ ЧАСТЬ: Список сборок с прокруткой
            // =========================================================================
            // Заголовок для списка сборок
            Text {
                text: "ДОСТУПНЫЕ СБОРКИ"
                color: Qt.rgba(1, 1, 1, 0.4) // Ненавязчивый серый цвет
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
                Layout.fillHeight: true // Занимает всё свободное место между верхом и низом
                clip: true

                ListView {
                    id: buildListView
                    model: buildModel
                    spacing: 8
                    currentIndex: 0 // По умолчанию выбрана первая сборка

                    // Компонент визуального отображения сборки (кнопка)
                    delegate: Button {
                        id: buildButton
                        width: buildListView.width
                        height: 52
                        hoverEnabled: true
                        flat: true

                        padding: 0
                        topPadding: 0
                        bottomPadding: 0

                        // Свойство для проверки, выбрана ли данная строка
                        property bool isSelected: buildListView.currentIndex === index

                        contentItem: RowLayout {
                            id: delegateLayout
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.centerIn: parent
                            spacing: 12

                            // Иконка сборки (например, логотип мода или дефолтный кубик)
                            Image {
                                source: model.icon || "qrc:/icons/default_pack.svg"
                                sourceSize.width: 24
                                sourceSize.height: 24
                                Layout.alignment: Qt.AlignVCenter
                                opacity: buildButton.isSelected ? 1.0 : (buildButton.hovered ? 0.8 : 0.6)
                                Behavior on opacity { NumberAnimation { duration: 120 } }
                            }

                            // Текст с названием и версией сборки
                            ColumnLayout {
                                spacing: 2
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter

                                Text {
                                    text: model.name
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

                            // Акцентный маркер слева
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
                            console.log("Выбрана сборка:", model.name)
                        }

                    }
                }
            }

            // Временные данные для списка (замените на свои или на C++ модель)
            ListModel {
                id: buildModel
                ListElement { name: "Krevetka"; version: "1.21.1 (neoforge 21.1.250)"; icon: "qrc:/resources/wheat.png" }
                ListElement { name: "Test"; version: "1.21.1 (neoforge 21.1.250)"; icon: "qrc:/icons/vanilla.svg" }
            }

            Text {
                text: "НАСТРОЙКИ"
                color: Qt.rgba(1, 1, 1, 0.4) // Ненавязчивый серый цвет
                font.pixelSize: 11
                font.bold: true
                font.letterSpacing: 0.5
                Layout.fillWidth: true
            }

            // Разделительная линия перед нижним блоком профиля
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Qt.rgba(1, 1, 1, 0.1)
                Layout.topMargin: 5
                Layout.bottomMargin: 5
            }

            // =========================================================================
            // 3. НИЖНЯЯ ЧАСТЬ: Поле ввода имени и кнопка Настройки
            // =========================================================================
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10
                Layout.bottomMargin: 5

                // Поле ввода Никнейма
                TextField {
                    id: usernameInput
                    Layout.fillWidth: true
                    height: 44
                    placeholderText: "Введите ваш ник..."
                    placeholderTextColor: Qt.rgba(1, 1, 1, 0.3)
                    color: "#FFFFFF"
                    font.pixelSize: 14
                    selectByMouse: true

                    // Ограничим длину ника для красоты интерфейса
                    maximumLength: 16

                    // Стилизация поля ввода под общее размытое стекло
                    background: Rectangle {
                        radius: 8
                        color: usernameInput.activeFocus ? Qt.rgba(0, 0, 0, 0.3) : Qt.rgba(0, 0, 0, 0.15)
                        border.color: usernameInput.activeFocus ? "#00A2FF" : Qt.rgba(1, 1, 1, 0.15)
                        border.width: 1

                        Behavior on color { ColorAnimation { duration: 150 } }
                        Behavior on border.color { ColorAnimation { duration: 150 } }
                    }

                    onTextChanged: console.log("Текущий никнейм:", text)
                }

                // Кнопка Настроек
                Button {
                    id: settingsButton
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    hoverEnabled: true

                    contentItem: RowLayout {
                        spacing: 10
                        anchors.centerIn: parent

                        Image {
                            source: "qrc:/icons/settings.svg"
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

            // --- БЛОК 1: НОВОСТНАЯ ЛЕНТА (Сверху) ---
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

                // Горизонтальный ряд карточек новостей
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    // Шаблон новостной карточки
                    component NewsCard : Rectangle {
                        id: card
                        property string titleText: ""
                        property string dateText: ""
                        property string bgImage: "" // Свойство для пути к картинке

                        Layout.fillWidth: true
                        Layout.preferredHeight: 140
                        radius: 8
                        color: "#1E1E24" // Базовый цвет, если картинка не загрузится
                        border.color: cardMouse.hovered ? "#FFDE7D" : Qt.rgba(1, 1, 1, 0.05)
                        border.width: 1
                        clip: true

                        // Плавная анимация рамки и масштабирования при наведении
                        Behavior on border.color { ColorAnimation { duration: 150 } }

                        HoverHandler { id: cardMouse }

                        // 1. Изображение новости на заднем плане карточки
                        Item {
                            id: imageClipContainer
                            anchors.fill: parent

                            // Включаем аппаратную обрезку для самого контейнера
                            clip: true

                            // Заставляем графический движок Qt 6 закруглить пиксели САМОГО контейнера.
                            // Теперь всё, что находится внутри него (включая картинку со scale: 1.05),
                            // будет автоматически и гладко обрезаться по радиусу карточки без всяких масок!
                            layer.enabled: true
                            layer.effect: MultiEffect {
                                // Мы НЕ включаем maskEnabled, движок скруглит слой по границам родительского радиуса
                            }

                            Image {
                                id: cardImg
                                source: card.bgImage
                                anchors.fill: parent
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true

                                // Плавная анимация зума при наведении (теперь работает без исчезновений)
                                scale: cardMouse.hovered ? 1.05 : 1.0
                                Behavior on scale { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                            }
                        }

                        // 2. Градиентное затемнение ПОВЕРХ картинки (кинематографичный эффект)
                        // Снизу плашка темнее (чтобы читался текст), сверху — прозрачнее
                        Rectangle {
                            anchors.fill: parent
                            radius: card.radius // Принудительно скругляем сам градиент
                            color: "transparent"

                            gradient: Gradient {
                                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.2) }
                                GradientStop { position: 0.6; color: Qt.rgba(0, 0, 0, 0.6) }
                                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.85) }
                            }
                        }

                        // 3. Контент с текстом (лежит на самом верхнем слое)
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 4

                            Item { Layout.fillHeight: true } // Прижимает текст к нижнему краю карточки

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

                                // Текст слегка приподнимается при наведении
                                Layout.bottomMargin: cardMouse.hovered ? 2 : 0
                                Behavior on Layout.bottomMargin { NumberAnimation { duration: 150 } }
                            }
                        }
                    }

                    NewsCard {
                        titleText: "Сенсация! Владелец сервера Krevetka захлебнулся спермой!!!"
                        dateText: "22 сентября 2026"
                        bgImage: "https://avatars.mds.yandex.net/i?id=ef1218e3961c3db0273eef921db68a53_l-5221319-images-thumbs&n=13" // Или прямая ссылка из сети!
                    }

                    NewsCard {
                        titleText: "Где то под мостом умер неизвестный \"toast\""
                        dateText: "21 сентября 2026 г."
                        bgImage: "https://i.imgflip.com/7w38au.jpg"
                    }
                }
            }

            // --- БЛОК 2: ЗОНА ЗАПУСКА С КНОПКОЙ «ИГРАТЬ» (Снизу) ---
            Rectangle {
                id: launchBar
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 90

                // Градиентная подложка, чтобы кнопка выделялась на фоне видео
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.6) } // Плавное затемнение к низу
                }

                // Пиксельная зеленая кнопка ИГРАТЬ (В стиле Minecraft)
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

                        // Объемный текст с черной тенью (каноничный стиль Minecraft GUI)
                        Text {
                            text: "ИГРАТЬ"
                            color: "#E0E0E0"

                            // ИСПОЛЬЗУЕМ ЗАГРУЖЕННЫЙ ШРИФТ MINECRAFT
                            font.family: minecraftFont.name
                            font.pixelSize: 18

                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: 2
                            anchors.verticalCenterOffset: 2
                        }
                        Text {
                            text: "ИГРАТЬ"
                            color: playButton.hovered ? "#FFFFFF" : "#FFFF55" // При наведении текст светится ярче
                            font.family: minecraftFont.name
                            font.pixelSize: 18
                            anchors.centerIn: parent
                        }
                    }

                    // Объемный 3D-рельеф кнопки (Торцы светлее, низ темнее)
                    background: Rectangle {
                        id: btnBg
                        radius: 2 // Маленькое скругление для сохранения пиксельного стиля

                        // Натуральный зеленый цвет Minecraft Launcher: #3C8527, ховер: #479A2F
                        color: playButton.down
                            ? "#2E671E"
                            : (playButton.hovered ? "#479A2F" : "#3C8527")

                        // Границы (светлый верх, темный низ для 3D пиксель-эффекта)
                        border.color: playButton.down ? "#1A3D11" : "#55B635"
                        border.width: 2

                        // Имитация глубокой тени кнопки снизу
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
                        console.log("Запуск выбранной сборки:", buildListView.model.get(buildListView.currentIndex).name)
                    }
                }
            }
        }

        Item {
            id: settingsPanel
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right // Прижимаем к правому краю

            // Динамическая ширина: если флаг true — растягивается на всю область, если false — сжимается в 0
            width: root.settingsOpen ? parent.width : 0
            clip: true // Обрезает внутренний контент, когда панель скрыта

            // Плавный эффект выезжающей шторки
            Behavior on width {
                NumberAnimation { duration: 300; easing.type: Easing.OutCubic }
            }

            MultiEffect {
                width: root.width
                height: root.height
                // Сдвигаем текстуру эффекта влево, так как панель находится в правой части root
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: -32 // Компенсируем высоту шапки
                autoPaddingEnabled: false
                blur: 1.0
                blurEnabled: true
                blurMax: 48
                source: backgroundVideo
            }

            // Внутренний визуальный контейнер настроек
            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(0, 0, 0, 0.4) // Темное стекло
                anchors.margins: 10
                radius: 8
                border.color: Qt.rgba(1, 1, 1, 0.05)
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 18

                    // --- ЗАГОЛОВОК ---
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

                    // --- НАСТРОЙКА 1: ВЫДЕЛЕНИЕ ОЗУ (Исправленный слайдер) ---
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
                            Layout.fillWidth: true
                            from: 2048
                            to: 16384
                            stepSize: 1024
                            value: 4096
                            live: true

                            // Красивая кастомная полоса без белых просветов по бокам
                            background: Rectangle {
                                x: ramSlider.leftPadding
                                y: ramSlider.topPadding + ramSlider.availableHeight / 2 - height / 2
                                implicitWidth: 200
                                implicitHeight: 4
                                width: ramSlider.availableWidth
                                height: implicitHeight
                                radius: 2
                                color: Qt.rgba(1, 1, 1, 0.1)

                                // Заполненная часть (синий прогресс)
                                Rectangle {
                                    width: ramSlider.visualPosition * parent.width
                                    height: parent.height
                                    color: "#FFF0B5"
                                    radius: 2
                                }
                            }

                            // Кастомный круглый ползунок (Handle)
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

                    // Шаблон для текстовых полей ввода в настройках
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

                    // --- НАСТРОЙКА 2: ВЫБОР JAVA ---
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text { text: "Путь к Java (исполняемый файл):"; color: Qt.rgba(1, 1, 1, 0.6); font.pixelSize: 12 }
                        RowLayout {
                            spacing: 8
                            SettingsInput {
                                id: javaPathInput
                                text: ""
                                placeholderText: "Например C:/Program Files/Java/jdk-17/bin/java.exe"
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
                            }
                        }
                    }

                    // --- НАСТРОЙКА 3: ДИРЕКТОРИЯ СБОРКИ ---
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text { text: "Директория установки сборок:"; color: Qt.rgba(1, 1, 1, 0.6); font.pixelSize: 12 }
                        RowLayout {
                            spacing: 8
                            SettingsInput {
                                id: gameDirInput
                                text: ""
                                placeholderText: "Например C:/Users/Clove/AppData/Roaming/.sealauncher"
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
                            }
                        }
                    }

                    // --- НАСТРОЙКА 4: АРГУМЕНТЫ MINECRAFT ---
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text { text: "Аргументы запуска JVM (Minecraft):"; color: Qt.rgba(1, 1, 1, 0.6); font.pixelSize: 12 }
                        SettingsInput {
                            id: jvmArgsInput
                            text: ""
                            placeholderText: "Например: -Xmx4G -XX:+UseG1GC"
                        }
                    }

                    Item { Layout.fillHeight: true } // Сдвигает всё меню наверх

                    // --- СТИЛЬНАЯ КНОПКА «СОХРАНИТЬ И ЗАКРЫТЬ» ---
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
                            // При наведении становится ярче, при клике темнеет
                            color: saveSettingsButton.down
                                ? "#BDB386"
                                : (saveSettingsButton.hovered ? "#FFF8DB" : "#FFF1BA")

                            // Легкое свечение рамки при наведении
                            border.color: saveSettingsButton.hovered ? "#FFFFFF" : "transparent"
                            border.width: 1

                            Behavior on color { ColorAnimation { duration: 100 } }
                        }

                        onClicked: {
                            root.settingsOpen = false // Закрываем шторку
                            console.log("Сохранено ОЗУ:", ramSlider.value, "МБ")
                            console.log("Аргументы JVM:", jvmArgsInput.text)
                        }
                    }
                }
            }
        }
    }
}
