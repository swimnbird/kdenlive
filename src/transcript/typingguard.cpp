/*
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "typingguard.hpp"

#include "shortcutpreset.hpp"

#include <KActionCollection>
#include <QAction>
#include <QApplication>
#include <QWidget>

#include <algorithm>

StoryTypingGuard::StoryTypingGuard(KActionCollection *collection, QObject *parent)
    : QObject(parent)
    , m_collection(collection)
{
    connect(qApp, &QApplication::focusChanged, this, &StoryTypingGuard::onFocusChanged);
}

bool StoryTypingGuard::inTranscript(QWidget *widget)
{
    for (QWidget *cursor = widget; cursor != nullptr; cursor = cursor->parentWidget()) {
        if (cursor->inherits("TextBasedEdit") || cursor->inherits("VideoTextEdit")) {
            return true;
        }
    }
    return false;
}

void StoryTypingGuard::onFocusChanged(QWidget *previous, QWidget *now)
{
    Q_UNUSED(previous)
    const bool inside = inTranscript(now);
    if (inside && !m_active) {
        suspend();
    } else if (!inside && m_active) {
        restore();
    }
}

void StoryTypingGuard::suspend()
{
    if (m_collection == nullptr) {
        return;
    }
    m_active = true;
    const auto actions = m_collection->actions();
    for (QAction *action : actions) {
        if (action == nullptr) {
            continue;
        }
        const QList<QKeySequence> shortcuts = action->shortcuts();
        if (shortcuts.isEmpty()) {
            continue;
        }
        const bool releaseToText = StoryShortcuts::suspendWhileTyping(action->objectName()) ||
                                   std::any_of(shortcuts.cbegin(), shortcuts.cend(), [](const QKeySequence &sequence) {
                                       return StoryShortcuts::isTextEditingShortcut(sequence);
                                   });
        if (!releaseToText) {
            continue;
        }
        QList<QKeySequence> kept;
        if (!StoryShortcuts::suspendWhileTyping(action->objectName())) {
            for (const QKeySequence &sequence : shortcuts) {
                if (!StoryShortcuts::isTextEditingShortcut(sequence)) {
                    kept.push_back(sequence);
                }
            }
        }
        m_saved.insert(action, shortcuts);
        action->setShortcuts(kept);
    }
}

void StoryTypingGuard::restore()
{
    m_active = false;
    for (auto it = m_saved.cbegin(); it != m_saved.cend(); ++it) {
        if (it.key() != nullptr) {
            it.key()->setShortcuts(it.value());
        }
    }
    m_saved.clear();
}
