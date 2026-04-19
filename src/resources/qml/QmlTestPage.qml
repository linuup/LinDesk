import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    color: "#eef3f8"

    Rectangle {
        anchors.fill: parent
        anchors.margins: 20
        radius: 18
        color: "#ffffff"
        border.color: "#d8e1ec"
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16

            Text {
                text: "QML is active"
                font.pixelSize: 28
                font.bold: true
                color: "#1f2937"
            }

            Text {
                text: "This test page is rendered by Qt Quick through QQuickWidget."
                font.pixelSize: 14
                color: "#5b6472"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                radius: 16
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#0ea5e9" }
                    GradientStop { position: 1.0; color: "#2563eb" }
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 8

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "LinDesk"
                        color: "white"
                        font.pixelSize: 26
                        font.bold: true
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "Qt Widgets + QML hybrid test"
                        color: "#dbeafe"
                        font.pixelSize: 13
                    }
                }
            }

            RowLayout {
                spacing: 12

                Button {
                    text: "Primary"
                }

                Button {
                    text: "Secondary"
                    highlighted: true
                }
            }

            Item {
                Layout.fillHeight: true
            }
        }
    }
}
