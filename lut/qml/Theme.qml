pragma Singleton
import QtQuick
//定义界面中所有的颜色值
QtObject {
    // Primary palette - Indigo
    readonly property color primary: "#3F51B5"
    readonly property color primaryDark: "#303F9F"
    readonly property color primaryLight: "#C5CAE9"

    // Accent palette - Teal
    readonly property color accent: "#009688"
    readonly property color accentLight: "#B2DFDB"

    // Backgrounds
    readonly property color background: "#FAFAFA"
    readonly property color surface: "#FFFFFF"

    // Semantic colors
    readonly property color error: "#F44336"
    readonly property color warning: "#FF9800"
    readonly property color success: "#4CAF50"

    // Text colors
    readonly property color textPrimary: "#212121"
    readonly property color textSecondary: "#757575"

    // Divider
    readonly property color divider: "#BDBDBD"
}
