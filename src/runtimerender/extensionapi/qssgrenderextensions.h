// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#ifndef QSSGRENDEREXTENSIIONS_P_H
#define QSSGRENDEREXTENSIIONS_P_H

//
//  W A R N I N G
//  -------------
//
// This file is part of the QtQuick3D API, with limited compatibility guarantees.
// Usage of this API may make your code source and binary incompatible with
// future versions of Qt.
//

#include <QtQuick3DRuntimeRender/qtquick3druntimerenderexports.h>
#include <ssg/qssgrenderbasetypes.h>
#include <QtQuick3DRuntimeRender/private/qssgrenderableobjects_p.h>
#include <QtCore/qobject.h>

QT_BEGIN_NAMESPACE

class QSSGRenderer;
class QSSGLayerRenderData;

struct QSSGRhiRenderableTexture;

class Q_QUICK3DRUNTIMERENDER_EXPORT QSSGFrameData
{
public:
    enum class RenderResult : quint32
    {
        AoTexture,
        DepthTexture,
        ScreenTexture
    };

    using RenderResultT = std::underlying_type_t<RenderResult>;

    const QSSGRhiRenderableTexture *getRenderResult(RenderResult id) const;

    [[nodiscard]] QSSGRhiGraphicsPipelineState getPipelineState() const;

    [[nodiscard]] QSSGNodeId activeCamera() const;

    [[nodiscard]] QSSGRenderer *renderer() const { return m_renderer; }

private:
    friend class QSSGLayerRenderData;
    friend class QSSGRenderHelpers;

    void clear();

    [[nodiscard]] QSSGLayerRenderData *getCurrent() const;

    QSSGFrameData() = default;
    explicit QSSGFrameData(QSSGRenderer *renderer);
    QSSGRenderer *m_renderer = nullptr;
};

class Q_QUICK3DRUNTIMERENDER_EXPORT QSSGRenderExtension : public QSSGRenderGraphObject
{
public:
    enum class Type
    {
        Standalone,
        Main
    };

    enum class RenderMode
    {
        Underlay,
        Overlay
    };

    QSSGRenderExtension();
    virtual ~QSSGRenderExtension();

    virtual bool prepareData(QSSGFrameData &data) = 0;
    virtual void prepareRender(QSSGFrameData &data) = 0;
    virtual void render(QSSGFrameData &data) = 0;

    virtual void resetForFrame() = 0;

    virtual Type type() const = 0;
    virtual RenderMode mode() const = 0;
};

QT_END_NAMESPACE

#endif // QSSGRENDEREXTENSIIONS_P_H