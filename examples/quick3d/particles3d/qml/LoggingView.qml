/****************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the examples of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** BSD License Usage
** Alternatively, you may use this file under the terms of the BSD license
** as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of The Qt Company Ltd nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

import QtQuick
import QtQuick3D.Particles3D

Item {
    property var particleSystems
    readonly property bool loggingEnabled: rootWindow.showLoggingView
    property bool intervalInstant: false
    property real itemWidth: (width - loggingButton.width - intervalButton.width) / 6

    width: parent.width
    height: tableContent.height + 30

    Component.onCompleted: {
        for (const psystem of particleSystems)
            psystem.logging = rootWindow.showLoggingView;
    }

    // Background
    Rectangle {
        color: "#80000000"
        anchors.fill: parent
        visible: loggingEnabled
    }

    Item {
        id: loggingButton
        height: parent.height
        width: 40
        anchors.right: parent.right
        anchors.rightMargin: 10
        Image {
            anchors.centerIn: parent
            width: 32
            height: 32
            source: "images/icon_logging.png"
            opacity: loggingEnabled ? 1.0 : 0.4
            mipmap: true
        }
        MouseArea {
            anchors.fill: parent
            onClicked: {
                rootWindow.showLoggingView = !rootWindow.showLoggingView
                for (const psystem of particleSystems) {
                    psystem.logging = rootWindow.showLoggingView;
                }
            }
        }
    }

    Item {
        id: intervalButton
        height: parent.height
        width: 40
        anchors.right: loggingButton.left
        anchors.rightMargin: 0
        visible: loggingEnabled
        Image {
            anchors.centerIn: parent
            width: 32
            height: 32
            source: "images/icon_interval.png"
            opacity: intervalInstant ? 1.0 : 0.2
            mipmap: true
        }
        MouseArea {
            anchors.fill: parent
            anchors.margins: -10
            onClicked: {
                intervalInstant = !intervalInstant;
                var interval = intervalInstant ? 0 : 1000;
                for (const psystem of particleSystems)
                    psystem.loggingData.loggingInterval = interval;
            }
        }
    }

    Component {
        id: systemItem
        Row {
            Text {
                width: itemWidth
                horizontalAlignment: Text.AlignHCenter
                color: "#ffffff"
                text: modelData.seed
            }
            Text {
                width: itemWidth
                horizontalAlignment: Text.AlignHCenter
                color: "#ffffff"
                text: modelData.loggingData.updates
            }
            Text {
                width: itemWidth
                horizontalAlignment: Text.AlignHCenter
                color: "#ffffff"
                text: modelData.loggingData.particlesMax
            }
            Text {
                width: itemWidth
                horizontalAlignment: Text.AlignHCenter
                color: "#ffffff"
                text: modelData.loggingData.particlesUsed
            }
            Text {
                width: itemWidth
                horizontalAlignment: Text.AlignHCenter
                color: "#ffffff"
                text: modelData.loggingData.time.toFixed(4)
            }
            Text {
                width: itemWidth
                horizontalAlignment: Text.AlignHCenter
                color: "#ffffff"
                text: modelData.loggingData.timeAverage.toFixed(4)
            }
        }
    }

    Column {
        id: tableContent
        width: parent.width
        anchors.verticalCenter: parent.verticalCenter
        visible: loggingEnabled
        Row {
            Text {
                width: itemWidth
                horizontalAlignment: Text.AlignHCenter
                color: "#ffffff"
                text: qsTr("SEED")
            }
            Text {
                width: itemWidth
                horizontalAlignment: Text.AlignHCenter
                color: "#ffffff"
                text: qsTr("UPDATES")
            }
            Text {
                width: itemWidth
                horizontalAlignment: Text.AlignHCenter
                color: "#ffffff"
                text: qsTr("PARTICLES MAX")
            }
            Text {
                width: itemWidth
                horizontalAlignment: Text.AlignHCenter
                color: "#ffffff"
                text: qsTr("PARTICLES USED")
            }
            Text {
                width: itemWidth
                horizontalAlignment: Text.AlignHCenter
                color: "#ffffff"
                text: qsTr("TIME")
            }
            Text {
                width: itemWidth
                horizontalAlignment: Text.AlignHCenter
                color: "#ffffff"
                text: qsTr("TIME AVG.")
            }
        }
        Repeater {
            model: particleSystems
            delegate: systemItem
        }
    }
}
