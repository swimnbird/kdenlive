/*
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "fillerreview.hpp"

#include <algorithm>

namespace {
QStringList tokensOf(const QString &phrase)
{
    QStringList tokens;
    const QStringList parts = phrase.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString token = FillerReview::normalizeToken(part);
        if (!token.isEmpty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}
}

FillerLists FillerReview::defaults()
{
    FillerLists lists;
    lists.fillers = {QStringLiteral("um"), QStringLiteral("uh"), QStringLiteral("er"), QStringLiteral("ah"), QStringLiteral("hmm"), QStringLiteral("mm")};
    lists.cautionPhrases = {QStringLiteral("like"), QStringLiteral("you know"), QStringLiteral("i mean"), QStringLiteral("sort of"), QStringLiteral("kind of")};
    return lists;
}

QString FillerReview::normalizeToken(const QString &word)
{
    QString token = word.trimmed().toLower();
    while (!token.isEmpty() && !token.front().isLetterOrNumber()) {
        token.remove(0, 1);
    }
    while (!token.isEmpty() && !token.back().isLetterOrNumber()) {
        token.chop(1);
    }
    return token;
}

QVector<FillerCandidate> FillerReview::findCandidates(const TranscriptDocument &document, const FillerLists &lists)
{
    QVector<QStringList> fillerTokens;
    QVector<QStringList> cautionTokens;
    int longest = 1;
    for (const QString &filler : lists.fillers) {
        const QStringList tokens = tokensOf(filler);
        if (!tokens.isEmpty()) {
            fillerTokens.push_back(tokens);
            longest = std::max(longest, static_cast<int>(tokens.size()));
        }
    }
    for (const QString &phrase : lists.cautionPhrases) {
        const QStringList tokens = tokensOf(phrase);
        if (!tokens.isEmpty()) {
            cautionTokens.push_back(tokens);
            longest = std::max(longest, static_cast<int>(tokens.size()));
        }
    }

    QVector<FillerCandidate> found;
    const int count = document.words.size();
    QVector<bool> used(count, false);
    for (int i = 0; i < count; ++i) {
        if (used.at(i) || document.words.at(i).excluded) {
            continue;
        }
        const FillerKind kinds[2] = {FillerKind::Caution, FillerKind::Filler};
        const QVector<QStringList> *tables[2] = {&cautionTokens, &fillerTokens};
        bool matched = false;
        for (int tableIndex = 0; tableIndex < 2 && !matched; ++tableIndex) {
            for (const QStringList &phrase : *tables[tableIndex]) {
                if (i + phrase.size() > count) {
                    continue;
                }
                bool same = true;
                for (int n = 0; n < phrase.size(); ++n) {
                    if (used.at(i + n) || document.words.at(i + n).excluded ||
                        FillerReview::normalizeToken(document.words.at(i + n).display) != phrase.at(n)) {
                        same = false;
                        break;
                    }
                }
                if (!same) {
                    continue;
                }
                FillerCandidate candidate;
                candidate.kind = kinds[tableIndex];
                candidate.defaultApproved = candidate.kind == FillerKind::Filler;
                candidate.startSec = document.words.at(i).startSec;
                candidate.endSec = document.words.at(i + phrase.size() - 1).endSec;
                QStringList shown;
                for (int n = 0; n < phrase.size(); ++n) {
                    used[i + n] = true;
                    candidate.wordIds.push_back(document.words.at(i + n).id);
                    shown.push_back(document.words.at(i + n).display.trimmed());
                }
                candidate.text = shown.join(QLatin1Char(' '));
                found.push_back(candidate);
                matched = true;
                break;
            }
        }
        Q_UNUSED(longest)
    }
    return found;
}
