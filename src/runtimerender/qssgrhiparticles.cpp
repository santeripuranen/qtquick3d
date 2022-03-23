/****************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Quick 3D.
**
** $QT_BEGIN_LICENSE:GPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 or (at your option) any later version
** approved by the KDE Free Qt Foundation. The licenses are as published by
** the Free Software Foundation and appearing in the file LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qssgrhiparticles_p.h"
#include "qssgrhicontext_p.h"

#include <QtQuick3DUtils/private/qssgutils_p.h>

#include <QtQuick3DRuntimeRender/private/qssgrenderer_p.h>
#include <QtQuick3DRuntimeRender/private/qssgrendercamera_p.h>

QT_BEGIN_NAMESPACE

static const QRhiShaderResourceBinding::StageFlags VISIBILITY_ALL =
        QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;

struct ParticleLightData
{
    QVector4D pointLightPos[4];
    float pointLightConstantAtt[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float pointLightLinearAtt[4] = {0.0f};
    float pointLightQuadAtt[4] = {0.0f};
    QVector4D pointLightColor[4];
    QVector4D spotLightPos[4];
    float spotLightConstantAtt[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float spotLightLinearAtt[4] = {0.0f};
    float spotLightQuadAtt[4] = {0.0f};
    QVector4D spotLightColor[4];
    QVector4D spotLightDir[4];
    float spotLightConeAngle[4] = {0.0f};
    float spotLightInnerConeAngle[4] = {0.0f};
};

void QSSGParticleRenderer::updateUniformsForParticles(QSSGRef<QSSGRhiShaderPipeline> &shaders,
                                                      QSSGRhiContext *rhiCtx,
                                                      char *ubufData,
                                                      QSSGParticlesRenderable &renderable,
                                                      QSSGRenderCamera &inCamera)
{
    const QMatrix4x4 clipSpaceCorrMatrix = rhiCtx->rhi()->clipSpaceCorrMatrix();

    QSSGRhiShaderPipeline::CommonUniformIndices &cui = shaders->commonUniformIndices;

    const QMatrix4x4 projection = clipSpaceCorrMatrix * inCamera.projection;
    shaders->setUniform(ubufData, "qt_projectionMatrix", projection.constData(), 16 * sizeof(float), &cui.projectionMatrixIdx);

    const QMatrix4x4 viewMatrix = inCamera.globalTransform.inverted();
    shaders->setUniform(ubufData, "qt_viewMatrix", viewMatrix.constData(), 16 * sizeof(float), &cui.viewMatrixIdx);

    const QMatrix4x4 &modelMatrix = renderable.globalTransform;
    shaders->setUniform(ubufData, "qt_modelMatrix", modelMatrix.constData(), 16 * sizeof(float), &cui.modelMatrixIdx);

    QVector2D oneOverSize = QVector2D(1.0f, 1.0f);
    auto &particleBuffer = renderable.particles.m_particleBuffer;
    const quint32 particlesPerSlice = particleBuffer.particlesPerSlice();
    oneOverSize = QVector2D(1.0f / particleBuffer.size().width(), 1.0f / particleBuffer.size().height());
    shaders->setUniform(ubufData, "qt_oneOverParticleImageSize", &oneOverSize, 2 * sizeof(float));
    shaders->setUniform(ubufData, "qt_countPerSlice", &particlesPerSlice, 1 * sizeof(quint32));

    // Global opacity of the particles node
    shaders->setUniform(ubufData, "qt_opacity", &renderable.opacity, 1 * sizeof(float));

    float blendImages = renderable.particles.m_blendImages ? 1.0f : 0.0f;
    float imageCount = float(renderable.particles.m_spriteImageCount);
    float ooImageCount = 1.0f / imageCount;

    QVector4D spriteConfig(imageCount, ooImageCount, 0.0f, blendImages);
    shaders->setUniform(ubufData, "qt_spriteConfig", &spriteConfig, 4 * sizeof(float));

    const float billboard = renderable.particles.m_billboard ? 1.0f : 0.0f;
    shaders->setUniform(ubufData, "qt_billboard", &billboard, 1 * sizeof(float));

    // Lights
    QVector3D theLightAmbientTotal;
    bool hasLights = !renderable.particles.m_lights.isEmpty();
    int pointLight = 0;
    int spotLight = 0;
    if (hasLights) {
        ParticleLightData lightData;
        auto &lights = renderable.lights;
        for (quint32 lightIdx = 0, lightEnd = lights.size();
             lightIdx < lightEnd && lightIdx < QSSG_MAX_NUM_LIGHTS; ++lightIdx) {
            QSSGRenderLight *theLight(lights[lightIdx].light);
            // Ignore lights which are not specified for the particle
            if (!renderable.particles.m_lights.contains(theLight))
                continue;
            const bool lightEnabled = lights[lightIdx].enabled;
            if (lightEnabled) {
                if (theLight->m_brightness > 0.0f) {
                    if (theLight->type == QSSGRenderLight::Type::DirectionalLight) {
                        theLightAmbientTotal += theLight->m_diffuseColor * theLight->m_brightness;
                    } else if (theLight->type == QSSGRenderLight::Type::PointLight && pointLight < 4) {
                        lightData.pointLightColor[pointLight] = QVector4D(theLight->m_diffuseColor * theLight->m_brightness, 1.0f);
                        lightData.pointLightPos[pointLight] = QVector4D(theLight->getGlobalPos(), 1.0f);
                        lightData.pointLightConstantAtt[pointLight] = aux::translateConstantAttenuation(theLight->m_constantFade);
                        lightData.pointLightLinearAtt[pointLight] = aux::translateLinearAttenuation(theLight->m_linearFade);
                        lightData.pointLightQuadAtt[pointLight] = aux::translateQuadraticAttenuation(theLight->m_quadraticFade);
                        pointLight++;
                    } else if (theLight->type == QSSGRenderLight::Type::SpotLight && spotLight < 4) {
                        lightData.spotLightColor[spotLight] = QVector4D(theLight->m_diffuseColor * theLight->m_brightness, 1.0f);
                        lightData.spotLightPos[spotLight] = QVector4D(theLight->getGlobalPos(), 1.0f);
                        lightData.spotLightDir[spotLight] = QVector4D(lights[lightIdx].direction, 0.0f);
                        lightData.spotLightConstantAtt[spotLight] = aux::translateConstantAttenuation(theLight->m_constantFade);
                        lightData.spotLightLinearAtt[spotLight] = aux::translateLinearAttenuation(theLight->m_linearFade);
                        lightData.spotLightQuadAtt[spotLight] = aux::translateQuadraticAttenuation(theLight->m_quadraticFade);
                        float coneAngle = theLight->m_coneAngle;
                        // Inner cone angle must always be < cone angle, to not have possible undefined behavior for shader smoothstep
                        float innerConeAngle = std::min(theLight->m_innerConeAngle, coneAngle - 0.01f);
                        lightData.spotLightConeAngle[spotLight] = qDegreesToRadians(coneAngle);
                        lightData.spotLightInnerConeAngle[spotLight] = qDegreesToRadians(innerConeAngle);
                        spotLight++;
                    }
                }
                theLightAmbientTotal += theLight->m_ambientColor;
            }
        }
        // Copy light data
        int lightOffset = shaders->offsetOfUniform("qt_pointLightPosition");
        if (lightOffset >= 0)
            memcpy(ubufData + lightOffset, &lightData, sizeof(ParticleLightData));
    }
    shaders->setUniform(ubufData, "qt_light_ambient_total", &theLightAmbientTotal, 3 * sizeof(float), &cui.light_ambient_totalIdx);
    int enablePointLights = pointLight > 0 ? 1 : 0;
    int enableSpotLights = spotLight > 0 ? 1 : 0;
    shaders->setUniform(ubufData, "qt_pointLights", &enablePointLights, sizeof(int));
    shaders->setUniform(ubufData, "qt_spotLights", &enableSpotLights, sizeof(int));
}

void QSSGParticleRenderer::updateUniformsForParticleModel(QSSGRef<QSSGRhiShaderPipeline> &shaderPipeline,
                                                          char *ubufData,
                                                          const QSSGRenderModel *model,
                                                          quint32 offset)
{
    auto &particleBuffer = *model->particleBuffer;
    const quint32 particlesPerSlice = particleBuffer.particlesPerSlice();
    const QVector2D oneOverSize = QVector2D(1.0f / particleBuffer.size().width(), 1.0f / particleBuffer.size().height());
    shaderPipeline->setUniform(ubufData, "qt_oneOverParticleImageSize", &oneOverSize, 2 * sizeof(float));
    shaderPipeline->setUniform(ubufData, "qt_countPerSlice", &particlesPerSlice, sizeof(quint32));
    const QMatrix4x4 &particleMatrix = model->particleMatrix;
    shaderPipeline->setUniform(ubufData, "qt_particleMatrix", &particleMatrix, 16 * sizeof(float));
    shaderPipeline->setUniform(ubufData, "qt_particleIndexOffset", &offset, sizeof(quint32));
}

static void fillTargetBlend(QRhiGraphicsPipeline::TargetBlend &targetBlend, QSSGRenderParticles::BlendMode mode)
{
    switch (mode) {
    case QSSGRenderParticles::BlendMode::Screen:
        targetBlend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        targetBlend.dstColor = QRhiGraphicsPipeline::One;
        targetBlend.srcAlpha = QRhiGraphicsPipeline::One;
        targetBlend.dstAlpha = QRhiGraphicsPipeline::One;
        break;
    case QSSGRenderParticles::BlendMode::Multiply:
        targetBlend.srcColor = QRhiGraphicsPipeline::DstColor;
        targetBlend.dstColor = QRhiGraphicsPipeline::Zero;
        targetBlend.srcAlpha = QRhiGraphicsPipeline::One;
        targetBlend.dstAlpha = QRhiGraphicsPipeline::One;
        break;
    default:
        // Source over as default
        targetBlend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        targetBlend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        targetBlend.srcAlpha = QRhiGraphicsPipeline::One;
        targetBlend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        break;

    }
}

static void sortParticles(QByteArray &result, QList<QSSGRhiSortData> &sortData,
                          const QSSGParticleBuffer &buffer, const QSSGRenderParticles &particles,
                          const QVector3D &cameraDirection, bool animatedParticles)
{
    const QMatrix4x4 &invModelMatrix = particles.globalTransform.inverted();
    QVector3D dir = invModelMatrix.map(cameraDirection);
    QVector3D n = dir.normalized();
    const auto particleCount = buffer.particleCount();
    sortData.resize(particleCount);
    sortData.fill({});

    // create sort data
    {
        const auto slices = buffer.sliceCount();
        const auto ss = buffer.sliceStride();
        const auto pps = buffer.particlesPerSlice();

        QSSGRhiSortData *dst = sortData.data();
        const char *source = buffer.pointer();
        const char *begin = source;
        int i = 0;
        if (animatedParticles) {
            for (int s = 0; s < slices; s++) {
                const QSSGParticleAnimated *sp = reinterpret_cast<const QSSGParticleAnimated *>(source);
                for (int p = 0; p < pps && i < particleCount; p++) {
                    *dst = { QVector3D::dotProduct(sp->position, n), int(reinterpret_cast<const char *>(sp) - begin)};
                    sp++;
                    dst++;
                    i++;
                }
                source += ss;
            }
        } else {
            for (int s = 0; s < slices; s++) {
                const QSSGParticleSimple *sp = reinterpret_cast<const QSSGParticleSimple *>(source);
                for (int p = 0; p < pps && i < particleCount; p++) {
                    *dst = { QVector3D::dotProduct(sp->position, n), int(reinterpret_cast<const char *>(sp) - begin)};
                    sp++;
                    dst++;
                    i++;
                }
                source += ss;
            }
        }
    }

    // sort
    result.resize(buffer.bufferSize());
    std::sort(sortData.begin(), sortData.end(), [](const QSSGRhiSortData &a, const QSSGRhiSortData &b){
        return a.d > b.d;
    });

    auto copyParticles = [&](QByteArray &dst, const QList<QSSGRhiSortData> &data, const QSSGParticleBuffer &buffer) {
        const auto slices = buffer.sliceCount();
        const auto ss = buffer.sliceStride();
        const auto pps = buffer.particlesPerSlice();
        const QSSGRhiSortData *sdata = data.data();
        char *dest = dst.data();
        const char *source = buffer.pointer();
        int i = 0;
        if (animatedParticles) {
            for (int s = 0; s < slices; s++) {
                QSSGParticleAnimated *dp = reinterpret_cast<QSSGParticleAnimated *>(dest);
                for (int p = 0; p < pps && i < particleCount; p++) {
                    *dp = *reinterpret_cast<const QSSGParticleAnimated *>(source + sdata->indexOrOffset);
                    dp++;
                    sdata++;
                    i++;
                }
                dest += ss;
            }
        } else {
            for (int s = 0; s < slices; s++) {
                QSSGParticleSimple *dp = reinterpret_cast<QSSGParticleSimple *>(dest);
                for (int p = 0; p < pps && i < particleCount; p++) {
                    *dp = *reinterpret_cast<const QSSGParticleSimple *>(source + sdata->indexOrOffset);
                    dp++;
                    sdata++;
                    i++;
                }
                dest += ss;
            }
        }
    };

    // write result
    copyParticles(result, sortData, buffer);
}

void QSSGParticleRenderer::rhiPrepareRenderable(QSSGRef<QSSGRhiShaderPipeline> &shaderPipeline,
                                                QSSGRhiContext *rhiCtx,
                                                QSSGRhiGraphicsPipelineState *ps,
                                                QSSGParticlesRenderable &renderable,
                                                QSSGLayerRenderData &inData,
                                                QRhiRenderPassDescriptor *renderPassDescriptor,
                                                int samples,
                                                QSSGRenderCamera *camera,
                                                int cubeFace,
                                                QSSGReflectionMapEntry *entry)
{
    const void *layerNode = &inData.layer;
    const void *node = &renderable.particles;

    QSSGRhiDrawCallData &dcd(cubeFace < 0 ? rhiCtx->drawCallData({ layerNode, node,
                                                    nullptr, 0, QSSGRhiDrawCallDataKey::Main })
                                          : rhiCtx->drawCallData({ layerNode, node,
                                                                   entry, cubeFace, QSSGRhiDrawCallDataKey::Reflection }));
    shaderPipeline->ensureUniformBuffer(&dcd.ubuf);

    char *ubufData = dcd.ubuf->beginFullDynamicBufferUpdateForCurrentFrame();
    if (!camera)
        updateUniformsForParticles(shaderPipeline, rhiCtx, ubufData, renderable, *inData.camera);
    else
        updateUniformsForParticles(shaderPipeline, rhiCtx, ubufData, renderable, *camera);
    dcd.ubuf->endFullDynamicBufferUpdateForCurrentFrame();

    QSSGRhiParticleData &particleData = rhiCtx->particleData(&renderable.particles);
    const QSSGParticleBuffer &particleBuffer = renderable.particles.m_particleBuffer;
    int particleCount = particleBuffer.particleCount();
    if (particleData.texture == nullptr || particleData.particleCount != particleCount) {
        QSize size(particleBuffer.size());
        if (!particleData.texture) {
            particleData.texture = rhiCtx->rhi()->newTexture(QRhiTexture::RGBA32F, size);
            particleData.texture->create();
        } else {
            particleData.texture->setPixelSize(size);
            particleData.texture->create();
        }
        particleData.particleCount = particleCount;
    }

    bool sortingChanged = particleData.sorting != renderable.particles.m_depthSorting;
    if (sortingChanged && !renderable.particles.m_depthSorting) {
        particleData.sortData.clear();
        particleData.sortedData.clear();
    }
    particleData.sorting = renderable.particles.m_depthSorting;

    QByteArray uploadData;

    if (renderable.particles.m_depthSorting) {
        bool animatedParticles = renderable.particles.m_featureLevel == QSSGRenderParticles::FeatureLevel::Animated;
        if (!camera)
            sortParticles(particleData.sortedData, particleData.sortData, particleBuffer, renderable.particles, *inData.cameraDirection, animatedParticles);
        else
            sortParticles(particleData.sortedData, particleData.sortData, particleBuffer, renderable.particles, camera->getScalingCorrectDirection(), animatedParticles);
        uploadData = particleData.sortedData;
    } else {
        uploadData = particleBuffer.data();
    }

    QRhiResourceUpdateBatch *rub = rhiCtx->rhi()->nextResourceUpdateBatch();
    QRhiTextureSubresourceUploadDescription upload;
    upload.setData(uploadData);
    QRhiTextureUploadDescription uploadDesc(QRhiTextureUploadEntry(0, 0, upload));
    rub->uploadTexture(particleData.texture, uploadDesc);
    rhiCtx->commandBuffer()->resourceUpdate(rub);

    ps->ia.topology = QRhiGraphicsPipeline::TriangleStrip;
    ps->ia.inputLayout = QRhiVertexInputLayout();
    ps->ia.inputs.clear();

    ps->samples = samples;
    ps->cullMode = QRhiGraphicsPipeline::None;
    if (renderable.renderableFlags.hasTransparency())
        fillTargetBlend(ps->targetBlend, renderable.particles.m_blendMode);
    else
        ps->targetBlend = QRhiGraphicsPipeline::TargetBlend();

    QSSGRhiShaderResourceBindingList bindings;
    bindings.addUniformBuffer(0, VISIBILITY_ALL, dcd.ubuf, 0, shaderPipeline->ub0Size());

    // Texture maps
    // we only have one image
    QSSGRenderableImage *renderableImage = renderable.firstImage;

    int samplerBinding = shaderPipeline->bindingForTexture("qt_sprite");
    if (samplerBinding >= 0) {
        QRhiTexture *texture = renderableImage ? renderableImage->m_texture.m_texture : nullptr;
        if (samplerBinding >= 0 && texture) {
            const bool mipmapped = texture->flags().testFlag(QRhiTexture::MipMapped);
            QRhiSampler *sampler = rhiCtx->sampler({ toRhi(renderableImage->m_imageNode.m_minFilterType),
                                                     toRhi(renderableImage->m_imageNode.m_magFilterType),
                                                     mipmapped ? toRhi(renderableImage->m_imageNode.m_mipFilterType) : QRhiSampler::None,
                                                     toRhi(renderableImage->m_imageNode.m_horizontalTilingMode),
                                                     toRhi(renderableImage->m_imageNode.m_verticalTilingMode),
                                                     QRhiSampler::Repeat
                                                   });
            bindings.addTexture(samplerBinding, QRhiShaderResourceBinding::FragmentStage, texture, sampler);
        } else {
            QRhiResourceUpdateBatch *rub = rhiCtx->rhi()->nextResourceUpdateBatch();
            QRhiTexture *texture = rhiCtx->dummyTexture({}, rub, QSize(4, 4), Qt::white);
            rhiCtx->commandBuffer()->resourceUpdate(rub);
            QRhiSampler *sampler = rhiCtx->sampler({ QRhiSampler::Nearest,
                                                     QRhiSampler::Nearest,
                                                     QRhiSampler::None,
                                                     QRhiSampler::ClampToEdge,
                                                     QRhiSampler::ClampToEdge,
                                                     QRhiSampler::Repeat
                                                   });
            bindings.addTexture(samplerBinding, QRhiShaderResourceBinding::FragmentStage, texture, sampler);
        }
    }

    samplerBinding = shaderPipeline->bindingForTexture("qt_particleTexture");
    if (samplerBinding >= 0) {
        QRhiTexture *texture = particleData.texture;
        if (samplerBinding >= 0 && texture) {
            QRhiSampler *sampler = rhiCtx->sampler({ QRhiSampler::Nearest,
                                                     QRhiSampler::Nearest,
                                                     QRhiSampler::None,
                                                     QRhiSampler::ClampToEdge,
                                                     QRhiSampler::ClampToEdge,
                                                     QRhiSampler::Repeat
                                                   });
            bindings.addTexture(samplerBinding, QRhiShaderResourceBinding::VertexStage, texture, sampler);
        }
    }

    samplerBinding = shaderPipeline->bindingForTexture("qt_colorTable");
    if (samplerBinding >= 0) {
        bool hasTexture = false;
        if (renderable.colorTable) {
            QRhiTexture *texture = renderable.colorTable->m_texture.m_texture;
            if (texture) {
                hasTexture = true;
                QRhiSampler *sampler = rhiCtx->sampler({ QRhiSampler::Nearest,
                                                         QRhiSampler::Nearest,
                                                         QRhiSampler::None,
                                                         QRhiSampler::ClampToEdge,
                                                         QRhiSampler::ClampToEdge,
                                                         QRhiSampler::Repeat
                                                       });
                bindings.addTexture(samplerBinding, QRhiShaderResourceBinding::FragmentStage, texture, sampler);
            }
        }

        if (!hasTexture) {
            QRhiResourceUpdateBatch *rub = rhiCtx->rhi()->nextResourceUpdateBatch();
            QRhiTexture *texture = rhiCtx->dummyTexture({}, rub, QSize(4, 4), Qt::white);
            rhiCtx->commandBuffer()->resourceUpdate(rub);
            QRhiSampler *sampler = rhiCtx->sampler({ QRhiSampler::Nearest,
                                                     QRhiSampler::Nearest,
                                                     QRhiSampler::None,
                                                     QRhiSampler::ClampToEdge,
                                                     QRhiSampler::ClampToEdge,
                                                     QRhiSampler::Repeat
                                                   });
            bindings.addTexture(samplerBinding, QRhiShaderResourceBinding::FragmentStage, texture, sampler);
        }
    }

    // TODO: This is identical to other renderables. Make into a function?
    QRhiShaderResourceBindings *&srb = dcd.srb;
    bool srbChanged = false;
    if (!srb || bindings != dcd.bindings) {
        srb = rhiCtx->srb(bindings);
        dcd.bindings = bindings;
        srbChanged = true;
    }

    if (cubeFace < 0)
        renderable.rhiRenderData.mainPass.srb = srb;
    else
        renderable.rhiRenderData.reflectionPass.srb[cubeFace] = srb;

    const QSSGGraphicsPipelineStateKey pipelineKey = QSSGGraphicsPipelineStateKey::create(*ps, renderPassDescriptor, srb);
    if (dcd.pipeline
            && !srbChanged
            && dcd.renderTargetDescriptionHash == pipelineKey.extra.renderTargetDescriptionHash
            && dcd.renderTargetDescription == pipelineKey.renderTargetDescription
            && dcd.ps == *ps)
    {
        if (cubeFace < 0)
            renderable.rhiRenderData.mainPass.pipeline = dcd.pipeline;
        else
            renderable.rhiRenderData.reflectionPass.pipeline = dcd.pipeline;
    } else {
        if (cubeFace < 0) {
            renderable.rhiRenderData.mainPass.pipeline = rhiCtx->pipeline(pipelineKey,
                                                                          renderPassDescriptor,
                                                                          srb);
            dcd.pipeline = renderable.rhiRenderData.mainPass.pipeline;
        } else {
            renderable.rhiRenderData.reflectionPass.pipeline = rhiCtx->pipeline(pipelineKey,
                                                                          renderPassDescriptor,
                                                                          srb);
            dcd.pipeline = renderable.rhiRenderData.reflectionPass.pipeline;
        }
        dcd.renderTargetDescriptionHash = pipelineKey.extra.renderTargetDescriptionHash;
        dcd.renderTargetDescription = pipelineKey.renderTargetDescription;
        dcd.ps = *ps;
    }
}

void QSSGParticleRenderer::prepareParticlesForModel(QSSGRef<QSSGRhiShaderPipeline> &shaderPipeline,
                                                    QSSGRhiContext *rhiCtx,
                                                    QSSGRhiShaderResourceBindingList &bindings,
                                                    const QSSGRenderModel *model)
{
    QSSGRhiParticleData &particleData = rhiCtx->particleData(model);
    const QSSGParticleBuffer &particleBuffer = *model->particleBuffer;
    int particleCount = particleBuffer.particleCount();
    bool update = particleBuffer.serial() != particleData.serial;
    if (particleData.texture == nullptr || particleData.particleCount != particleCount) {
        QSize size(particleBuffer.size());
        if (!particleData.texture) {
            particleData.texture = rhiCtx->rhi()->newTexture(QRhiTexture::RGBA32F, size);
            particleData.texture->create();
        } else {
            particleData.texture->setPixelSize(size);
            particleData.texture->create();
        }
        particleData.particleCount = particleCount;
        update = true;
    }

    if (update) {
        QRhiResourceUpdateBatch *rub = rhiCtx->rhi()->nextResourceUpdateBatch();
        QRhiTextureSubresourceUploadDescription upload;
        upload.setData(particleBuffer.data());
        QRhiTextureUploadDescription uploadDesc(QRhiTextureUploadEntry(0, 0, upload));
        rub->uploadTexture(particleData.texture, uploadDesc);
        rhiCtx->commandBuffer()->resourceUpdate(rub);
    }
    particleData.serial = particleBuffer.serial();
    int samplerBinding = shaderPipeline->bindingForTexture("qt_particleTexture");
    if (samplerBinding >= 0) {
        QRhiTexture *texture = particleData.texture;
        if (samplerBinding >= 0 && texture) {
            QRhiSampler *sampler = rhiCtx->sampler({ QRhiSampler::Nearest,
                                                     QRhiSampler::Nearest,
                                                     QRhiSampler::None,
                                                     QRhiSampler::ClampToEdge,
                                                     QRhiSampler::ClampToEdge,
                                                     QRhiSampler::Repeat
                                                   });
            bindings.addTexture(samplerBinding, QRhiShaderResourceBinding::VertexStage, texture, sampler);
        }
    }
}

void QSSGParticleRenderer::rhiRenderRenderable(QSSGRhiContext *rhiCtx,
                                               QSSGParticlesRenderable &renderable,
                                               QSSGLayerRenderData &inData,
                                               bool *needsSetViewport,
                                               int cubeFace,
                                               QSSGRhiGraphicsPipelineState *state)
{
    QRhiGraphicsPipeline *ps = renderable.rhiRenderData.mainPass.pipeline;
    QRhiShaderResourceBindings *srb = renderable.rhiRenderData.mainPass.srb;

    if (cubeFace >= 0) {
        ps = renderable.rhiRenderData.reflectionPass.pipeline;
        srb = renderable.rhiRenderData.reflectionPass.srb[cubeFace];
    }

    if (!ps || !srb)
        return;

    QRhiCommandBuffer *cb = rhiCtx->commandBuffer();
    // QRhi optimizes out unnecessary binding of the same pipline
    cb->setGraphicsPipeline(ps);
    cb->setVertexInput(0, 0, nullptr);
    cb->setShaderResources(srb);

    if (needsSetViewport && *needsSetViewport) {
        if (!state)
            cb->setViewport(rhiCtx->graphicsPipelineState(&inData)->viewport);
        else
            cb->setViewport(state->viewport);
        *needsSetViewport = false;
    }
    // draw triangle strip with 2 triangles N times
    cb->draw(4, renderable.particles.m_particleBuffer.particleCount());
    QSSGRHICTX_STAT(rhiCtx, draw(4, renderable.particles.m_particleBuffer.particleCount()));
}

QT_END_NAMESPACE
