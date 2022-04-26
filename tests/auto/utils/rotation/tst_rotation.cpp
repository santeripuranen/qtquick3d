/****************************************************************************
**
** Copyright (C) 2022 The Qt Company Ltd.
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

#include <QtTest>

#include <QtQuick3DUtils/private/qssgutils_p.h>

class tst_RotationDataClass : public QObject
{
    Q_OBJECT

public:
    tst_RotationDataClass() = default;
    ~tst_RotationDataClass() = default;

    using Dirty = RotationData::Dirty;
    static bool isDirty(const RotationData &rd) { return (rd.m_dirty != Dirty::None); };

private slots:
    void test_initialState();
    void test_construct();
    void test_eulerAssign();
    void test_quatAssign();
    void test_aba();
    void test_compare();
    void test_compare2();
};

void tst_RotationDataClass::test_initialState()
{

    // Initial state
    RotationData rotation;
    QCOMPARE(isDirty(rotation), false);
    QVERIFY(qFuzzyCompare(QVector3D(rotation), QVector3D()));
    QVERIFY(qFuzzyCompare(QQuaternion(rotation), QQuaternion()));
}

void tst_RotationDataClass::test_construct()
{
    QVector3D eulerRot = QVector3D(1.0f, 0.0f, 0.0f);
    QQuaternion quatRot = QQuaternion::fromEulerAngles(eulerRot);

    RotationData a(eulerRot);
    QCOMPARE(isDirty(a), true);

    RotationData b(quatRot);
    QCOMPARE(isDirty(b), true);

    QCOMPARE(a, b);
    // NOTE: Comparison is done based on the stored quaternion
    QCOMPARE(isDirty(a), false); // Dirty is cleared as 'a' was set using euler angles
    QCOMPARE(isDirty(b), true); // Still dirty, as the Euler angles where never compared and therefore not calculated
    QCOMPARE(b.m_dirty, Dirty::Euler);
    QVERIFY(qFuzzyCompare(QVector3D(b), eulerRot));
    QCOMPARE(isDirty(b), false); // Both internal values queried, so neither should be dirty


}

void tst_RotationDataClass::test_eulerAssign()
{
    RotationData rotation;
    QCOMPARE(isDirty(rotation), false);
    QVERIFY(qFuzzyCompare(QVector3D(rotation), QVector3D()));
    QVERIFY(qFuzzyCompare(QQuaternion(rotation), QQuaternion()));

    QVector3D eulerRot = QVector3D(1.0f, 0.0f, 0.0f);
    QQuaternion quatRot = QQuaternion::fromEulerAngles(eulerRot);

    rotation = eulerRot;
    QVERIFY(qFuzzyCompare(rotation.m_eulerRot, eulerRot));
    QCOMPARE(isDirty(rotation), true);

    {
        QCOMPARE(rotation.m_dirty, RotationData::Dirty::Quaternion);
        QQuaternion retQuatRot(rotation);
        QCOMPARE(isDirty(rotation), false);
        QVERIFY(qFuzzyCompare(retQuatRot, quatRot));
        QVERIFY(qFuzzyCompare(rotation.m_quatRot, quatRot));
    }
}

void tst_RotationDataClass::test_quatAssign()
{
    RotationData rotation;
    QCOMPARE(isDirty(rotation), false);
    QVERIFY(qFuzzyCompare(QVector3D(rotation), QVector3D()));
    QVERIFY(qFuzzyCompare(QQuaternion(rotation), QQuaternion()));

    QVector3D eulerRot = QVector3D(1.0f, 0.0f, 0.0f);
    QQuaternion quatRot = QQuaternion::fromEulerAngles(eulerRot);

    rotation = quatRot;
    QVERIFY(qFuzzyCompare(rotation.m_quatRot, quatRot));
    QCOMPARE(isDirty(rotation), true);

    {
        QCOMPARE(rotation.m_dirty, RotationData::Dirty::Euler);
        QVector3D retEulerRot(rotation);
        QCOMPARE(isDirty(rotation), false);
        QVERIFY(qFuzzyCompare(retEulerRot, eulerRot));
        QVERIFY(qFuzzyCompare(rotation.m_eulerRot, eulerRot));
    }
}

void tst_RotationDataClass::test_aba()
{
    // Dirty state should be mutually exclusive and the value
    // updated appropriately each time
    // (i.e., not skipping a valid write by testing agains an old value).
    RotationData rotation; // Init { 0, 0, 0}
    rotation = QVector3D(1.0f, 0.0f, 0.0f);
    // Quat dirty
    rotation = QQuaternion::fromEulerAngles(QVector3D(0.0f, 0.0f, 0.0f));
    // Euler dirty
    QCOMPARE(rotation, RotationData{});
}

void tst_RotationDataClass::test_compare()
{
    {
        RotationData a;
        RotationData b;
        QVERIFY(a == b);
        QVERIFY(b == a);

        a = RotationData(QVector3D(1.0f, 1.0f, 1.0f));
        QVERIFY(a != b);
        QVERIFY(b != a);

        a = RotationData(QQuaternion());
        QVERIFY(a == b);
        QVERIFY(b == a);
    }

    {
        RotationData a;
        QVector3D b;
        QVERIFY(a == b);
        QVERIFY(b == a);

        a = QVector3D(1.0f, 1.0f, 1.0f);
        QVERIFY(a != b);
        QVERIFY(b != a);

        a = QQuaternion();
        QVERIFY(a == b);
        QVERIFY(b == a);
    }

    {
        RotationData a;
        QQuaternion b;
        QVERIFY(a == b);
        QVERIFY(b == a);

        a = QVector3D(1.0f, 1.0f, 1.0f);
        QVERIFY(a != b);
        QVERIFY(b != a);

        a = QQuaternion();
        QVERIFY(a == b);
        QVERIFY(b == a);
    }
}

void tst_RotationDataClass::test_compare2()
{
    RotationData a;
    RotationData b;
    QVERIFY(a == b);
    QVERIFY(b == a);

    {
        const auto v = QVector3D();
        const auto qa = QQuaternion::fromEulerAngles(v);

        // Quat stored as normalized value ?
        QQuaternion qaNormalized = qa.normalized();
        a = qa;
        QVERIFY(a == qaNormalized);
        QVERIFY(a == v);

        b = -a;
        QVERIFY(a == b);
        QVERIFY(b == a);
    }

    {
        const auto v = QVector3D(0.0f, 0.0f, 45.0f);
        const auto qa = QQuaternion::fromEulerAngles(v);

        // Quat stored as normalized value ?
        QQuaternion qaNormalized = qa.normalized();
        {
            // Assumption 1: QQuaternion can be non-normalized but the RotationData class always
            // contains the normalized quaternion.
            {
                const auto qaNonNormalized = qa * 30.0f;
                QVERIFY(!qFuzzyCompare(qaNonNormalized, qaNormalized));
                {
                    RotationData rd(qaNonNormalized);
                    QVERIFY(rd == qaNormalized);
                }

                {
                    RotationData rd;
                    rd = qaNonNormalized;
                    QVERIFY(rd == qaNormalized);
                }
            }

            // Assumption 2: compare or fuzzy compare of QQuaternion is strictly a component by component compare
            auto qb = -qa;
            QVERIFY(!qFuzzyCompare(qb, qa));
        }
        a = qa;
        QVERIFY(a == qaNormalized);
        QVERIFY(a == v);

        b = -a;
        QVERIFY(a == b);
        QVERIFY(b == a);
    }
}

QTEST_APPLESS_MAIN(tst_RotationDataClass)

#include "tst_rotation.moc"
