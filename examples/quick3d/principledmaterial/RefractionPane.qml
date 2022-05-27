/****************************************************************************
**
** Copyright (C) 2022 The Qt Company Ltd.
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
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick3D

ScrollView {
    id: rootView
    required property Material targetMaterial
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    width: availableWidth
    property bool specularGlossyMode: false

    ColumnLayout {
        width: rootView.availableWidth

        MarkdownLabel {
            text: "# Refraction
The properties in this section would probably be best described as advanced
transparency. In the previous section on transparency we discussed alpha
blending, which is about blending colors together using the alpha channel of
the material's color. What makes the transparency effects in this section
different is that the goal is to handle transparency in a way that more
physically represents how light works. To achieve this blending requires that
all content that is blended with needs to be rendered to a texture in separate
pass. Using any properties on this page is as expensive as rendering all opaque
content in the scene at least twice. Once to get the background items, and again
including the items using the refractive transparency effects. The advantage of
this approach though is that we are not limited in how we can blend, but comes
with the caveat that only opaque items are visible through refracted objects."
        }

        MarkdownLabel {
            text: "## Transmission
Transmission refers to lights ability to transmit, or pass through a surface.  Not all
light will penetrate a surface and some will still be reflected as a specular
reflection. This ability to transmit light only concerns the surface of a material, and
not its depth. Without the use of further properties in this section, a material that
has transmission alone can be assumed to be infinitely thin.
### Transmission Factor
The Transmission Factor property controls the percentage of light that is transmitted by
a materials surface.  This value is a single value between 0.0 meaning no light is
transmitted and 1.0 meaning that 100% of the light that penetrates the surface of the
material is transmitted through.

Note: If you adjust the Transmission Factor to 1.0 and you still can't see
through the models, it could be that your material is metallic. Metallic materials
cannot transmit light."
        }
        Button {
            text: "Reset Metalness to 0.0"
            onClicked: targetMaterial.metalness = 0.0
        }

        RowLayout {
            Label {
                text: "Transmission Factor (" + targetMaterial.transmissionFactor.toFixed(2) + ")"
                Layout.fillWidth: true
            }
            Slider {
                from: 0
                to: 1
                value: targetMaterial.transmissionFactor
                onValueChanged: targetMaterial.transmissionFactor = value
            }
        }
        MarkdownLabel {
            text: "### Transmission Map
Like most other single floating point value properties, the Transmission property
also allows for the use of a single channel of a texture to map transmission values
to a mesh. And like many other textures, the final value of transmission will be
the multiplication of Transmission Factor and the value sampled from Transmission Map.
So when using a Transmission Map, it typically makes sense to set the Transmission
Factor to 1.0.
"
        }
        Button {
            text: "Reset Transmission"
            onClicked: targetMaterial.transmissionFactor = 1.0
        }

        ComboBox {
            id: transmissionChannelComboBox
            textRole: "text"
            valueRole: "value"
            implicitContentWidthPolicy: ComboBox.WidestText
            onActivated: targetMaterial.transmissionChannel = currentValue
            Component.onCompleted: currentIndex = indexOfValue(targetMaterial.transmissionChannel)
            model: [
                { value: PrincipledMaterial.R, text: "Red Channel"},
                { value: PrincipledMaterial.G, text: "Green Channel"},
                { value: PrincipledMaterial.B, text: "Blue Channel"},
                { value: PrincipledMaterial.A, text: "Alpha Channel"}
            ]
        }
        TextureSourceControl {
            defaultTexture: "maps/noise.png"
            defaultClearColor: "black"
            onTargetTextureChanged: {
                targetMaterial.transmissionMap = targetTexture
            }
        }

        VerticalSectionSeparator {}

        ColumnLayout {
            width: rootView.availableWidth
            visible: !rootView.specularGlossyMode
            MarkdownLabel {
                text: "## Index of Refraction (IOR)
The Index of Refraction or refraction index refers to the physical property of
how fast light passes through a material. This number then is used to determine
how light is bent or refracted when it enters a material. Since this value is
a physical value, it's possible to plug in the same values as real life
materials as well. The default value that the PrincipledMaterial uses for all
lighting calculations is 1.5, which is very close to window glass. Below are
several other materials' IOR values that will produce different results when
used with a refractive material (especially ones with thickness)."
            }

            ComboBox {
                id: iorChannelComboBox
                textRole: "text"
                valueRole: "value"
                implicitContentWidthPolicy: ComboBox.WidestText
                onActivated: targetMaterial.indexOfRefraction = currentValue
                Component.onCompleted: currentIndex = 0
                model: [
                    { value: 1.5, text: "Custom"},
                    { value: 1.4, text: "Acrylic glass"},
                    { value: 1.0, text: "Air"},
                    { value: 1.33, text: "Water"},
                    { value: 1.76, text: "Sapphire"},
                    { value: 2.42, text: "Diamond"}
                ]
            }
            RowLayout {
                Label {
                    text: "IOR (" + iorSlider.value.toFixed(2) + ")"
                    Layout.fillWidth: true
                }
                Slider {
                    id: iorSlider
                    from: 1.0
                    to: 3.0
                    value: targetMaterial.indexOfRefraction ?? 1.5
                    onValueChanged: {
                        if (iorChannelComboBox.currentValue != value)
                            iorChannelComboBox.currentIndex = 0;
                        targetMaterial.indexOfRefraction = value
                    }
                }
            }

            VerticalSectionSeparator {}
        }

        MarkdownLabel {
            text: "## Thickness
The Thickness properties are for giving refractive materials volume.  A
transmissive material alone is considered to be infinitely thin so any
Index of Refraction values will only affect the specular and fresnel
effects of a material.  However when a transmissive material is given
volume via the thickness properties, then light passing through the
material is bent as it passes through.
### Thickness Factor
The Thickness Factor property defines the thickness of the volume beneath
the surface of the mesh.  Unlike other factors, the Thickness Factor
Property is not clipped at 1.0, but rather refers to the distance in the
coordinate space of the mesh itself.  When used in conjunction with the
Thickness Map, the Thickness Factor would be the point of maximum thickness.
"
        }

        RowLayout {
            Label {
                text: "Thickness Factor (" + targetMaterial.thicknessFactor.toFixed(2) + ")"
                Layout.fillWidth: true
            }
            Slider {
                from: 0
                to: 100.0
                value: targetMaterial.thicknessFactor
                onValueChanged: targetMaterial.thicknessFactor = value
            }
        }

        MarkdownLabel {
            text: "### Thickness Map
The Thickness Map is a single channel (greyscale) texture that defines
the thickness (or volume) of a mesh.  The values sampled from the
Thickness Map are multiplied against the value of Thickness Factor to
get the thickness of the mesh under the surface in the meshe's coordinate
space.  Thickness Maps are baked in 3D content creation tools using ray
tracers. The process of baking thickness is similar to the process for
baking ambient occlusion, but the rays are cast in the opposite direction
of the surface normal (into the mesh).  Darker values represent thin
sections, and lighter values will be thicker.  Provided is a baked thickness
map of the Monkey model. (The other models would have uniform thicknesses)."
        }

        ComboBox {
            id: thicknessChannelComboBox
            textRole: "text"
            valueRole: "value"
            implicitContentWidthPolicy: ComboBox.WidestText
            onActivated: targetMaterial.thicknessChannel = currentValue
            Component.onCompleted: currentIndex = indexOfValue(targetMaterial.thicknessChannel)
            model: [
                { value: PrincipledMaterial.R, text: "Red Channel"},
                { value: PrincipledMaterial.G, text: "Green Channel"},
                { value: PrincipledMaterial.B, text: "Blue Channel"},
                { value: PrincipledMaterial.A, text: "Alpha Channel"}
            ]
        }
        TextureSourceControl {
            defaultTexture: "maps/monkey_thickness.jpg"
            defaultClearColor: "black"
            onTargetTextureChanged: {
                targetMaterial.thicknessMap = targetTexture
            }
        }

        VerticalSectionSeparator {}

        MarkdownLabel {
            text: "## Attenuation
As light passes through a volume it will be subject to absorption and scattering.
To simulate this interaction, two properties are provided for determining this
attenuation.
### Attenuation Color
The Attenuation Color property refers to the color that white light turns into
due to the absorption when reaching the attenuation distance.
"
        }
        RowLayout {
            Label {
                text: "Red (" + targetMaterial.attenuationColor.r.toFixed(2) + ")"
                Layout.fillWidth: true
            }
            Slider {
                from: 0
                to: 1
                value: targetMaterial.attenuationColor.r
                onValueChanged: targetMaterial.attenuationColor.r = value
            }
        }
        RowLayout {
            Label {
                text: "Green  (" + targetMaterial.attenuationColor.g.toFixed(2) + ")"
                Layout.fillWidth: true
            }

            Slider {
                from: 0
                to: 1
                value: targetMaterial.attenuationColor.g
                onValueChanged: targetMaterial.attenuationColor.g = value
            }
        }
        RowLayout {
            Label {
                text: "Blue (" + targetMaterial.attenuationColor.b.toFixed(2) + ")"
                Layout.fillWidth: true
            }

            Slider {
                from: 0
                to: 1
                value: targetMaterial.attenuationColor.b
                onValueChanged: targetMaterial.attenuationColor.b = value
            }
        }
        MarkdownLabel {
            text: "### Attenuation Distance
Attenuation Distance defines material density, but does so by describing the
average distance light must travel through the medium before interacting with a
particle (absorption). In this case the distance is specified in world
coordinate space (scene space). This distance can be any positive floating point
value. This means the attenuation color will start to appear when the thickness
is greater than the attenuation distance, with the caveat that the Attenuation
Color assumes white light is passing through the model, so any other light will
create a blended result. For this demonstration the slider value is limited to
100, which should be the maximum thickness for all 3 models."
        }

        RowLayout {
            Label {
                text: "Attenuation Distance (" + targetMaterial.attenuationDistance.toFixed(2) + ")"
                Layout.fillWidth: true
            }
            Slider {
                from: 0
                to: 100
                value: targetMaterial.attenuationDistance
                onValueChanged:  {
                    if (value != targetMaterial.attenuationDistance)
                        targetMaterial.attenuationDistance = value
                }
            }
        }
    }
}
