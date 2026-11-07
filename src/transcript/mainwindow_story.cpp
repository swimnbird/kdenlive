/*
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "bin/bin.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "kdenlivesettings.h"
#include "mainwindow.h"
#include "monitor/monitormanager.h"
#include "timeline2/model/timelinefunctions.hpp"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/view/timelinewidget.h"
#include "transcript/shortcutpreset.hpp"
#include "transcript/storytimelinebridge.hpp"
#include "transcript/typingguard.hpp"

#include <KActionCategory>
#include <KLocalizedString>
#include <QAction>

namespace {
std::shared_ptr<TimelineItemModel> activeTimeline()
{
    if (!pCore->window() || !pCore->window()->getCurrentTimeline()) {
        return nullptr;
    }
    return pCore->window()->getCurrentTimeline()->model();
}
}

void MainWindow::setupStoryEditing()
{
    KActionCategory *story = new KActionCategory(i18n("Story editing"), actionCollection());
    QAction *rangeTool = new QAction(i18n("Range tool"), this);
    rangeTool->setCheckable(true);
    rangeTool->setChecked(KdenliveSettings::storyRangeTool());
    connect(rangeTool, &QAction::triggered, this, &MainWindow::slotToggleStoryRange);
    story->addAction(QStringLiteral("story_range_tool"), rangeTool);

    QAction *skim = new QAction(i18n("Skim"), this);
    skim->setCheckable(true);
    skim->setChecked(KdenliveSettings::storySkim());
    connect(skim, &QAction::triggered, this, &MainWindow::slotToggleStorySkim);
    story->addAction(QStringLiteral("story_skim"), skim);

    QAction *magnetic = new QAction(i18n("Magnetic storyline"), this);
    magnetic->setToolTip(i18n("Optional. Transcript and story deletes ripple unlocked tracks. Trim, slip, and spacer tools are unchanged. Intentional gaps are kept."));
    magnetic->setCheckable(true);
    magnetic->setChecked(KdenliveSettings::storyMagnetic());
    connect(magnetic, &QAction::triggered, this, &MainWindow::slotToggleStoryMagnetic);
    story->addAction(QStringLiteral("story_magnetic"), magnetic);

    QAction *append = new QAction(i18n("Append to storyline"), this);
    connect(append, &QAction::triggered, this, &MainWindow::slotStoryAppend);
    story->addAction(QStringLiteral("story_append"), append);

    QAction *connectClip = new QAction(i18n("Connect to primary storyline"), this);
    connect(connectClip, &QAction::triggered, this, &MainWindow::slotStoryConnect);
    story->addAction(QStringLiteral("story_connect"), connectClip);

    QAction *gap = new QAction(i18n("Insert intentional gap"), this);
    connect(gap, &QAction::triggered, this, &MainWindow::slotStoryInsertGap);
    story->addAction(QStringLiteral("story_insert_gap"), gap);

    QAction *nudgeLeft = new QAction(i18n("Nudge left"), this);
    connect(nudgeLeft, &QAction::triggered, this, &MainWindow::slotStoryNudgeLeft);
    story->addAction(QStringLiteral("story_nudge_left"), nudgeLeft);
    QAction *nudgeRight = new QAction(i18n("Nudge right"), this);
    connect(nudgeRight, &QAction::triggered, this, &MainWindow::slotStoryNudgeRight);
    story->addAction(QStringLiteral("story_nudge_right"), nudgeRight);

    QAction *clipRange = new QAction(i18n("Select clip range"), this);
    connect(clipRange, &QAction::triggered, this, &MainWindow::slotStorySelectClipRange);
    story->addAction(QStringLiteral("story_select_clip_range"), clipRange);

    QAction *closeGaps = new QAction(i18n("Close unintended gaps on the primary video track"), this);
    closeGaps->setToolTip(i18n("Closes blanks on the primary video track except ranges marked as intentional gaps. Other trim tools are not magnetic."));
    connect(closeGaps, &QAction::triggered, this, &MainWindow::slotStoryCloseGaps);
    story->addAction(QStringLiteral("story_close_gaps"), closeGaps);

    QAction *useStoryKeys = new QAction(i18n("Use story editing keys"), this);
    connect(useStoryKeys, &QAction::triggered, this, &MainWindow::slotApplyStoryShortcuts);
    story->addAction(QStringLiteral("story_keyboard_story"), useStoryKeys);
    QAction *useClassicKeys = new QAction(i18n("Use classic Kdenlive keys"), this);
    connect(useClassicKeys, &QAction::triggered, this, &MainWindow::slotApplyClassicShortcuts);
    story->addAction(QStringLiteral("story_keyboard_classic"), useClassicKeys);

    auto alternate = [this, story](const QString &id, const QString &label, const QKeySequence &sequence, QAction *target) {
        QAction *action = new QAction(label, this);
        action->setShortcut(sequence);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        if (target != nullptr) {
            connect(action, &QAction::triggered, target, &QAction::trigger);
        }
        story->addAction(id, action);
    };
    alternate(QStringLiteral("story_text_reverse"), i18n("Play reverse while typing"), Qt::CTRL | Qt::Key_J,
              actionCollection()->action(QStringLiteral("monitor_seek_backward")));
    // Ctrl+K is already Kdenlive's cut shortcut in the editing schemes, so pause-while-typing uses Ctrl+Shift+K.
    alternate(QStringLiteral("story_text_pause"), i18n("Pause while typing"), Qt::CTRL | Qt::SHIFT | Qt::Key_K,
              actionCollection()->action(QStringLiteral("monitor_pause")));
    alternate(QStringLiteral("story_text_forward"), i18n("Play forward while typing"), Qt::CTRL | Qt::Key_L,
              actionCollection()->action(QStringLiteral("monitor_seek_forward")));
    // Ctrl+Space, Ctrl+I, and Ctrl+O already belong to Play Zone, Insert Zone, and Open.

    new StoryTypingGuard(actionCollection(), this);
}

void MainWindow::applyStartupStoryPreset()
{
    if (KdenliveSettings::storyKeyboardPreset() == QLatin1String("story") && KdenliveSettings::storyPresetEpoch() < 1) {
        const StoryShortcutReport report = StoryShortcuts::apply(actionCollection(), StoryShortcuts::Preset::Story);
        KdenliveSettings::setStoryKeyboardPreset(QStringLiteral("story"));
        KdenliveSettings::setStoryPresetEpoch(1);
        qInfo().noquote() << report.summary();
    }
}

void MainWindow::slotApplyStoryShortcuts()
{
    const StoryShortcutReport report = StoryShortcuts::apply(actionCollection(), StoryShortcuts::Preset::Story);
    KdenliveSettings::setStoryKeyboardPreset(QStringLiteral("story"));
    KdenliveSettings::setStoryPresetEpoch(1);
    pCore->displayMessage(i18n("Story editing keys are active."), InformationMessage, 8000);
    qInfo().noquote() << report.summary();
}

void MainWindow::slotApplyClassicShortcuts()
{
    const StoryShortcutReport report = StoryShortcuts::apply(actionCollection(), StoryShortcuts::Preset::Classic);
    KdenliveSettings::setStoryKeyboardPreset(QStringLiteral("classic"));
    pCore->displayMessage(i18n("Classic Kdenlive keys are active."), InformationMessage, 8000);
    qInfo().noquote() << report.summary();
}

void MainWindow::slotStoryAppend()
{
    const QString binId = m_clipMonitor->activeClipId();
    auto timeline = activeTimeline();
    if (binId.isEmpty() || !timeline) {
        pCore->displayMessage(i18n("No clip selected in project bin"), ErrorMessage);
        return;
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    const int end = timeline->duration();
    const QPoint zone = m_clipMonitor->getZoneInfo();
    QList<int> tracks;
    const int video = StoryTimelineBridge::primaryVideoTrack(timeline);
    if (video >= 0) {
        tracks << video;
    }
    if (!TimelineFunctions::insertZone(timeline, tracks, binId, end, zone, false, true, undo, redo)) {
        undo();
        pCore->displayMessage(i18n("Could not append the clip."), ErrorMessage);
        return;
    }
    pCore->pushUndo(undo, redo, i18n("Append to storyline"));
}

void MainWindow::slotStoryConnect()
{
    QString error;
    if (!StoryTimelineBridge::connectSelection(activeTimeline(), &error) && !error.isEmpty()) {
        pCore->displayMessage(error, ErrorMessage);
    }
}

void MainWindow::slotStoryInsertGap()
{
    auto timeline = activeTimeline();
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    const int frame = pCore->getMonitorPosition();
    const int duration = qMax(1, static_cast<int>(pCore->getCurrentFps()));
    QString error;
    if (!StoryTimelineBridge::insertIntentionalGap(timeline, frame, duration, undo, redo, &error)) {
        undo();
        pCore->displayMessage(error, ErrorMessage);
        return;
    }
    pCore->pushUndo(undo, redo, i18n("Insert intentional gap"));
}

void MainWindow::slotStoryNudgeLeft()
{
    StoryTimelineBridge::nudgeSelection(activeTimeline(), -1);
}

void MainWindow::slotStoryNudgeRight()
{
    StoryTimelineBridge::nudgeSelection(activeTimeline(), 1);
}

void MainWindow::slotStorySelectClipRange()
{
    if (!StoryTimelineBridge::selectClipRangeAtPlayhead(activeTimeline())) {
        pCore->displayMessage(i18n("No clip under the playhead."), InformationMessage);
    }
}

void MainWindow::slotToggleStorySkim()
{
    KdenliveSettings::setStorySkim(!KdenliveSettings::storySkim());
    if (auto *action = actionCollection()->action(QStringLiteral("story_skim"))) {
        action->setChecked(KdenliveSettings::storySkim());
    }
}

void MainWindow::slotToggleStoryRange()
{
    KdenliveSettings::setStoryRangeTool(!KdenliveSettings::storyRangeTool());
    if (auto *action = actionCollection()->action(QStringLiteral("story_range_tool"))) {
        action->setChecked(KdenliveSettings::storyRangeTool());
    }
}

void MainWindow::slotToggleStoryMagnetic()
{
    KdenliveSettings::setStoryMagnetic(!KdenliveSettings::storyMagnetic());
    if (auto *action = actionCollection()->action(QStringLiteral("story_magnetic"))) {
        action->setChecked(KdenliveSettings::storyMagnetic());
    }
}

void MainWindow::slotStoryCloseGaps()
{
    auto timeline = activeTimeline();
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    QString error;
    if (!StoryTimelineBridge::closeUnintendedGaps(timeline, undo, redo, &error)) {
        undo();
        if (!error.isEmpty()) {
            pCore->displayMessage(error, InformationMessage);
        }
        return;
    }
    pCore->pushUndo(undo, redo, i18n("Close unintended gaps"));
}
