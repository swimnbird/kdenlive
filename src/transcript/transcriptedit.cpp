/*
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "transcriptedit.hpp"

#include <QKeyEvent>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>

#include <algorithm>

namespace {
constexpr int SpokenRole = QTextFormat::UserProperty + 20;
constexpr int IdRole = QTextFormat::UserProperty + 21;
constexpr int RetranscribeRole = QTextFormat::UserProperty + 22;

struct Run {
    int start{-1};
    int end{-1};
    int id{-1};
    QString href;
    QString spoken;
    QString display;
    bool needsRetranscribe{false};
};

bool readRunAt(QTextDocument *document, int position, Run *run)
{
    if (document == nullptr || position < 0 || position >= document->characterCount()) {
        return false;
    }
    QTextCursor cursor(document);
    const int probe = std::min(position, document->characterCount() - 1);
    cursor.setPosition(probe);
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    const QTextCharFormat format = cursor.charFormat();
    if (!format.isAnchor() || format.anchorHref().isEmpty()) {
        return false;
    }
    const QString href = format.anchorHref();
    int start = probe;
    while (start > 0) {
        QTextCursor left(document);
        left.setPosition(start - 1);
        left.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        if (left.charFormat().anchorHref() != href) {
            break;
        }
        --start;
    }
    int end = probe + 1;
    while (end < document->characterCount()) {
        QTextCursor right(document);
        right.setPosition(end);
        right.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        if (right.charFormat().anchorHref() != href) {
            break;
        }
        ++end;
    }
    QTextCursor word(document);
    word.setPosition(start);
    word.setPosition(end, QTextCursor::KeepAnchor);
    run->start = start;
    run->end = end;
    run->href = href;
    run->display = word.selectedText();
    run->spoken = format.property(SpokenRole).toString();
    if (run->spoken.isEmpty()) {
        run->spoken = run->display;
    }
    run->id = format.property(IdRole).toInt();
    run->needsRetranscribe = format.property(RetranscribeRole).toBool();
    return true;
}

bool parseHref(const QString &href, double *start, double *end)
{
    const int hash = href.indexOf(QLatin1Char('#'));
    const int colon = href.indexOf(QLatin1Char(':'), hash + 1);
    if (hash < 0 || colon < 0) {
        return false;
    }
    bool okStart = false;
    bool okEnd = false;
    *start = href.mid(hash + 1, colon - hash - 1).toDouble(&okStart);
    *end = href.mid(colon + 1).toDouble(&okEnd);
    return okStart && okEnd && *end > *start;
}
}

QTextCharFormat StoryTranscriptEdit::formatForWord(const QString &binId, const TranscriptWord &word)
{
    QTextCharFormat format;
    format.setAnchor(true);
    format.setAnchorHref(QStringLiteral("%1#%2:%3").arg(binId).arg(word.startSec).arg(word.endSec));
    format.setProperty(SpokenRole, word.spoken);
    format.setProperty(IdRole, word.id);
    format.setProperty(RetranscribeRole, word.needsRetranscribe);
    if (word.needsRetranscribe) {
        format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        format.setUnderlineColor(Qt::darkYellow);
    }
    if (word.display != word.spoken) {
        format.setFontItalic(true);
    }
    return format;
}

bool StoryTranscriptEdit::applyCorrectionKey(QTextEdit *editor, QKeyEvent *event, TranscriptDocument *document, QString *message)
{
    if (editor == nullptr || event == nullptr) {
        return false;
    }
    const bool modified = event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (modified || event->key() == Qt::Key_Left || event->key() == Qt::Key_Right || event->key() == Qt::Key_Up || event->key() == Qt::Key_Down ||
        event->key() == Qt::Key_Home || event->key() == Qt::Key_End || event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown ||
        event->matches(QKeySequence::Copy) || event->matches(QKeySequence::SelectAll) || event->matches(QKeySequence::Undo) ||
        event->matches(QKeySequence::Redo)) {
        return false;
    }
    const bool deletion = event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete;
    const QString typed = event->text();
    if (!deletion && (typed.isEmpty() || !typed.at(0).isPrint())) {
        return false;
    }
    QTextCursor cursor = editor->textCursor();
    if (cursor.hasSelection()) {
        Run startRun;
        Run endRun;
        const bool hasStart = readRunAt(editor->document(), cursor.selectionStart(), &startRun);
        const bool hasEnd = readRunAt(editor->document(), std::max(cursor.selectionStart(), cursor.selectionEnd() - 1), &endRun);
        if (hasStart && hasEnd && startRun.id != endRun.id) {
            if (message) {
                *message = QStringLiteral("Text correction changes one word and does not cut footage. Use Delete spoken words to remove audio.");
            }
            return true;
        }
    }
    const int probe = cursor.hasSelection() ? cursor.selectionStart() : std::max(0, cursor.position() - (event->key() == Qt::Key_Backspace ? 1 : 0));
    Run run;
    if (!readRunAt(editor->document(), probe, &run)) {
        if (message) {
            *message = QStringLiteral("Place the cursor on a transcribed word to correct it.");
        }
        return true;
    }
    QString updated = run.display;
    if (cursor.hasSelection() && cursor.selectionStart() >= run.start && cursor.selectionEnd() <= run.end) {
        const int localStart = cursor.selectionStart() - run.start;
        const int localEnd = cursor.selectionEnd() - run.start;
        updated.remove(localStart, localEnd - localStart);
        if (!deletion) {
            updated.insert(localStart, typed);
        }
    } else if (event->key() == Qt::Key_Backspace) {
        const int local = cursor.position() - run.start;
        if (local <= 0 || local > updated.size()) {
            return true;
        }
        updated.remove(local - 1, 1);
    } else if (event->key() == Qt::Key_Delete) {
        const int local = cursor.position() - run.start;
        if (local < 0 || local >= updated.size()) {
            return true;
        }
        updated.remove(local, 1);
    } else {
        const int local = std::clamp(cursor.position() - run.start, 0, static_cast<int>(updated.size()));
        updated.insert(local, typed);
    }
    if (updated.trimmed().isEmpty()) {
        if (message) {
            *message = QStringLiteral("Deleting the whole word would remove spoken audio. Use Delete spoken words.");
        }
        return true;
    }
    const QString previous = run.display;
    QTextCursor replace(editor->document());
    replace.setPosition(run.start);
    replace.setPosition(run.end, QTextCursor::KeepAnchor);
    QTextCharFormat format = replace.charFormat();
    format.setProperty(SpokenRole, run.spoken);
    format.setProperty(IdRole, run.id);
    format.setFontItalic(updated != run.spoken);
    replace.insertText(updated, format);
    if (document != nullptr && run.id >= 0) {
        document->correctWord(run.id, updated);
    }
    if (message) {
        *message = QStringLiteral("Corrected “%1” to “%2” without cutting footage.").arg(previous.trimmed(), updated.trimmed());
    }
    event->accept();
    return true;
}

bool StoryTranscriptEdit::removeWordsInside(QTextEdit *editor, double startSec, double endSec)
{
    if (editor == nullptr || !(endSec > startSec)) {
        return false;
    }
    QVector<Run> runs;
    for (int i = 0; i < editor->document()->characterCount();) {
        Run run;
        if (!readRunAt(editor->document(), i, &run)) {
            ++i;
            continue;
        }
        double wordStart = 0.;
        double wordEnd = 0.;
        if (parseHref(run.href, &wordStart, &wordEnd) && wordStart >= startSec - 1e-6 && wordEnd <= endSec + 1e-6) {
            runs.push_back(run);
        }
        i = std::max(i + 1, run.end);
    }
    std::sort(runs.begin(), runs.end(), [](const Run &a, const Run &b) { return a.start > b.start; });
    for (const Run &run : runs) {
        QTextCursor cursor(editor->document());
        cursor.setPosition(run.start);
        cursor.setPosition(run.end, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
    return !runs.isEmpty();
}

void StoryTranscriptEdit::highlightWord(QTextEdit *editor, int wordId)
{
    if (editor == nullptr || wordId < 0) {
        return;
    }
    QTextCursor cursor(editor->document());
    while (!cursor.atEnd()) {
        Run run;
        if (readRunAt(editor->document(), cursor.position(), &run) && run.id == wordId) {
            QTextCursor select(editor->document());
            select.setPosition(run.start);
            select.setPosition(run.end, QTextCursor::KeepAnchor);
            if (!editor->textCursor().hasSelection()) {
                editor->setTextCursor(select);
            }
            return;
        }
        cursor.movePosition(QTextCursor::NextCharacter);
        if (run.end > cursor.position()) {
            cursor.setPosition(run.end);
        }
    }
}

void StoryTranscriptEdit::showRetranscribeFlags(QTextEdit *editor, const TranscriptDocument &document)
{
    if (editor == nullptr) {
        return;
    }
    for (const TranscriptWord &word : document.words) {
        if (!word.needsRetranscribe && word.display == word.spoken) {
            continue;
        }
        for (int i = 0; i < editor->document()->characterCount();) {
            Run run;
            if (!readRunAt(editor->document(), i, &run)) {
                ++i;
                continue;
            }
            if (run.id == word.id) {
                QTextCursor cursor(editor->document());
                cursor.setPosition(run.start);
                cursor.setPosition(run.end, QTextCursor::KeepAnchor);
                cursor.setCharFormat(formatForWord(run.href.section(QLatin1Char('#'), 0, 0), word));
                break;
            }
            i = std::max(i + 1, run.end);
        }
    }
}
