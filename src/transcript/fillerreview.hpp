/*
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "transcriptdocument.hpp"

#include <QStringList>
#include <QVector>

enum class FillerKind { None, Filler, Caution };

struct FillerCandidate {
    QVector<int> wordIds;
    QString text;
    FillerKind kind{FillerKind::None};
    double startSec{0.};
    double endSec{0.};
    bool excluded{false};
    /** Caution phrases are not approved until a person checks them. */
    bool defaultApproved{false};
};

struct FillerLists {
    QStringList fillers;
    QStringList cautionPhrases;
};

class FillerReview
{
public:
    static FillerLists defaults();
    static QString normalizeToken(const QString &word);
    static QVector<FillerCandidate> findCandidates(const TranscriptDocument &document, const FillerLists &lists);
};
