// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#ifndef QQUICK3DRENDEREXTENSIONS_P_H
#define QQUICK3DRENDEREXTENSIONS_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <QtCore/qobject.h>
#include <QtQuick3D/qquick3dobject.h>
#include <QtQuick3DRuntimeRender/private/qssgrenderextensions_p.h>

QT_BEGIN_NAMESPACE

class QSSGRenderer;
class QSSGLayerRenderData;

class Q_QUICK3D_EXPORT QQuick3DRenderExtension : public QQuick3DObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(RenderExtension)
    QML_UNCREATABLE("RenderExtension is an abstract type")
    QML_ADDED_IN_VERSION(6, 6)
public:
    explicit QQuick3DRenderExtension(QQuick3DObject *parent = nullptr);
    virtual ~QQuick3DRenderExtension();
};

QT_END_NAMESPACE

#endif // QQUICK3DRENDEREXTENSIONS_P_H
