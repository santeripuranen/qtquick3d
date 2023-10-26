// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#version 440

layout(location = 0) in vec3 attr_pos;
layout(location = 1) in vec3 attr_color;

layout(std140, binding = 0) uniform buf {
    mat4 viewProjection;
}ubuf;

layout(location = 0) out vec3 var_color;

out gl_PerVertex { vec4 gl_Position; float gl_PointSize;};

void main()
{
    var_color = attr_color;
    gl_Position = ubuf.viewProjection * vec4(attr_pos, 1.0);
    gl_PointSize = 4.0;
}
