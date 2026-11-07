/*
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QPair>
#include <QString>
#include <QVector>
#include <QWidget>

struct StoryCleanupChoice {
    QString label;
    double startSec{0.};
    double endSec{0.};
    bool checked{false};
    QString note;
};

class StoryCleanup
{
public:
    /** Batch cleanup offers to duplicate the sequence first, and that box starts checked. */
    static bool duplicateSequenceByDefault() { return true; }
    /** Returns the checked ranges. duplicate is set from the checkbox, which defaults to on. */
    static QVector<QPair<double, double>> choose(QWidget *parent, const QString &title, const QString &help, QVector<StoryCleanupChoice> choices,
                                                 bool *duplicate);
};
