/****************************************************************************
**
** Copyright (C) 2008-2012 NVIDIA Corporation.
** Copyright (C) 2019 The Qt Company Ltd.
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

#ifndef QSSG_RENDER_SHADER_CACHE_H
#define QSSG_RENDER_SHADER_CACHE_H

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

#include <QtQuick3DRuntimeRender/private/qtquick3druntimerenderglobal_p.h>
#include <QtQuick3DUtils/private/qssgdataref_p.h>
#include <QtQuick3DUtils/private/qqsbcollection_p.h>

#include <QtQuick3DRuntimeRender/private/qssgrhicontext_p.h>

#include <QtCore/QString>
#include <QtCore/qcryptographichash.h>
#include <QtCore/QSharedPointer>
#include <QtCore/QVector>

QT_BEGIN_NAMESPACE

class QSSGRhiShaderPipeline;
class QShaderBaker;


struct Q_QUICK3DRUNTIMERENDER_EXPORT QSSGShaderFeatures
{

// There are a number of macros used to turn on or off various features.
// This allows those features to be propagated into the shader cache's caching mechanism.
// They will be translated into #define name value where value is 1 or zero depending on
// if the feature is enabled or not.
//
// In snippets that use this feature the code would look something like this:

/*
#ifndef QSSG_ENABLE_<FEATURE>
#define QSSG_ENABLE_<FEATURE> 0
#endif

void func()
{
    ...
#if QSSG_ENABLE_<FEATURE>
     ...
#endif
     ...
}
*/

// NOTE: The order of these will affect generated keys, so re-ordering these
// will break already baked shaders (e.g. shadergen).
using FlagType = quint32;
enum class Feature : FlagType
{
    LightProbe = (1 << 8),
    IblOrientation = (1 << 9) + 1,
    Ssm = (1 << 10) + 2,
    Ssao = (1 << 11) + 3,
    DepthPass = (1 << 12) + 4,
    OrthoShadowPass = (1 << 13) + 5,
    CubeShadowPass = (1 << 14) + 6,
    LinearTonemapping = (1 << 15) + 7,
    AcesTonemapping = (1 << 16) + 8,
    HejlDawsonTonemapping = (1 << 17) + 9,
    FilmicTonemapping = (1 << 18) + 10,
    RGBELightProbe = (1 << 19) + 11,
    OpaqueDepthPrePass = (1 << 20) + 12,
    ReflectionProbe = (1 << 21) + 13,
    ReduceMaxNumLights = (1 << 22) + 14,

    LastFeature
};

static constexpr FlagType IndexMask = 0xff;
static constexpr quint32 Count = (static_cast<FlagType>(Feature::LastFeature) & IndexMask);

static const char *asDefineString(QSSGShaderFeatures::Feature feature);
static Feature fromIndex(quint32 idx);

constexpr bool isNull() const { return flags == 0; }
constexpr bool isSet(Feature feature) const { return (static_cast<FlagType>(feature) & flags); }
void set(Feature feature, bool val);

FlagType flags = 0;

inline friend bool operator==(QSSGShaderFeatures a, QSSGShaderFeatures b) { return a.flags == b.flags; }

};

Q_QUICK3DRUNTIMERENDER_EXPORT size_t qHash(QSSGShaderFeatures features) noexcept;

struct QSSGShaderCacheKey
{
    QByteArray m_key;
    QSSGShaderFeatures m_features;
    size_t m_hashCode = 0;

    explicit QSSGShaderCacheKey(const QByteArray &key = QByteArray()) : m_key(key), m_hashCode(0) {}

    QSSGShaderCacheKey(const QSSGShaderCacheKey &other) = default;
    QSSGShaderCacheKey &operator=(const QSSGShaderCacheKey &other) = default;

    static inline size_t generateHashCode(const QByteArray &key, QSSGShaderFeatures features)
    {
        return qHash(key) ^ qHash(features);
    }

    static QByteArray hashString(const QByteArray &key, QSSGShaderFeatures features)
    {
        return  QCryptographicHash::hash(QByteArray::number(generateHashCode(key, features)), QCryptographicHash::Algorithm::Sha1).toHex();
    }

    void updateHashCode()
    {
        m_hashCode = generateHashCode(m_key, m_features);
    }

    bool operator==(const QSSGShaderCacheKey &inOther) const
    {
        return m_key == inOther.m_key && m_features == inOther.m_features;
    }
};

class Q_QUICK3DRUNTIMERENDER_EXPORT QSSGShaderCache
{
public:
    enum class ShaderType
    {
        Vertex = 0,
        Fragment = 1
    };

    QAtomicInt ref;

    using InitBakerFunc = void (*)(QShaderBaker *baker, QRhi::Implementation target);
private:
    typedef QHash<QSSGShaderCacheKey, QSSGRef<QSSGRhiShaderPipeline>> TRhiShaderMap;
    QSSGRef<QSSGRhiContext> m_rhiContext;
    TRhiShaderMap m_rhiShaders;
    QString m_cacheFilePath;
    QByteArray m_vertexCode;
    QByteArray m_fragmentCode;
    QByteArray m_insertStr;
    QString m_flagString;
    QString m_contextTypeString;
    QSSGShaderCacheKey m_tempKey;
    const InitBakerFunc m_initBaker;

    void addShaderPreprocessor(QByteArray &str,
                               const QByteArray &inKey,
                               ShaderType shaderType,
                               const QSSGShaderFeatures &inFeatures);

public:
    QSSGShaderCache(const QSSGRef<QSSGRhiContext> &ctx,
                    const InitBakerFunc initBakeFn = nullptr);
    ~QSSGShaderCache();

    QSSGRef<QSSGRhiShaderPipeline> getRhiShaderPipeline(const QByteArray &inKey,
                                                        const QSSGShaderFeatures &inFeatures);

    QSSGRef<QSSGRhiShaderPipeline> compileForRhi(const QByteArray &inKey,
                                               const QByteArray &inVert,
                                               const QByteArray &inFrag,
                                               const QSSGShaderFeatures &inFeatures,
                                               QSSGRhiShaderPipeline::StageFlags stageFlags);

    QSSGRef<QSSGRhiShaderPipeline> loadGeneratedShader(const QByteArray &inKey, QQsbCollection::Entry entry);
    QSSGRef<QSSGRhiShaderPipeline> loadBuiltinForRhi(const QByteArray &inKey);

    static QByteArray resourceFolder();
    static QByteArray shaderCollectionFile();
};

namespace QtQuick3DEditorHelpers {
namespace ShaderBaker
{
    enum class Status : quint8
    {
        Success,
        Error
    };
    using StatusCallback = void(*)(const QByteArray &descKey, Status status, const QString &err, QShader::Stage stage);
    Q_QUICK3DRUNTIMERENDER_EXPORT void setStatusCallback(StatusCallback cb);
}
}

QT_END_NAMESPACE

#endif
