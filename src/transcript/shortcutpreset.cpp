/*
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "shortcutpreset.hpp"

#include <KActionCollection>
#include <QAction>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace {
QString sequenceText(const QList<QKeySequence> &sequences)
{
    QStringList parts;
    for (const QKeySequence &sequence : sequences) {
        if (!sequence.isEmpty()) {
            parts.push_back(sequence.toString(QKeySequence::PortableText));
        }
    }
    return parts.join(QLatin1Char(' '));
}

StoryShortcutBinding bind1(const QString &id, QKeySequence sequence, int priority, const QString &note)
{
    StoryShortcutBinding binding;
    binding.actionId = id;
    binding.sequences = {sequence};
    binding.priority = priority;
    binding.note = note;
    return binding;
}
}

QString StoryShortcutReport::summary() const
{
    QStringList lines;
    lines << QStringLiteral("Assigned %1 shortcuts.").arg(assigned.size());
    for (const StoryShortcutConflict &conflict : conflicts) {
        lines << QStringLiteral("Conflict %1: kept %2, cleared %3 (%4)")
                     .arg(conflict.sequence, conflict.winnerAction, conflict.loserAction, conflict.note);
    }
    for (const QString &missing : missingActions) {
        lines << QStringLiteral("Missing action: %1").arg(missing);
    }
    return lines.join(QLatin1Char('\n'));
}

QVector<StoryShortcutBinding> StoryShortcuts::bindings(Preset preset)
{
    if (preset == Preset::Classic) {
        return {
            bind1(QStringLiteral("select_tool"), Qt::Key_S, 100, QStringLiteral("Kdenlive classic")),
            bind1(QStringLiteral("razor_tool"), Qt::Key_X, 100, QStringLiteral("Kdenlive classic")),
            bind1(QStringLiteral("insert_to_in_point"), Qt::Key_V, 100, QStringLiteral("Kdenlive classic")),
            bind1(QStringLiteral("overwrite_to_in_point"), Qt::Key_B, 100, QStringLiteral("Kdenlive classic")),
            bind1(QStringLiteral("remove_lift"), Qt::Key_Z, 100, QStringLiteral("Kdenlive classic")),
            bind1(QStringLiteral("audio_record"), Qt::Key_R, 100, QStringLiteral("Kdenlive classic")),
            bind1(QStringLiteral("switch_active_target"), Qt::Key_A, 100, QStringLiteral("Kdenlive classic")),
            bind1(QStringLiteral("cut_timeline_clip"), Qt::SHIFT | Qt::Key_R, 100, QStringLiteral("Kdenlive classic")),
            bind1(QStringLiteral("cut_timeline_all_clips"), Qt::CTRL | Qt::SHIFT | Qt::Key_R, 100, QStringLiteral("Kdenlive classic")),
            bind1(QStringLiteral("select_timeline_clip"), Qt::Key_Plus, 100, QStringLiteral("Kdenlive classic")),
            bind1(QStringLiteral("resize_timeline_clip_start"), Qt::Key_ParenLeft, 80, QStringLiteral("Kdenlive classic")),
            bind1(QStringLiteral("resize_timeline_clip_end"), Qt::Key_ParenRight, 80, QStringLiteral("Kdenlive classic")),
            bind1(QStringLiteral("snap"), QKeySequence(), 50, QStringLiteral("Kdenlive classic had no snap key")),
            bind1(QStringLiteral("story_range_tool"), QKeySequence(), 40, QStringLiteral("story preset only")),
            bind1(QStringLiteral("story_skim"), QKeySequence(), 40, QStringLiteral("story preset only")),
            bind1(QStringLiteral("story_append"), QKeySequence(), 40, QStringLiteral("story preset only")),
            bind1(QStringLiteral("story_connect"), QKeySequence(), 40, QStringLiteral("story preset only")),
            bind1(QStringLiteral("story_select_clip_range"), QKeySequence(), 40, QStringLiteral("story preset only")),
            bind1(QStringLiteral("story_insert_gap"), QKeySequence(), 40, QStringLiteral("story preset only")),
            bind1(QStringLiteral("story_nudge_left"), QKeySequence(), 40, QStringLiteral("story preset only")),
            bind1(QStringLiteral("story_nudge_right"), QKeySequence(), 40, QStringLiteral("story preset only")),
            bind1(QStringLiteral("story_close_gaps"), QKeySequence(), 40, QStringLiteral("story preset only")),
        };
    }

    // Apple Final Cut Pro user guide, accessed 2026-11-06.
    // Playback J/K/L: https://support.apple.com/guide/final-cut-pro/play-media-ver90ba4ef0/mac
    // Shortcut table: https://support.apple.com/guide/final-cut-pro/keyboard-shortcuts-ver90ba5929/mac
    // Blade tool B and Command-B: https://support.apple.com/guide/final-cut-pro/cut-clips-in-two-ver4e30479/mac
    // Select tool A: https://support.apple.com/guide/final-cut-pro/extend-or-shorten-clips-ver9847ec25/mac
    // Range tool R and I/O: https://support.apple.com/guide/final-cut-pro/select-ranges-ver28cca92/mac
    // Zoom tool Z: https://support.apple.com/guide/final-cut-pro/zoom-in-to-and-out-of-the-timeline-ver4e2edcc/mac
    return {
        bind1(QStringLiteral("select_tool"), Qt::Key_A, 100, QStringLiteral("Select tool, press A")),
        bind1(QStringLiteral("razor_tool"), Qt::Key_B, 100, QStringLiteral("Blade tool, press B")),
        bind1(QStringLiteral("story_range_tool"), Qt::Key_R, 100, QStringLiteral("Range Selection tool, R")),
        bind1(QStringLiteral("monitor_seek_backward"), Qt::Key_J, 100, QStringLiteral("Play Reverse, J")),
        bind1(QStringLiteral("monitor_pause"), Qt::Key_K, 100, QStringLiteral("Pause, K")),
        bind1(QStringLiteral("monitor_seek_forward"), Qt::Key_L, 100, QStringLiteral("Play Forward, L")),
        bind1(QStringLiteral("monitor_play"), Qt::Key_Space, 100, QStringLiteral("Play/Pause, Space")),
        bind1(QStringLiteral("mark_in"), Qt::Key_I, 100, QStringLiteral("Set Range Start, I")),
        bind1(QStringLiteral("mark_out"), Qt::Key_O, 100, QStringLiteral("Set Range End, O")),
        bind1(QStringLiteral("insert_to_in_point"), Qt::Key_W, 100, QStringLiteral("Insert, W")),
        bind1(QStringLiteral("overwrite_to_in_point"), Qt::Key_D, 100, QStringLiteral("Overwrite, D")),
        bind1(QStringLiteral("story_append"), Qt::Key_E, 100, QStringLiteral("Append to Storyline, E")),
        bind1(QStringLiteral("story_connect"), Qt::Key_Q, 100, QStringLiteral("Connect to Primary Storyline, Q")),
        bind1(QStringLiteral("snap"), Qt::Key_N, 100, QStringLiteral("Snapping, N")),
        bind1(QStringLiteral("story_skim"), Qt::Key_S, 100, QStringLiteral("Skimming, S")),
        bind1(QStringLiteral("view_zoom_in"), Qt::Key_Z, 90, QStringLiteral("Zoom tool Z steps timeline zoom in; there is no separate zoom tool")),
        bind1(QStringLiteral("cut_timeline_clip"), Qt::CTRL | Qt::Key_B, 100, QStringLiteral("Blade, Command-B")),
        bind1(QStringLiteral("cut_timeline_all_clips"), Qt::CTRL | Qt::SHIFT | Qt::Key_B, 100, QStringLiteral("Blade All, Shift-Command-B")),
        bind1(QStringLiteral("story_select_clip_range"), Qt::Key_X, 80, QStringLiteral("Select Clip Range, X")),
        bind1(QStringLiteral("select_timeline_clip"), Qt::Key_C, 80, QStringLiteral("Select Clip, C")),
        bind1(QStringLiteral("story_insert_gap"), Qt::ALT | Qt::Key_W, 80, QStringLiteral("Insert Gap, Option-W")),
        bind1(QStringLiteral("resize_timeline_clip_start"), Qt::ALT | Qt::Key_BracketLeft, 80, QStringLiteral("Trim Start, Option-[")),
        bind1(QStringLiteral("resize_timeline_clip_end"), Qt::ALT | Qt::Key_BracketRight, 80, QStringLiteral("Trim End, Option-]")),
        bind1(QStringLiteral("story_nudge_left"), Qt::Key_Comma, 70, QStringLiteral("Nudge Left, comma")),
        bind1(QStringLiteral("story_nudge_right"), Qt::Key_Period, 70, QStringLiteral("Nudge Right, period")),
        bind1(QStringLiteral("remove_lift"), Qt::ALT | Qt::Key_Z, 40, QStringLiteral("Conflict: Z is zoom, lift moved to Alt+Z")),
        bind1(QStringLiteral("audio_record"), Qt::ALT | Qt::Key_R, 40, QStringLiteral("Conflict: R is the range tool, record moved to Alt+R")),
        bind1(QStringLiteral("switch_active_target"), QKeySequence(), 30, QStringLiteral("Conflict: A is the select tool")),
        bind1(QStringLiteral("story_close_gaps"), QKeySequence(), 20, QStringLiteral("Menu command")),
    };
}

StoryShortcutReport StoryShortcuts::resolve(const QVector<StoryShortcutBinding> &desired)
{
    StoryShortcutReport report;
    QVector<StoryShortcutBinding> ordered = desired;
    std::stable_sort(ordered.begin(), ordered.end(), [](const StoryShortcutBinding &a, const StoryShortcutBinding &b) {
        return a.priority > b.priority;
    });
    QHash<QString, QString> owner;
    QHash<QString, QList<QKeySequence>> assigned;
    for (const StoryShortcutBinding &binding : ordered) {
        QList<QKeySequence> kept;
        for (const QKeySequence &sequence : binding.sequences) {
            if (sequence.isEmpty()) {
                continue;
            }
            const QString key = sequence.toString(QKeySequence::PortableText);
            if (owner.contains(key)) {
                StoryShortcutConflict conflict;
                conflict.sequence = key;
                conflict.winnerAction = owner.value(key);
                conflict.loserAction = binding.actionId;
                conflict.note = binding.note;
                report.conflicts.push_back(conflict);
                continue;
            }
            owner.insert(key, binding.actionId);
            kept.push_back(sequence);
        }
        assigned.insert(binding.actionId, kept);
        report.assigned << QStringLiteral("%1 = %2").arg(binding.actionId, sequenceText(kept));
    }
    return report;
}

StoryShortcutReport StoryShortcuts::apply(KActionCollection *collection, Preset preset)
{
    const QVector<StoryShortcutBinding> desired = bindings(preset);
    StoryShortcutReport report = resolve(desired);
    if (collection == nullptr) {
        return report;
    }
    QHash<QString, QList<QKeySequence>> finalKeys;
    QSet<QString> claimed;
    const StoryShortcutReport resolved = resolve(desired);
    Q_UNUSED(resolved)
    // Recompute winners the same way resolve() does, keeping the sequences.
    QVector<StoryShortcutBinding> ordered = desired;
    std::stable_sort(ordered.begin(), ordered.end(), [](const StoryShortcutBinding &a, const StoryShortcutBinding &b) {
        return a.priority > b.priority;
    });
    QHash<QString, QString> owner;
    for (const StoryShortcutBinding &binding : ordered) {
        QList<QKeySequence> kept;
        for (const QKeySequence &sequence : binding.sequences) {
            if (sequence.isEmpty()) {
                continue;
            }
            const QString key = sequence.toString(QKeySequence::PortableText);
            if (owner.contains(key)) {
                continue;
            }
            owner.insert(key, binding.actionId);
            claimed.insert(key);
            kept.push_back(sequence);
        }
        finalKeys.insert(binding.actionId, kept);
    }

    const QList<QAction *> actions = collection->actions();
    for (QAction *action : actions) {
        if (action == nullptr) {
            continue;
        }
        const QString name = action->objectName();
        if (finalKeys.contains(name)) {
            continue;
        }
        QList<QKeySequence> remain;
        const QList<QKeySequence> current = action->shortcuts();
        for (const QKeySequence &sequence : current) {
            const QString key = sequence.toString(QKeySequence::PortableText);
            if (claimed.contains(key)) {
                StoryShortcutConflict conflict;
                conflict.sequence = key;
                conflict.winnerAction = owner.value(key);
                conflict.loserAction = name;
                conflict.note = QStringLiteral("Cleared from the previous owner");
                report.conflicts.push_back(conflict);
            } else if (!sequence.isEmpty()) {
                remain.push_back(sequence);
            }
        }
        if (remain.size() != current.size()) {
            action->setShortcuts(remain);
        }
    }
    for (auto it = finalKeys.cbegin(); it != finalKeys.cend(); ++it) {
        QAction *action = collection->action(it.key());
        if (action == nullptr) {
            report.missingActions << it.key();
            continue;
        }
        action->setShortcuts(it.value());
        if (!it.value().isEmpty()) {
            collection->setDefaultShortcut(action, it.value().constFirst());
        }
    }
    return report;
}

QStringList StoryShortcuts::typingWhitelist()
{
    return {QStringLiteral("edit_undo"),
            QStringLiteral("edit_redo"),
            QStringLiteral("edit_copy"),
            QStringLiteral("edit_cut"),
            QStringLiteral("edit_paste"),
            QStringLiteral("edit_select_all"),
            QStringLiteral("file_save"),
            QStringLiteral("file_save_as"),
            QStringLiteral("file_quit"),
            QStringLiteral("story_text_reverse"),
            QStringLiteral("story_text_pause"),
            QStringLiteral("story_text_forward"),
            QStringLiteral("story_text_play"),
            QStringLiteral("story_text_mark_in"),
            QStringLiteral("story_text_mark_out")};
}

bool StoryShortcuts::suspendWhileTyping(const QString &actionId)
{
    return !typingWhitelist().contains(actionId);
}
