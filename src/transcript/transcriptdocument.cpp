/*
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "transcriptdocument.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>

#include <cmath>

namespace {
bool nearlyLess(double a, double b)
{
    return a < b - 1e-6;
}

bool nearlyGreater(double a, double b)
{
    return a > b + 1e-6;
}
}

TranscriptDocument TranscriptDocument::fromWhisperStdout(const QString &stdoutText, double offsetSec)
{
    TranscriptDocument doc;
    int nextId = 1;
    const QStringList lines = stdoutText.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (!line.startsWith(QLatin1Char('['))) {
            continue;
        }
        const int open = line.indexOf(QLatin1Char('['));
        const int sep = line.indexOf(QLatin1Char('>'));
        const int close = line.indexOf(QLatin1Char(']'));
        if (open < 0 || sep < open || close < sep) {
            continue;
        }
        bool startOk = false;
        bool endOk = false;
        const double start = line.mid(open + 1, sep - open - 1).toDouble(&startOk);
        const double end = line.mid(sep + 1, close - sep - 1).toDouble(&endOk);
        const QString text = line.mid(close + 1);
        if (!startOk || !endOk || text.isEmpty() || !(end > start)) {
            continue;
        }
        TranscriptWord word;
        word.id = nextId++;
        word.spoken = text;
        word.display = text;
        word.startSec = start + offsetSec;
        word.endSec = end + offsetSec;
        doc.words.push_back(word);
    }
    return doc;
}

TranscriptDocument TranscriptDocument::fromJson(const QString &json)
{
    TranscriptDocument doc;
    const QJsonDocument parsed = QJsonDocument::fromJson(json.toUtf8());
    const QJsonArray array = parsed.object().value(QStringLiteral("words")).toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        TranscriptWord word;
        word.id = obj.value(QStringLiteral("id")).toInt();
        word.spoken = obj.value(QStringLiteral("spoken")).toString();
        word.display = obj.value(QStringLiteral("display")).toString(word.spoken);
        word.startSec = obj.value(QStringLiteral("start")).toDouble();
        word.endSec = obj.value(QStringLiteral("end")).toDouble();
        word.needsRetranscribe = obj.value(QStringLiteral("needsRetranscribe")).toBool();
        word.excluded = obj.value(QStringLiteral("excluded")).toBool();
        if (word.id < 0 || !(word.endSec > word.startSec)) {
            continue;
        }
        doc.words.push_back(word);
    }
    return doc;
}

QString TranscriptDocument::toJson() const
{
    QJsonArray array;
    for (const TranscriptWord &word : words) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), word.id);
        obj.insert(QStringLiteral("spoken"), word.spoken);
        obj.insert(QStringLiteral("display"), word.display);
        obj.insert(QStringLiteral("start"), word.startSec);
        obj.insert(QStringLiteral("end"), word.endSec);
        obj.insert(QStringLiteral("needsRetranscribe"), word.needsRetranscribe);
        obj.insert(QStringLiteral("excluded"), word.excluded);
        array.append(obj);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("words"), array);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

TranscriptWord *TranscriptDocument::wordById(int id)
{
    for (TranscriptWord &word : words) {
        if (word.id == id) {
            return &word;
        }
    }
    return nullptr;
}

const TranscriptWord *TranscriptDocument::wordById(int id) const
{
    for (const TranscriptWord &word : words) {
        if (word.id == id) {
            return &word;
        }
    }
    return nullptr;
}

bool TranscriptDocument::correctWord(int id, const QString &display)
{
    TranscriptWord *word = wordById(id);
    if (word == nullptr || display.trimmed().isEmpty()) {
        return false;
    }
    word->display = display;
    return true;
}

TranscriptRippleResult TranscriptDocument::applyRipple(double startSec, double endSec)
{
    TranscriptRippleResult result;
    const double duration = endSec - startSec;
    if (!(duration > 0.0) || !std::isfinite(duration)) {
        return result;
    }
    QVector<TranscriptWord> kept;
    kept.reserve(words.size());
    for (TranscriptWord word : words) {
        if (!nearlyGreater(word.endSec, startSec)) {
            kept.push_back(word);
            continue;
        }
        if (!nearlyLess(word.startSec, endSec)) {
            word.startSec -= duration;
            word.endSec -= duration;
            kept.push_back(word);
            continue;
        }
        const bool startsInside = !nearlyLess(word.startSec, startSec);
        const bool endsInside = !nearlyGreater(word.endSec, endSec);
        if (startsInside && endsInside) {
            result.removedIds.push_back(word.id);
            continue;
        }
        word.needsRetranscribe = true;
        result.flaggedIds.push_back(word.id);
        if (nearlyLess(word.startSec, startSec) && nearlyGreater(word.endSec, endSec)) {
            const double left = startSec - word.startSec;
            const double right = word.endSec - endSec;
            word.endSec = word.startSec + left + right;
        } else if (nearlyLess(word.startSec, startSec)) {
            word.endSec = startSec;
        } else {
            const double remain = word.endSec - endSec;
            word.startSec = startSec;
            word.endSec = startSec + remain;
        }
        if (word.endSec > word.startSec) {
            kept.push_back(word);
        } else {
            result.removedIds.push_back(word.id);
        }
    }
    words = kept;
    return result;
}

QVector<int> TranscriptDocument::search(const QString &query) const
{
    QVector<int> hits;
    const QString needle = query.trimmed();
    if (needle.isEmpty()) {
        return hits;
    }
    for (const TranscriptWord &word : words) {
        if (word.display.contains(needle, Qt::CaseInsensitive) || word.spoken.contains(needle, Qt::CaseInsensitive)) {
            hits.push_back(word.id);
        }
    }
    return hits;
}

QVector<int> TranscriptDocument::idsOverlapping(double startSec, double endSec) const
{
    QVector<int> hits;
    for (const TranscriptWord &word : words) {
        if (word.endSec > startSec && word.startSec < endSec) {
            hits.push_back(word.id);
        }
    }
    return hits;
}

int TranscriptDocument::wordAt(double seconds) const
{
    for (const TranscriptWord &word : words) {
        if (seconds >= word.startSec && seconds < word.endSec) {
            return word.id;
        }
    }
    return -1;
}

bool TranscriptDocument::rangeForIds(const QVector<int> &ids, double *startSec, double *endSec) const
{
    bool any = false;
    double start = 0.;
    double end = 0.;
    for (int id : ids) {
        const TranscriptWord *word = wordById(id);
        if (word == nullptr) {
            continue;
        }
        if (!any || word->startSec < start) {
            start = word->startSec;
        }
        if (!any || word->endSec > end) {
            end = word->endSec;
        }
        any = true;
    }
    if (any && startSec != nullptr) {
        *startSec = start;
    }
    if (any && endSec != nullptr) {
        *endSec = end;
    }
    return any;
}
