pragma Singleton

import QtQuick

QtObject {
    id: theme

    // 0 = follow the system, 1 = dark, -1 = light
    property int scheme: 0
    readonly property bool dark:
        scheme === 1 || (scheme === 0 && Application.styleHints.colorScheme === Qt.Dark)

    readonly property color window:     dark ? "#1f1f1f" : "#f0f0f0"
    readonly property color surface:    dark ? "#262626" : "#ffffff"
    readonly property color surfaceAlt: dark ? "#2e2e2e" : "#f4f4f4"
    readonly property color border:     dark ? "#3a3a3a" : "#d0d0d0"
    readonly property color text:       dark ? "#e7e7e7" : "#1f1f1f"
    readonly property color textMuted:  dark ? "#9a9a9a" : "#6a6a6a"

    readonly property color accent: "#3d7eff"
    readonly property color ok:     "#3fb950"
    readonly property color warn:   "#d29922"
    readonly property color error:  "#f85149"

    readonly property string mono: "Consolas"
}
