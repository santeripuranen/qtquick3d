// Copyright (C) 2008-2012 NVIDIA Corporation.
// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#ifndef QSSG_RENDER_DEFAULT_MATERIAL_SHADER_GENERATOR_H
#define QSSG_RENDER_DEFAULT_MATERIAL_SHADER_GENERATOR_H

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

#include <QtQuick3DRuntimeRender/private/qssgrendermaterialshadergenerator_p.h>
#include <QtQuick3DRuntimeRender/private/qssgrendershadercodegenerator_p.h>
#include <QtQuick3DRuntimeRender/private/qssgrendershadowmap_p.h>
#include <QtQuick3DRuntimeRender/private/qssgrenderlight_p.h>
#include <QtQuick3DRuntimeRender/private/qssgrenderableimage_p.h>
#include <QtQuick3DRuntimeRender/private/qssgrendershaderkeys_p.h>

QT_BEGIN_NAMESPACE

struct QSSGRenderableImage;
struct QSSGShaderDefaultMaterialKeyProperties;
struct QSSGShaderDefaultMaterialKey;
struct QSSGLayerGlobalRenderProperties;
struct QSSGMaterialVertexPipeline;

struct Q_QUICK3DRUNTIMERENDER_EXPORT QSSGMaterialShaderGenerator
{
    struct LightVariableNames
    {
        QByteArray lightColor;
        QByteArray lightSpecularColor;
        QByteArray lightAttenuation;
        QByteArray lightConstantAttenuation;
        QByteArray lightLinearAttenuation;
        QByteArray lightQuadraticAttenuation;
        QByteArray normalizedDirection;
        QByteArray lightDirection;
        QByteArray lightPos;
        QByteArray lightConeAngle;
        QByteArray lightInnerConeAngle;
        QByteArray relativeDistance;
        QByteArray relativeDirection;
        QByteArray spotAngle;
    };

    struct ShadowVariableNames
    {
        QByteArray shadowMapStem;
        QByteArray shadowCubeStem;
        QByteArray shadowMatrixStem;
        QByteArray shadowCoordStem;
        QByteArray shadowControlStem;
    };

    ~QSSGMaterialShaderGenerator() = default;

    static const char* getSamplerName(QSSGRenderableImage::Type type);

    static QSSGRef<QSSGRhiShaderPipeline> generateMaterialRhiShader(const QByteArray &inShaderKeyPrefix,
                                                                    QSSGMaterialVertexPipeline &vertexGenerator,
                                                                    const QSSGShaderDefaultMaterialKey &key,
                                                                    QSSGShaderDefaultMaterialKeyProperties &inProperties,
                                                                    const QSSGShaderFeatures &inFeatureSet,
                                                                    const QSSGRenderGraphObject &inMaterial,
                                                                    const QSSGShaderLightList &inLights,
                                                                    QSSGRenderableImage *inFirstImage, const QSSGRef<QSSGShaderLibraryManager> &shaderLibraryManager,
                                                                    const QSSGRef<QSSGShaderCache> &theCache);

    static void setRhiMaterialProperties(const QSSGRenderContextInterface &,
                                         QSSGRef<QSSGRhiShaderPipeline> &shaders,
                                         char *ubufData,
                                         QSSGRhiGraphicsPipelineState *inPipelineState,
                                         const QSSGRenderGraphObject &inMaterial,
                                         const QSSGShaderDefaultMaterialKey &inKey,
                                         QSSGShaderDefaultMaterialKeyProperties &inProperties,
                                         QSSGRenderCamera &inCamera,
                                         const QMatrix4x4 &inModelViewProjection,
                                         const QMatrix3x3 &inNormalMatrix,
                                         const QMatrix4x4 &inGlobalTransform,
                                         const QMatrix4x4 &clipSpaceCorrMatrix,
                                         const QMatrix4x4 &localInstanceTransform,
                                         const QMatrix4x4 &globalInstanceTransform,
                                         const QSSGDataView<float> &inMorphWeights,
                                         QSSGRenderableImage *inFirstImage,
                                         float inOpacity,
                                         const QSSGLayerGlobalRenderProperties &inRenderProperties,
                                         const QSSGShaderLightList &inLights,
                                         const QSSGShaderReflectionProbe &reflectionProbe,
                                         bool receivesShadows,
                                         bool receivesReflections,
                                         const QVector2D *shadowDepthAdjust,
                                         QRhiTexture *lightmapTexture);

    static const char *directionalLightProcessorArgumentList();
    static const char *pointLightProcessorArgumentList();
    static const char *spotLightProcessorArgumentList();
    static const char *ambientLightProcessorArgumentList();
    static const char *specularLightProcessorArgumentList();
    static const char *shadedFragmentMainArgumentList();
    static const char *postProcessorArgumentList();
    static const char *iblProbeProcessorArgumentList();
    static const char *vertexMainArgumentList();
    static const char *vertexInstancedMainArgumentList();

private:
    QSSGMaterialShaderGenerator() = delete;
    Q_DISABLE_COPY(QSSGMaterialShaderGenerator)
};

namespace QtQuick3DEditorHelpers {
namespace CustomMaterial {
// NOTE: Returns a copy of the actual list, cache as needed!
[[nodiscard]] Q_QUICK3DRUNTIMERENDER_EXPORT QList<QByteArrayView> reservedArgumentNames();
}
}

QT_END_NAMESPACE
#endif
