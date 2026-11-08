/*
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "transcriptdocument.hpp"

#include <QString>
#include <QTextCharFormat>
#include <QVector>

class QKeyEvent;
class QTextCursor;
class QTextEdit;

namespace StoryTranscriptEdit {

QTextCharFormat formatForWord(const QString &binId, const TranscriptWord &word);
bool applyCorrectionKey(QTextEdit *editor, QKeyEvent *event, TranscriptDocument *document, QString *message);
bool removeWordsInside(QTextEdit *editor, double startSec, double endSec);
void highlightWord(QTextEdit *editor, int wordId);
void showRetranscribeFlags(QTextEdit *editor, const TranscriptDocument &document);

} // namespace StoryTranscriptEdit
