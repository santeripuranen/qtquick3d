/****************************************************************************
**
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

#ifndef Q_QUICK3D_INSTANCING_P_H
#define Q_QUICK3D_INSTANCING_P_H

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

#include <QtQuick3D/qquick3dinstancing.h>
#include <QtQuick3D/private/qquick3dobject_p.h>

#include <QtGui/qvector3d.h>

QT_BEGIN_NAMESPACE

class QQuick3DInstancingPrivate : public QQuick3DObjectPrivate
{
public:
    QQuick3DInstancingPrivate();
    int m_instanceCountOverride = -1;
    int m_instanceCount = 0;
    bool m_hasTransparency = false;
    bool m_instanceDataChanged = true;
    bool m_instanceCountOverrideChanged = false;
};

class Q_QUICK3D_EXPORT QQuick3DInstanceListEntry : public QQuick3DObject
{
    Q_OBJECT

    //QML_ADDED_IN_VERSION(6, 1)
    Q_PROPERTY(QVector3D position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(QVector3D scale READ scale WRITE setScale NOTIFY scaleChanged)
    Q_PROPERTY(QVector3D eulerRotation READ eulerRotation WRITE setEulerRotation NOTIFY eulerRotationChanged)
    Q_PROPERTY(QQuaternion rotation READ rotation WRITE setRotation NOTIFY rotationChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(QVector4D customData READ customData WRITE setCustomData NOTIFY customDataChanged)
    QML_NAMED_ELEMENT(InstanceListEntry)

public:
    explicit QQuick3DInstanceListEntry(QQuick3DObject *parent = nullptr);
    ~QQuick3DInstanceListEntry() override {}

    QVector3D position() const
    {
        return m_position;
    }
    QVector3D scale() const
    {
        return m_scale;
    }

    QVector3D eulerRotation() const
    {
        return m_eulerRotation;
    }

    QQuaternion rotation() const
    {
        return m_rotation;
    }

    QColor color() const
    {
        return m_color;
    }

    QVector4D customData() const
    {
        return m_customData;
    }

public Q_SLOTS:
    void setPosition(QVector3D position);
    void setScale(QVector3D scale);
    void setEulerRotation(QVector3D eulerRotation);
    void setRotation(QQuaternion rotation);
    void setColor(QColor color);
    void setCustomData(QVector4D customData);

Q_SIGNALS:
    void positionChanged();
    void scaleChanged();
    void eulerRotationChanged();
    void rotationChanged();
    void colorChanged();
    void customDataChanged();
    void changed();

protected:
    QSSGRenderGraphObject *updateSpatialNode(QSSGRenderGraphObject *) override
    {
        return nullptr;
    }

private:
    QVector3D m_position;
    QVector3D m_scale = {1, 1, 1};
    QVector3D m_eulerRotation;
    QQuaternion m_rotation;
    QColor m_color = Qt::white;
    QVector4D m_customData;
    bool m_useEulerRotation = true;
    friend class QQuick3DInstanceList;
};

class Q_QUICK3D_EXPORT QQuick3DInstanceList : public QQuick3DInstancing
{
    Q_OBJECT
    Q_PROPERTY(QQmlListProperty<QQuick3DInstanceListEntry> instances READ instances)
    QML_NAMED_ELEMENT(InstanceList)
    //QML_ADDED_IN_VERSION(6, 1)

public:
    explicit QQuick3DInstanceList(QQuick3DObject *parent = nullptr);
    ~QQuick3DInstanceList() override;

    QByteArray getInstanceBuffer(int *instanceCount) override;
    QQmlListProperty<QQuick3DInstanceListEntry> instances();

private Q_SLOTS:
    void handleInstanceChange();
    void onInstanceDestroyed(QObject *object);

private:
    void generateInstanceData();

    static void qmlAppendInstanceListEntry(QQmlListProperty<QQuick3DInstanceListEntry> *list, QQuick3DInstanceListEntry *material);
    static QQuick3DInstanceListEntry *qmlInstanceListEntryAt(QQmlListProperty<QQuick3DInstanceListEntry> *list, qsizetype index);
    static qsizetype qmlInstanceListEntriesCount(QQmlListProperty<QQuick3DInstanceListEntry> *list);
    static void qmlClearInstanceListEntries(QQmlListProperty<QQuick3DInstanceListEntry> *list);

    bool m_dirty = true;
    QByteArray m_instanceData;
    QList<QQuick3DInstanceListEntry *> m_instances;
};

QT_END_NAMESPACE

#endif