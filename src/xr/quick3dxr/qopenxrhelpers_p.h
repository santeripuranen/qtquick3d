// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#ifndef QOPENXRHELPERS_H
#define QOPENXRHELPERS_H

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


#include <openxr/openxr.h>
#include <QtQuick3DXr/qtquick3dxrglobal.h>
#include <QString>

QT_BEGIN_NAMESPACE

namespace OpenXRHelpers
{
QString getXrResultAsString(XrResult result, XrInstance instance);
bool checkXrResult(XrResult result, XrInstance instance);
}

QT_END_NAMESPACE

#endif // QOPENXRHELPERS_H
