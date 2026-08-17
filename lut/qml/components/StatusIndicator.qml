import QtQuick
import QtQuick.Layouts
//	圆形指示灯（带发光效果）
Rectangle {
    id: root

    property string status: "unknown"  // "online", "offline", "fault", "unknown"
    property int size: 12

    width: size
    height: size
    radius: size / 2

    color: {
        switch (status) {
        case "online":  return "#4CAF50"
        case "fault":   return "#F44336"
        case "offline": return "#9E9E9E"
        case "unknown": return "#FFC107"
        default:        return "#9E9E9E"
        }
    }

    // Subtle glow effect for online status
    Rectangle {
        anchors.centerIn: parent
        width: parent.width + 4
        height: parent.height + 4
        radius: width / 2
        color: "transparent"
        border.width: 1
        border.color: parent.color
        opacity: root.status === "online" ? 0.4 : 0
        visible: root.status === "online"
    }
}
