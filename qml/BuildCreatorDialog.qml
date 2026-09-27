import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Dialog {
    id: dialog
    width: 840
    height: 680
    modal: true
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    closePolicy: Popup.CloseOnEscape

    property var selectedMods: []
    property bool mcLoading: false
    property string editingBuildName: ""
    property string editingTargetVersion: ""

    function loadBuildForEditing(name) {
        dialog.editingBuildName = name

        var localBuilds = localBuildsManager.buildsArray()
        var target = null

        for (var i = 0; i < localBuilds.length; i++) {
            if (localBuilds[i].name === name) {
                target = localBuilds[i]
                break
            }
        }

        if (!target)
            return

        nameInput.text = target.name

        mcCombo.currentValue = target.mcV
        loaderCombo.currentValue = target.loader
        dialog.editingTargetVersion = target.v

        var installedMods = localBuildsManager.getInstalledModsForBuild(name)

        dialog.selectedMods = []
        chosenModel.clear()

        for (var j = 0; j < installedMods.length; j++) {
            dialog.selectedMods.push({
                id: installedMods[j].id,
                name: installedMods[j].name
            })

            chosenModel.append({
                title: installedMods[j].name,
                project_id: installedMods[j].id
            })
        }

        statusText.text = "Загружено модов: " + installedMods.length
        createBtnText.text = "СОХРАНИТЬ ИЗМЕНЕНИЯ"

        loaderVersionCombo.currentValue = target.v
        loaderVersionCombo.loading = false

        localBuildsManager.requestLoaderVersions(target.loader, target.mcV)
    }

    function modList() {
        if (loaderCombo.currentValue === "vanilla") return []
        return selectedMods === undefined ? [] : selectedMods
    }

    onClosed: {
        dialog.editingBuildName = ""
        dialog.editingTargetVersion = ""
        createBtnText.text = "СОЗДАТЬ СБОРКУ"
        resetForm()
    }

    FontLoader { id: mcFont; source: "qrc:/fonts/minecraft.ttf" }

    function addMod(projectId, title) {
        var arr = modList().slice()
        for (var i = 0; i < arr.length; i++) {
            if (arr[i].id === projectId) return
        }
        arr.push({id: projectId, name: title})
        selectedMods = arr
        chosenModel.append({ title: title, project_id: projectId })
    }

    function removeMod(idx) {
        var arr = modList().slice()
        arr.splice(idx, 1)
        selectedMods = arr
        chosenModel.remove(idx)
    }

    function mcValue() { return mcCombo.currentValue || "" }
    function loaderValue() { return loaderCombo.currentValue || "neoforge" }
    function loaderVersionValue() { return loaderVersionCombo.currentValue || "" }

    function refreshLoaderVersions() {
        if (dialog.editingBuildName !== "")
            return

        if (loaderValue() === "vanilla") {
            loaderVersionCombo.loading = false
            loaderVersionCombo.items = ["-"]
            loaderVersionCombo.currentValue = "-"
            return
        }

        loaderVersionCombo.loading = true
        loaderVersionCombo.items = []
        loaderVersionCombo.currentValue = ""

        localBuildsManager.requestLoaderVersions(
            loaderValue(),
            mcValue()
        )
    }

    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.65) }

    background: Rectangle {
        radius: 12
        color: "#141419"
        border.color: Qt.rgba(1, 1, 1, 0.08)
        border.width: 1
    }

    component FieldLabel : Text {
        font.pixelSize: 10
        font.bold: true
        font.letterSpacing: 1
        color: Qt.rgba(1, 1, 1, 0.4)
    }

    component DropdownButton : Item {
        id: dd
        property var items: []
        property string currentValue: ""
        property bool loading: false
        property bool menuOpen: false

        signal selected(string value)

        implicitHeight: 40

        Button {
            id: ddButton
            anchors.fill: parent
            flat: true
            hoverEnabled: true

            contentItem: RowLayout {
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    leftPadding: 12
                    text: dd.loading ? "Загрузка..." : (dd.currentValue || "—")
                    color: dd.loading ? Qt.rgba(1,1,1,0.35) : "#FFFFFF"
                    font.pixelSize: 13
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                Text {
                    rightPadding: 12
                    text: "▾"
                    color: ddButton.hovered ? "#FFF0B5" : Qt.rgba(1, 1, 1, 0.45)
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                }
            }

            background: Rectangle {
                radius: 8
                color: ddButton.down ? "#2A2A33" : (ddButton.hovered ? "#26262E" : "#1D1D24")
                border.color: ddButton.pressed ? "#FFF0B5" : Qt.rgba(1, 1, 1, 0.1)
                border.width: 1
                Behavior on border.color { ColorAnimation { duration: 120 } }
                Behavior on color { ColorAnimation { duration: 120 } }
            }

            onClicked: dd.menuOpen = !dd.menuOpen
        }

        Rectangle {
            id: ddMenu
            visible: dd.menuOpen && dd.items !== undefined && dd.items.length > 0
            y: dd.height + 4
            width: Math.max(dd.width, 180)
            height: Math.min(ddList.contentHeight + 8, 280)
            radius: 8
            color: "#1D1D24"
            border.color: Qt.rgba(1, 1, 1, 0.12)
            z: 1000

            ListView {
                id: ddList
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: dd.items !== undefined ? dd.items : []
                ScrollIndicator.vertical: ScrollIndicator {}

                delegate: ItemDelegate {
                    width: ddList.width
                    height: 34
                    highlighted: modelData === dd.currentValue

                    contentItem: Text {
                        leftPadding: 12
                        verticalAlignment: Text.AlignVCenter
                        text: modelData !== undefined ? modelData : ""
                        color: highlighted ? "#FFF0B5" : "#FFFFFF"
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                    background: Rectangle {
                        color: highlighted ? Qt.rgba(1, 1, 1, 0.07) : (hovered ? Qt.rgba(1,1,1,0.04) : "transparent")
                        radius: 4
                    }

                    onClicked: {
                        dd.currentValue = modelData
                        dd.selected(modelData)
                        dd.menuOpen = false
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                text: "СОЗДАНИЕ СБОРКИ"
                font.family: mcFont.name
                font.pixelSize: 15
                color: "#FFF0B5"
            }
            Item { Layout.fillWidth: true }
            Button {
                id: closeX
                width: 32; height: 32
                flat: true
                contentItem: Text { text: "×"; color: closeX.hovered ? "#FFFFFF" : Qt.rgba(1,1,1,0.5); font.pixelSize: 18; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: 6; color: closeX.hovered ? Qt.rgba(1,1,1,0.1) : "transparent" }
                onClicked: dialog.close()
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            FieldLabel { text: "НАЗВАНИЕ СБОРКИ" }
            TextField {
                id: nameInput
                Layout.fillWidth: true
                implicitHeight: 40
                placeholderText: "Например My Pack"
                placeholderTextColor: Qt.rgba(1, 1, 1, 0.25)
                color: "#FFFFFF"
                font.pixelSize: 13
                leftPadding: 12
                background: Rectangle {
                    radius: 8
                    color: nameInput.activeFocus ? "#26262E" : "#1D1D24"
                    border.color: nameInput.activeFocus ? "#FFF0B5" : Qt.rgba(1, 1, 1, 0.1)
                    border.width: 1
                    Behavior on border.color { ColorAnimation { duration: 120 } }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            z: 100

            ColumnLayout {
                Layout.preferredWidth: 170
                spacing: 6
                FieldLabel { text: "ВЕРСИЯ MINECRAFT" }
                DropdownButton {
                    id: mcCombo
                    Layout.fillWidth: true
                    enabled: dialog.editingBuildName === ""
                    loading: dialog.mcLoading
                    onSelected: dialog.refreshLoaderVersions()
                }
            }

            ColumnLayout {
                Layout.preferredWidth: 150
                spacing: 6
                FieldLabel { text: "ЗАГРУЗЧИК" }
                DropdownButton {
                    id: loaderCombo
                    Layout.fillWidth: true
                    enabled: dialog.editingBuildName === ""
                    currentValue: "neoforge"
                    items: ["neoforge", "forge", "fabric", "vanilla"]
                    onSelected: dialog.refreshLoaderVersions()
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                FieldLabel { text: "ВЕРСИЯ ЗАГРУЗЧИКА" }
                DropdownButton {
                    id: loaderVersionCombo
                    Layout.fillWidth: true
                    enabled: dialog.editingBuildName === ""
                    loading: false
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }

        ColumnLayout {
            Layout.fillWidth: true
            visible: loaderCombo.currentValue !== "vanilla"
            spacing: 6

            FieldLabel { text: "ПОИСК МОДОВ (MODRINTH)" }
            TextField {
                id: searchInput
                Layout.fillWidth: true
                implicitHeight: 40
                placeholderText: "Введите название мода..."
                placeholderTextColor: Qt.rgba(1, 1, 1, 0.25)
                color: "#FFFFFF"
                font.pixelSize: 13
                leftPadding: 12
                background: Rectangle {
                    radius: 8
                    color: searchInput.activeFocus ? "#26262E" : "#1D1D24"
                    border.color: searchInput.activeFocus ? "#FFF0B5" : Qt.rgba(1, 1, 1, 0.1)
                    border.width: 1
                    Behavior on border.color { ColorAnimation { duration: 120 } }
                }

                Timer {
                    id: searchTimer
                    interval: 400
                    onTriggered: {
                        if (searchInput.text.trim().length >= 2)
                            modrinthApi.searchMods(searchInput.text.trim(), dialog.mcValue(), dialog.loaderValue())
                    }
                }

                onTextChanged: searchTimer.restart()
                onAccepted: {
                    searchTimer.stop()
                    if (text.trim().length >= 2)
                        modrinthApi.searchMods(text.trim(), dialog.mcValue(), dialog.loaderValue())
                }
            }
        }

        Item {
            id: modSearcher
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: loaderCombo.currentValue !== "vanilla"

            ListView {
                id: searchList
                anchors.fill: parent
                clip: true
                model: searchModel
                spacing: 6

                delegate: Rectangle {
                    id: searchDelegate
                    width: searchList.width
                    height: 58
                    radius: 8
                    color: searchHover.hovered ? "#26262E" : "#1D1D24"
                    border.color: searchHover.hovered ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                    border.width: 1
                    Behavior on color { ColorAnimation { duration: 100 } }

                    HoverHandler { id: searchHover }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            radius: 6
                            color: "#22222A"
                            clip: true

                            Image {
                                id: modIcon
                                anchors.fill: parent
                                source: (model.icon_url || "").toString().replace(/\.webp(\?|$)/, ".png$1")
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                onStatusChanged: {
                                    if (status === Image.Error) source = ""
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: modIcon.status !== Image.Ready
                                text: model.title ? model.title.charAt(0).toUpperCase() : "?"
                                color: "#FFF0B5"
                                font.pixelSize: 18
                                font.bold: true
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: model.title; color: "#FFFFFF"; font.pixelSize: 13; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                            Text { text: model.description; color: Qt.rgba(1, 1, 1, 0.45); font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                        }

                        Text {
                            text: (model.downloads / 1000000).toFixed(1) + "M"
                            color: Qt.rgba(1, 1, 1, 0.35)
                            font.pixelSize: 10
                        }

                        Button {
                            id: addBtn
                            width: 30; height: 30
                            flat: true
                            contentItem: Text { text: "+"; color: addBtn.hovered ? "#000000" : "#FFF0B5"; font.pixelSize: 16; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { radius: 6; color: addBtn.hovered ? "#FFF0B5" : Qt.rgba(255, 240, 181, 0.12) }
                            onClicked: dialog.addMod(model.project_id, model.title)
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: searchModel.count === 0
                text: searchInput.text.trim().length > 0 ? "(◐ω◑)" : "Найдите моды через поле поиска выше"
                color: Qt.rgba(1, 1, 1, 0.25)
                font.pixelSize: 12
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            radius: 8
            color: "#1D1D24"
            border.color: Qt.rgba(1, 1, 1, 0.08)
            border.width: 1
            visible: loaderCombo.currentValue !== "vanilla"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6

                FieldLabel { text: "ВЫБРАННЫЕ МОДЫ: " + chosenModel.count }

                ListView {
                    id: chosenList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: chosenModel
                    spacing: 4

                    delegate: Rectangle {
                        width: chosenList.width
                        height: 30
                        radius: 6
                        color: "#22222A"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 4
                            Text { text: model.title; color: "#FFFFFF"; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                            Button {
                                id: rmBtn
                                width: 24; height: 24
                                flat: true
                                contentItem: Text { text: "×"; color: rmBtn.hovered ? "#FFFFFF" : Qt.rgba(1,1,1,0.4); font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { radius: 4; color: rmBtn.hovered ? Qt.rgba(0.8, 0.2, 0.2, 0.35) : "transparent" }
                                onClicked: dialog.removeMod(index)
                            }
                        }
                    }
                }
            }
        }

        Text {
            id: statusText
            Layout.fillWidth: true
            color: "#FFF0B5"
            font.pixelSize: 11
            visible: text !== ""
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Item { Layout.fillWidth: true }

            Button {
                id: cancelBtn
                implicitWidth: 130
                implicitHeight: 42
                flat: true
                contentItem: Text { text: "ОТМЕНА"; color: cancelBtn.hovered ? "#FFFFFF" : Qt.rgba(1,1,1,0.6); font.pixelSize: 12; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle {
                    radius: 8
                    color: cancelBtn.hovered ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.03)
                    border.color: Qt.rgba(1, 1, 1, 0.12)
                    border.width: 1
                }
                onClicked: dialog.close()
            }

            Button {
                id: createBtn
                implicitWidth: 200
                implicitHeight: 42
                flat: true
                enabled: nameInput.text.trim().length > 0
                         && (dialog.loaderValue() === "vanilla" || dialog.loaderVersionValue().length > 0)

                contentItem: Item {
                    Text {
                        id: createBtnText
                        text: "СОЗДАТЬ СБОРКУ"
                        color: createBtn.hovered ? "#000000" : "#2e2e2e"
                        font.family: mcFont.name
                        font.pixelSize: 13
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: 2
                        anchors.verticalCenterOffset: 2
                        opacity: createBtn.enabled ? 1 : 0.4
                    }
                    Text {
                        text: createBtnText.text
                        color: "#FFFFFF"
                        font.family: mcFont.name
                        font.pixelSize: 13
                        anchors.centerIn: parent
                        opacity: createBtn.enabled ? 1 : 0.4
                    }
                }

                background: Rectangle {
                    radius: 2
                    color: !createBtn.enabled ? "#2A3A25"
                         : (createBtn.down ? "#2E671E" : (createBtn.hovered ? "#479A2F" : "#3C8527"))
                    border.color: createBtn.enabled ? "#55B635" : "#3A4A35"
                    border.width: 2

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 3
                        color: "#235218"
                        radius: 1
                    }
                }

                onClicked: {
                    var finalLoaderVersion = dialog.loaderVersionValue()
                    if (finalLoaderVersion === "" && dialog.editingBuildName !== "") {
                        finalLoaderVersion = dialog.editingTargetVersion
                    }

                    if (dialog.editingBuildName !== "") {
                        localBuildsManager.editBuild(
                            dialog.editingBuildName,
                            nameInput.text,
                            dialog.mcValue(),
                            dialog.loaderValue(),
                            finalLoaderVersion,
                            dialog.modList()
                        )
                    } else {
                        localBuildsManager.createBuild(nameInput.text, dialog.mcValue(),
                                                       dialog.loaderValue(), finalLoaderVersion,
                                                       dialog.modList())
                    }
                }
            }
        }
    }

    ListModel { id: searchModel }
    ListModel { id: chosenModel }

    onOpened: {
        if (dialog.editingBuildName !== "") {
            mcLoading = false
            loaderVersionCombo.loading = false
            return
        }

        mcLoading = true
        loaderVersionCombo.loading = true
        localBuildsManager.requestMinecraftVersions()
    }

    function resetForm() {
        statusText.text = ""
        dialog.selectedMods = []
        chosenModel.clear()
        searchModel.clear()
        nameInput.text = ""
    }

    Connections {
        target: qmlHandler

        function onInstallProgress(stage, details, progress) {
            if (dialog.visible)
                dialog.close()
        }
    }

    Connections {
        target: modrinthApi
        function onSearchCompleted(projects) {
            searchModel.clear()
            var list = projects !== undefined ? projects : []
            for (var i = 0; i < list.length; i++) {
                searchModel.append({
                    project_id: list[i].project_id,
                    title: list[i].title,
                    description: list[i].description,
                    icon_url: list[i].icon_url,
                    downloads: list[i].downloads
                })
            }
        }
    }

    Connections {
        target: localBuildsManager

        function onMinecraftVersionsReady(versions) {
            if (dialog.editingBuildName !== "")
                return

            mcLoading = false

            var list = versions !== undefined ? versions : []
            mcCombo.items = list

            if (list.length > 0 && dialog.mcValue() === "")
                mcCombo.currentValue = list[0]

            dialog.refreshLoaderVersions()
        }

        function onLoaderVersionsReady(versions) {
            loaderVersionCombo.loading = false

            if (dialog.editingBuildName !== "") {
                loaderVersionCombo.items = versions.length > 0 ? versions : [dialog.editingTargetVersion]
                loaderVersionCombo.currentValue = dialog.editingTargetVersion
                return
            }

            loaderVersionCombo.items = versions.length > 0 ? versions : [""]

            if (versions.length > 0)
                loaderVersionCombo.currentValue = versions[0]
        }

        function onBuildsChanged() {
           rebuildBuildsModel()
        }

        function onCreationFinished(name) {
           dialog.resetForm()
           dialog.close()
           rebuildBuildsModel()
        }

        function onCreationError(message) {
           statusText.text = "Ошибка: " + message
        }
    }
}