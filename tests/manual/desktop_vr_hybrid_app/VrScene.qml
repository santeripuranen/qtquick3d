// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

import QtQuick
import QtQuick3D
import QtQuick3D.Xr

XrView {
    id: xrView

    quitOnSessionEnd: false

    signal sessionEndRequested()

    environment: SceneEnvironment {
        clearColor: "black"
        backgroundMode: SceneEnvironment.Color
    }

    XrActor {
        id: actor
        position: Qt.vector3d(50, 2, 50)

        LeftHand {
            id: left
        }

        RightHand {
            id: right
            view3d: xrView.view3d
        }
    }

    XrVirtualMouse {
        view: xrView
        source: right
        enabled: true
        leftMouseButton: right.handInput.triggerValue > 0.8
        rightMouseButton: right.handInput.button1Pressed
        middleMouseButton: right.handInput.button2Pressed
    }

    XrVirtualMouse {
        view: xrView
        source: left
        enabled: true
        leftMouseButton: left.handInput.triggerValue > 0.8
        rightMouseButton: left.handInput.button1Pressed
        middleMouseButton: left.handInput.button2Pressed
    }

    DirectionalLight {

    }

    Model {
        source: "#Cube"
        materials: DefaultMaterial {
            diffuseColor: "red"
        }

        y: -10

        scale: Qt.vector3d(0.1, 0.1, 0.1)


        NumberAnimation  on eulerRotation.y {
            duration: 10000
            easing.type: Easing.InOutQuad
            from: 0
            to: 360
            running: true
            loops: -1
        }
    }

    Node {
        x: 10
        y: 20
        Text {
            text: "Qt 6 in VR"
            font.pointSize: 12
            color: "white"
        }
        Text {
            text: "On " + xrView.runtimeInfo.runtimeName + " " + xrView.runtimeInfo.runtimeVersion + " with " + xrView.runtimeInfo.graphicsApiName + "\nCheck right hand for perf. info"
            font.pointSize: 4
            color: "white"
            y: 20
        }
        Rectangle {
            y: 40
            radius: 8
            width: 60
            height: 25
            id: btnRect
            color: "gray"
            Text {
                text: "Click to end VR session"
                anchors.centerIn: parent
                font.pointSize: 4
            }
            MouseArea {
                anchors.fill: parent
                onClicked: xrView.sessionEndRequested()
                hoverEnabled: true
                onEntered: btnRect.color = "red"
                onExited: btnRect.color = "gray"
            }
        }
    }
}
