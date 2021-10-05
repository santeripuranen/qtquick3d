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

#include "uniformmodel.h"

QT_BEGIN_NAMESPACE

UniformModel::UniformModel(QObject *parent)
    : QAbstractListModel(parent)
{

}

void UniformModel::setModelData(UniformTable *data)
{
    beginResetModel();
    m_uniformTable = data;
    endResetModel();
}

int UniformModel::rowCount(const QModelIndex &) const
{
    return int((m_uniformTable != nullptr) ? m_uniformTable->size() : 0);
}

QVariant UniformModel::data(const QModelIndex &index, int role) const
{
    if (!m_uniformTable || !index.isValid())
        return QVariant();

    if (index.row() >= m_uniformTable->size())
        return false;

    const auto &uniform = (*m_uniformTable)[index.row()];

    if (role == Type) {
        return QVariant::fromValue(uniform.type);
    } else if (role == Name) {
        return QVariant::fromValue(QString::fromLatin1(uniform.name));
    } else if (role == Value) {
        switch (uniform.type) {
        case CustomMaterial::Uniform::Type::Sampler:
            return QVariant::fromValue(uniform.imagePath);
        case UniformModel::UniformType::Bool:
            return QVariant::fromValue(uniform.b);
        case UniformModel::UniformType::Int:
            return QVariant::fromValue(uniform.i);
        case UniformModel::UniformType::Float:
            return QVariant::fromValue(uniform.f);
        case UniformModel::UniformType::Vec2:
            return QVariant::fromValue(uniform.vec2);
        case UniformModel::UniformType::Vec3:
            return QVariant::fromValue(uniform.vec3);
        case UniformModel::UniformType::Vec4:
            return QVariant::fromValue(uniform.vec4);
        case UniformModel::UniformType::Mat44:
            return QVariant::fromValue(uniform.m44);
        case UniformModel::UniformType::Last:
            break;
        }
    }

    return QVariant();
}

QHash<int, QByteArray> UniformModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[Type] = "type";
    roles[Name] = "name";
    roles[Value] = "value";
    return roles;
}

bool UniformModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || !m_uniformTable)
        return false;

    if (index.row() >= m_uniformTable->size())
        return false;

    auto &uniform = (*m_uniformTable)[index.row()];

    bool ok = true;

    if (role == Type) {
        const auto v = value.toInt(&ok);
        if (ok)
            uniform.type = static_cast<UniformType>(v);
    } else if (role == Name) {
        uniform.name = value.toString().toUtf8();
    } else if (role == Value) {
        switch (uniform.type) {
        case UniformModel::UniformType::Bool:
            uniform.b = value.toBool();
            break;
        case UniformModel::UniformType::Int:
            uniform.i = value.toInt();
            break;
        case UniformModel::UniformType::Float:
            uniform.f = value.toFloat();
            break;
        case UniformModel::UniformType::Vec2:
            uniform.vec2 = value.value<QVector2D>();
            break;
        case UniformModel::UniformType::Vec3:
            uniform.vec3 = value.value<QVector3D>();
            break;
        case UniformModel::UniformType::Vec4:
            uniform.vec4 = value.value<QVector4D>();
            break;
        case UniformModel::UniformType::Mat44:
            uniform.m44 = value.value<QMatrix4x4>();
            break;
        case UniformModel::UniformType::Sampler:
            uniform.imagePath = value.toUrl().path();
            break;
        case UniformModel::UniformType::Last:
            break;
        }
    }


    if (ok)
        emit dataChanged(index, index, {role});

    return ok;
}

bool UniformModel::insertRow(int rowIndex, int type, const QString &id)
{
    if (m_uniformTable == nullptr)
        return false;

    if (!validateUniformName(id))
        return false;


    beginInsertRows(QModelIndex(), rowIndex, rowIndex);

    CustomMaterial::Uniform uniform = { };
    uniform.type = CustomMaterial::Uniform::Type(type);
    uniform.name = id.toLocal8Bit();
    switch (uniform.type) {
    case CustomMaterial::Uniform::Type::Bool:
        uniform.b = false;
        break;
    case CustomMaterial::Uniform::Type::Int:
        uniform.i = 0;
        break;
    case CustomMaterial::Uniform::Type::Float:
        uniform.f = 0.0f;
        break;
    case CustomMaterial::Uniform::Type::Vec2:
        uniform.vec2 = QVector2D();
        break;
    case CustomMaterial::Uniform::Type::Vec3:
        uniform.vec3 = QVector3D();
        break;
    case CustomMaterial::Uniform::Type::Vec4:
        uniform.vec4 = QVector4D();
        break;
    case CustomMaterial::Uniform::Type::Mat44:
        uniform.m44 = QMatrix4x4();
        break;
    case CustomMaterial::Uniform::Type::Sampler:
        uniform.imagePath = QString();
        break;
    case CustomMaterial::Uniform::Type::Last:
        Q_UNREACHABLE();
        break;
    }

    m_uniformTable->insert(rowIndex, uniform);

    endInsertRows();

    emit dataChanged(QAbstractItemModel::createIndex(0, 0),
                     QAbstractItemModel::createIndex(rowIndex, 0));
    return true;
}

void UniformModel::removeRow(int rowIndex, int rows)
{
    if (m_uniformTable == nullptr)
        return;

    if (m_uniformTable->size() > rowIndex) {
        rows = qBound(1, rows, m_uniformTable->size());
        beginRemoveRows(QModelIndex(), rowIndex, rowIndex + rows - 1);
        m_uniformTable->remove(rowIndex, rows);
        endRemoveRows();
    }
}

bool UniformModel::validateUniformName(const QString &uniformName)
{
    if (!m_uniformTable)
        return false;

    // must be unique
    for (const auto &uniform : *m_uniformTable)
    {
        if (uniform.name == uniformName)
            return false;
    }

    // TODO: There are probably some other requirments

    return true;
}

QT_END_NAMESPACE


