/*
 *  SPDX-FileCopyrightText: 2022 Dmitry Kazakov <dimula73@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef KISMIRROROPTION_H
#define KISMIRROROPTION_H

#include <KisCurveOption2.h>

struct KisMirrorOptionData;
struct MirrorProperties;

class PAINTOP_EXPORT KisMirrorOption : public KisCurveOption2
{
public:
    KisMirrorOption(const KisPropertiesConfiguration *setting);

    MirrorProperties apply(const KisPaintInformation& info) const;

private:
    KisMirrorOption(const KisMirrorOptionData &data);
private:
    bool m_enableHorizontalMirror;
    bool m_enableVerticalMirror;
};

#endif // KISMIRROROPTION_H
