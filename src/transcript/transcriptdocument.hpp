/*
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

/** Word-level transcript. Corrections change display text only.
 *  Spoken text and media times stay put until an explicit ripple edit.
 */
struct TranscriptWord {
    int id{-1};
    QString spoken;
    QString display;
    double startSec{0.};
    double endSec{0.};
    bool needsRetranscribe{false};
    bool excluded{false};
};

struct TranscriptRippleResult {
    QVector<int> removedIds;
    QVector<int> flaggedIds;
};

class TranscriptDocument
{
public:
    QVector<TranscriptWord> words;

    static TranscriptDocument fromWhisperStdout(const QString &stdoutText, double offsetSec = 0.);
    static TranscriptDocument fromJson(const QString &json);
    QString toJson() const;

    TranscriptWord *wordById(int id);
    const TranscriptWord *wordById(int id) const;

    /** Change the visible word. Returns false if the id is missing. Does not move times or edit spoken text. */
    bool correctWord(int id, const QString &display);

    /** Drop words fully inside [startSec, endSec), flag partial overlaps, and shift later words earlier. */
    TranscriptRippleResult applyRipple(double startSec, double endSec);

    QVector<int> search(const QString &query) const;
    QVector<int> idsOverlapping(double startSec, double endSec) const;
    int wordAt(double seconds) const;
    bool rangeForIds(const QVector<int> &ids, double *startSec, double *endSec) const;
};
