/*
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QKeySequence>
#include <QList>
#include <QString>
#include <QVector>

class KActionCollection;
class QAction;

/** Final Cut-style shortcut preset.
 *  Keys are taken from Apple's Final Cut Pro user guide, read 2026-11-06.
 *  Command is Qt::CTRL (Command on macOS, Ctrl on Linux). Option is Qt::ALT.
 *  Text-field playback alternates use Qt::CTRL as well so they do not insert text.
 *  See the comments on storyBindings() for the page each key comes from.
 */
struct StoryShortcutBinding {
    QString actionId;
    QList<QKeySequence> sequences;
    int priority{100};
    QString note;
};

struct StoryShortcutConflict {
    QString sequence;
    QString winnerAction;
    QString loserAction;
    QString note;
};

struct StoryShortcutReport {
    QStringList assigned;
    QStringList missingActions;
    QVector<StoryShortcutConflict> conflicts;
    QString summary() const;
};

class StoryShortcuts
{
public:
    enum class Preset { Story, Classic };

    static QVector<StoryShortcutBinding> bindings(Preset preset);
    static StoryShortcutReport resolve(const QVector<StoryShortcutBinding> &desired);
    static StoryShortcutReport apply(KActionCollection *collection, Preset preset);
    static bool suspendWhileTyping(const QString &actionId);
    static QStringList typingWhitelist();
    /** Shortcuts the text editor must receive while it is focused. Ctrl+J playback is not one of these. */
    static bool isTextEditingShortcut(const QKeySequence &sequence);
};
