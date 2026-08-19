import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material
import QtQuick.Dialogs
import LST.Data 1.0

Rectangle {
    id: root
    color: "#FAFAFA"

    property var historyMgr: null

    // Selection state
    property bool editMode: false
    property bool allSelected: false
    property var selectedIndices: []

    // Top info bar
    Rectangle {
        id: infoBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 40
        color: "#E8EAF6"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 8

            Label {
                text: qsTr("历史记录")
                font.pixelSize: 16
                font.bold: true
                color: "#3F51B5"
            }

            Item { Layout.fillWidth: true }

            Label {
                text: qsTr("记录数: %1 / 100").arg(historyMgr ? historyMgr.recordCount : 0)
                font.pixelSize: 12
                color: "#757575"
            }

            ToolTip {
                id: infoTip
                text: qsTr("历史记录最多保存 100 条，超出将自动覆盖最旧记录")
                delay: 500
                visible: infoMouseArea.containsMouse
            }

            Label {
                text: "ⓘ"
                font.pixelSize: 16
                color: "#3F51B5"

                MouseArea {
                    id: infoMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                }
            }
        }
    }

    // Top action bar
    Rectangle {
        id: actionBar
        anchors.top: infoBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 48
        color: "#FFFFFF"
        border.color: "#E0E0E0"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 4

            Button {
                text: editMode ? qsTr("退出") : qsTr("编辑")
                Layout.preferredWidth: 80
                Layout.preferredHeight: 45
                font.pixelSize: 12
                Material.background: editMode ? "#F44336" : "#3F51B5"
                Material.foreground: "white"
                onClicked: {
                    editMode = !editMode
                    if (!editMode) {
                        allSelected = false
                        selectedIndices = []
                    }
                }
            }

            Button {
                text: allSelected ? qsTr("取消") : qsTr("全选")
                Layout.preferredWidth: 80
                Layout.preferredHeight: 45
                font.pixelSize: 12
                Material.background: "#3F51B5"
                Material.foreground: "white"
                enabled: editMode && historyMgr && historyMgr.recordCount > 0
                onClicked: {
                    allSelected = !allSelected
                    if (allSelected) {
                        var newSel = []
                        for (var i = 0; i < historyMgr.recordCount; i++) {
                            newSel.push(i)
                        }
                        selectedIndices = newSel
                    } else {
                        selectedIndices = []
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("保存")
                Layout.preferredWidth: 80
                Layout.preferredHeight: 45
                font.pixelSize: 12
                Material.background: "#4CAF50"
                Material.foreground: "white"
                enabled: editMode && selectedIndices.length > 0
                onClicked: exportDialog.open()
            }

            Button {
                text: qsTr("删除")
                Layout.preferredWidth: 80
                Layout.preferredHeight: 45
                font.pixelSize: 12
                Material.background: "#F44336"
                Material.foreground: "white"
                enabled: editMode && selectedIndices.length > 0
                onClicked: deleteConfirmDialog.open()
            }

            Button {
                text: qsTr("浏览")
                Layout.preferredWidth: 80
                Layout.preferredHeight: 45
                font.pixelSize: 12
                Material.background: "#3F51B5"
                Material.foreground: "white"
                enabled: historyMgr != null
                onClicked: importFileDialog.open()
            }
        }
    }

    // Record list
    ListView {
        id: listView
        anchors.top: actionBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        clip: true
        spacing: 4

        model: historyMgr ? historyMgr.recordCount : 0
        delegate: recordDelegate

        // Empty state
        Label {
            anchors.centerIn: parent
            text: qsTr("暂无历史记录")
            color: "#BDBDBD"
            font.pixelSize: 16
            visible: listView.count === 0
        }
    }

    Component {
        id: recordDelegate

        Rectangle {
            width: listView.width
            height: 48
            radius: 4
            color: mouseArea.containsMouse ? "#F5F5F5" : "#FFFFFF"
            border.color: "#E0E0E0"

            property var recordData: historyMgr ? historyMgr.getRecord(index) : ({})

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 8

                // Checkbox (only in edit mode)
                CheckBox {
                    id: itemCheck
                    property bool __guard: false
                    Layout.preferredWidth: 32
                    visible: editMode

                    onToggled: {
                        if (__guard) return
                        __guard = true
                        if (checked) {
                            var found = false
                            for (var j = 0; j < selectedIndices.length; j++) {
                                if (selectedIndices[j] === index) { found = true; break }
                            }
                            if (!found) {
                                var newSel = selectedIndices.slice()
                                newSel.push(index)
                                selectedIndices = newSel
                            }
                        } else {
                            var newArr = []
                            for (var k = 0; k < selectedIndices.length; k++) {
                                if (selectedIndices[k] !== index) newArr.push(selectedIndices[k])
                            }
                            selectedIndices = newArr
                            allSelected = false
                        }
                        __guard = false
                    }

                    function syncFromSelection() {
                        var shouldBe = false
                        for (var i = 0; i < selectedIndices.length; i++) {
                            if (selectedIndices[i] === index) { shouldBe = true; break }
                        }
                        if (checked !== shouldBe) {
                            __guard = true
                            checked = shouldBe
                            __guard = false
                        }
                    }

                    Component.onCompleted: syncFromSelection()
                }

                Connections {
                    target: root
                    function onSelectedIndicesChanged() {
                        itemCheck.syncFromSelection()
                    }
                }

                // Title
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        text: recordData ? recordData.title : ""
                        font.pixelSize: 14
                        font.bold: true
                        color: "#212121"
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Label {
                        text: {
                            if (!recordData) return ""
                            return qsTr("点数: %1").arg(recordData.totalPoints || 0)
                        }
                        font.pixelSize: 11
                        color: "#9E9E9E"
                    }
                }

                // Action buttons
                Button {
                    text: qsTr("绘制")
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 45
                    font.pixelSize: 12
                    Material.background: "#3F51B5"
                    Material.foreground: "white"
                    onClicked: {
                        if (historyMgr) {
                            historyMgr.loadRecordData(index)
                            drawerLoader.recordIndex = index
                            drawerLoader.recordTitle = recordData ? recordData.title : ""
                            drawerLoader.active = true
                            drawer.open()
                        }
                    }
                }

                Button {
                    text: qsTr("命名")
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 45
                    font.pixelSize: 12
                    Material.background: "#757575"
                    Material.foreground: "white"
                    onClicked: {
                        renameDialog.recordIndex = index
                        renameDialog.titleField.text = recordData ? recordData.title : ""
                        renameDialog.open()
                    }
                }
            }

            MouseArea {
                id: mouseArea
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.NoButton
            }
        }
    }

    // Drawer for graph
    Drawer {
        id: drawer
        width: Math.min(root.width * 0.7, 800)
        height: root.height
        edge: Qt.RightEdge
        interactive: false
        closePolicy: Drawer.NoAutoClose

        Rectangle {
            anchors.fill: parent
            color: "#FAFAFA"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Drawer header
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    color: "#3F51B5"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 8

                        Label {
                            text: qsTr("厚度波形")
                            font.pixelSize: 16
                            font.bold: true
                            color: "white"
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            text: qsTr("取消")
                            Layout.preferredWidth: 80
                            Layout.preferredHeight: 45
                            font.pixelSize: 12
                            Material.background: "#FFFFFF"
                            Material.foreground: "#3F51B5"
                            onClicked: {
                                drawerLoader.active = false
                                drawer.close()
                            }
                        }
                    }
                }

                // Graph area
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 8

                    Loader {
                        id: drawerLoader
                        anchors.fill: parent
                        active: false

                        property int recordIndex: -1
                        property string recordTitle: ""

                        sourceComponent: Rectangle {
                            anchors.fill: parent
                            color: "black"

                            HistoryGraphItem {
                                id: graphItem
                                anchors.fill: parent
                                anchors.margins: 2

                                Component.onCompleted: {
                                    if (historyMgr) {
                                        var rawData = historyMgr.getRecordData(recordIndex)
                                        setData(rawData)
                                        setTitle(recordTitle)
                                        var interpolated = historyMgr.interpolateData(rawData)
                                        setInterpolatedData(interpolated)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Rename dialog
    Dialog {
        id: renameDialog
        title: qsTr("修改标题")
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal: true
        x: (parent.width - width) / 2
        y: (parent.height - height) / 3
        width:350

        property int recordIndex: -1
        property alias titleField: renameField

        onAccepted: {
            if (historyMgr && recordIndex >= 0) {
                historyMgr.setRecordTitle(recordIndex, renameField.text)
            }
        }

        TextField {
            id: renameField
            width: 300
            placeholderText: qsTr("输入新标题")
            font.pixelSize: 14
        }
    }

    // Delete confirmation dialog
    Dialog {
        id: deleteConfirmDialog
        title: qsTr("确认删除")
        standardButtons: Dialog.Yes | Dialog.No
        modal: true
        x: (parent.width - width) / 2
        y: (parent.height - height) / 3

        Label {
            text: qsTr("确定要删除选中的 %1 条记录吗？\n此操作不可恢复。").arg(selectedIndices.length)
            font.pixelSize: 14
            wrapMode: Text.WordWrap
        }

        onAccepted: {
            if (historyMgr) {
                var indices = []
                for (var i = 0; i < selectedIndices.length; i++) {
                    indices.push(selectedIndices[i])
                }
                historyMgr.deleteRecords(indices)
                selectedIndices = []
                allSelected = false
            }
        }
    }

    // Export dialog
    Dialog {
        id: exportDialog
        title: qsTr("导出 CSV")
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal: true
        x: (parent.width - width) / 2
        y: (parent.height - height) / 3

        ColumnLayout {
            spacing: 12

            Label {
                text: qsTr("选中了 %1 条记录").arg(selectedIndices.length)
                font.pixelSize: 14
            }

            RowLayout {
                spacing: 8
                RadioButton {
                    id: radioMerge
                    text: qsTr("合并到一个文件")
                    checked: true
                }
                RadioButton {
                    id: radioSeparate
                    text: qsTr("每个记录独立文件")
                }
            }
        }

        onAccepted: {
            if (historyMgr) {
                var indices = []
                for (var i = 0; i < selectedIndices.length; i++) {
                    indices.push(selectedIndices[i])
                }

                if (radioMerge.checked) {
                    saveMergeDialog.open()
                } else {
                    saveSeparateDialog.open()
                }
            }
        }
    }

    // Export file dialog (merged)
    FileDialog {
        id: saveMergeDialog
        title: qsTr("保存合并 CSV 文件")
        nameFilters: ["CSV Files (*.csv)", "All Files (*)"]
        defaultSuffix: "csv"
        fileMode: FileDialog.SaveFile
        onAccepted: {
            if (historyMgr) {
                var indices = []
                for (var i = 0; i < selectedIndices.length; i++) {
                    indices.push(selectedIndices[i])
                }
                historyMgr.exportToCsv(indices, selectedFile.toString().replace("file:///", ""), true)
            }
        }
    }

    // Export file dialog (separate) - save to a directory
    FolderDialog {
        id: saveSeparateDialog
        title: qsTr("选择保存目录")
        onAccepted: {
            if (historyMgr) {
                var dir = selectedFolder.toString().replace("file:///", "")
                var indices = []
                for (var i = 0; i < selectedIndices.length; i++) {
                    indices.push(selectedIndices[i])
                }
                // Export each record as separate file
                for (var j = 0; j < indices.length; j++) {
                    var rec = historyMgr.getRecord(indices[j])
                    var fileName = dir + "/" + (rec.title || "record_" + (j+1)) + ".csv"
                    historyMgr.exportToCsv([indices[j]], fileName, false)
                }
            }
        }
    }

    // Import file dialog
    FileDialog {
        id: importFileDialog
        title: qsTr("选择要导入的 CSV 文件")
        nameFilters: ["CSV Files (*.csv)", "All Files (*)"]
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            if (historyMgr) {
                var files = selectedFiles
                for (var i = 0; i < files.length; i++) {
                    var path = files[i].toString().replace("file:///", "")
                    historyMgr.importFromCsv(path)
                }
            }
        }
    }

    // Connections
    Connections {
        target: historyMgr
        function onErrorOccurred(message) {
            console.log("History Error:", message)
            toastMessage = message
            toast.open()
            toastTimer.start()
        }
    }

    // Toast notification
    property string toastMessage: ""

    Popup {
        id: toast
        x: (parent.width - width) / 2
        y: parent.height - 80
        width: 300
        height: 40
        closePolicy: Popup.CloseOnEscape

        Timer {
            id: toastTimer
            interval: 3000
            onTriggered: toast.close()
        }

        background: Rectangle {
            color: "#323232"
            radius: 4
        }

        Label {
            anchors.centerIn: parent
            text: toastMessage
            color: "white"
            font.pixelSize: 13
        }
    }
}