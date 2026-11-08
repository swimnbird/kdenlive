/*
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QPointer>

class KActionCollection;
class QAction;
class QWidget;

/** While the transcript editor is focused, timeline shortcuts are removed so typing and
 *  text selection receive the keys. Whitelisted edit and text-field playback actions stay.
 */
class StoryTypingGuard : public QObject
{
    Q_OBJECT
public:
    explicit StoryTypingGuard(KActionCollection *collection, QObject *parent = nullptr);

private:
    void onFocusChanged(QWidget *previous, QWidget *now);
    void suspend();
    void restore();
    static bool inTranscript(QWidget *widget);

    QPointer<KActionCollection> m_collection;
    QHash<QAction *, QList<QKeySequence>> m_saved;
    bool m_active{false};
};
