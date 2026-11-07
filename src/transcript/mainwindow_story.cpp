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

#include <kddockwidgets/Config.h>
#include <kddockwidgets/core/TitleBar.h>
#include <kddockwidgets/qtwidgets/views/DropArea.h>
#include <kddockwidgets/qtwidgets/views/Group.h>
#include <kddockwidgets/qtwidgets/views/Separator.h>
#include <kddockwidgets/qtwidgets/views/TitleBar.h>
#include <KActionCategory>
#include <KColorScheme>
#include <KColorSchemeManager>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KToolBar>
#include "kddocksetup.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTimer>

namespace {
QString storyControlStyle()
{
    return QStringLiteral(
        "QToolButton {"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 6px;"
        "  padding: 4px 8px;"
        "  min-height: 26px;"
        "  min-width: 26px;"
        "}"
        "QToolButton:hover { background: #34343a; }"
        "QToolButton:pressed, QToolButton:checked {"
        "  background: #3c4e62;"
        "  border-color: #6d879f;"
        "}"
        "QToolButton:disabled { color: #6c6c72; }"
        "QToolButton:focus { border-color: #7aa4c8; }"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {"
        "  background: #1c1c1f;"
        "  color: #f0f0f2;"
        "  border: 1px solid #3e3e44;"
        "  border-radius: 5px;"
        "  padding: 3px 8px;"
        "  min-height: 22px;"
        "}"
        "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #7aa4c8; }"
        "QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { color: #6c6c72; }"
        "QComboBox::drop-down { border: none; width: 16px; }"
        "QSlider::groove:horizontal { height: 4px; background: #3a3a40; border-radius: 2px; }"
        "QSlider::handle:horizontal {"
        "  background: #e4e4e8; border: none; width: 14px; margin: -6px 0; border-radius: 7px;"
        "}"
        "QTreeView, QListView { background: transparent; border: none; outline: none; }"
        "QTreeView::item, QListView::item { padding: 5px 6px; min-height: 20px; border-radius: 4px; }"
        "QTreeView::item:hover, QListView::item:hover { background: #2c2c31; }"
        "QTreeView::item:selected, QListView::item:selected { background: #3a4e62; color: #f4f4f6; }"
        "QWidget#draggLabel { background: #2a2a2e; border-radius: 4px; }");
}
}

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
    QAction *appearance = new QAction(i18n("Story appearance"), this);
    appearance->setCheckable(true);
    appearance->setChecked(KdenliveSettings::storyAppearance() == QLatin1String("story"));
    appearance->setToolTip(i18n("Dark charcoal panels and a compact toolbar. Saved layouts and the theme menu stay available."));
    connect(appearance, &QAction::toggled, this, &MainWindow::slotToggleStoryAppearance);
    story->addAction(QStringLiteral("story_appearance"), appearance);

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

    auto panel = [this, story](const QString &id, const QString &label, const QString &tip, const QString &iconName, const QString &dockId) {
        QAction *action = new QAction(QIcon::fromTheme(iconName), label, this);
        action->setToolTip(tip);
        action->setStatusTip(tip);
        action->setCheckable(true);
        action->setChecked(true);
        connect(action, &QAction::triggered, this, [this, dockId](bool visible) { slotStoryToggleDock(dockId, visible); });
        story->addAction(id, action);
    };
    // These control Kdenlive docks. The labels follow the browser / viewer / inspector / transcript
    // arrangement described in Apple's Final Cut Pro interface guide. They are not Final Cut commands.
    panel(QStringLiteral("story_show_browser"), i18n("Browser"), i18n("Show or hide the project bin."), QStringLiteral("folder-videos"),
          QStringLiteral("project_bin"));
    panel(QStringLiteral("story_show_viewer"), i18n("Viewer"), i18n("Show or hide the project monitor."), QStringLiteral("video-television"),
          QStringLiteral("projectmonitor"));
    panel(QStringLiteral("story_show_clip"), i18n("Clip"), i18n("Show or hide the clip monitor."), QStringLiteral("kdenlive-show-video"),
          QStringLiteral("clipmonitor"));
    panel(QStringLiteral("story_show_inspector"), i18n("Inspector"), i18n("Show or hide the effect stack for the current selection."),
          QStringLiteral("document-properties"), QStringLiteral("effect_stack"));
    panel(QStringLiteral("story_show_transcript"), i18n("Transcript"), i18n("Show or hide the speech editor."), QStringLiteral("edit-rename"),
          QStringLiteral("textedit"));

    new StoryTypingGuard(actionCollection(), this);
}

void MainWindow::slotStoryToggleDock(const QString &dockId, bool visible)
{
    auto *dock = findChild<KDDockWidgets::QtWidgets::DockWidget *>(dockId);
    if (dock == nullptr) {
        return;
    }
    if (visible) {
        dock->open();
        dock->setAsCurrentTab();
        dock->raise();
    } else if (dock->isOpen()) {
        dock->forceClose();
    }
}

QString MainWindow::storySchemePath() const
{
    const QString generic = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("color-schemes/StoryCharcoal.colors"));
    if (!generic.isEmpty()) {
        return generic;
    }
    const QString appData = QStandardPaths::locate(QStandardPaths::AppLocalDataLocation, QStringLiteral("color-schemes/StoryCharcoal.colors"));
    if (!appData.isEmpty()) {
        return appData;
    }
    return QCoreApplication::applicationDirPath() + QStringLiteral("/../share/kdenlive/color-schemes/StoryCharcoal.colors");
}

void MainWindow::applyStoryMetrics(bool story)
{
    const QSize size(22, 22);
    const QString barStyle = story ? QStringLiteral("QToolBar {"
                                                    "  background: #222226;"
                                                    "  border: none;"
                                                    "  spacing: 8px;"
                                                    "  padding: 6px 10px;"
                                                    "}"
                                                    "QToolBar::separator { background: #3a3a40; width: 1px; margin: 6px 4px; }") +
                                         storyControlStyle()
                                   : QString();
    if (KToolBar *bar = toolBar()) {
        bar->setIconSize(QSize(20, 20));
        bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        bar->setStyleSheet(barStyle);
    }
    if (m_timelineToolBar != nullptr) {
        m_timelineToolBar->setIconSize(size);
        m_timelineToolBar->setStyleSheet(barStyle);
        const QStringList utilityTools = {QStringLiteral("favorite_effects"), QStringLiteral("timeline_preview_button"),
                                          QStringLiteral("audiomixer_button"), QStringLiteral("subtitle_tool"), QStringLiteral("mix_clip")};
        const auto actions = m_timelineToolBar->actions();
        for (QAction *action : actions) {
            if (utilityTools.contains(action->objectName())) {
                action->setVisible(!story);
            }
        }
    }
    if (story) {
        QFont font = this->font();
#ifdef Q_OS_MACOS
        font.setFamilies({QStringLiteral(".AppleSystemUIFont"), QStringLiteral("Helvetica Neue"), font.family()});
#else
        font.setFamilies({QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans"), QStringLiteral("Liberation Sans"), font.family()});
#endif
        if (font.pointSizeF() > 0 && font.pointSizeF() < 11.5) {
            font.setPointSizeF(11.5);
        }
        setFont(font);
        KDDockWidgets::Config::self().setTabsAtBottom(false);
        Q_EMIT pCore->hideBars(false);
    } else {
        setFont(QApplication::font());
        KDDockWidgets::Config::self().setTabsAtBottom(KdenliveSettings::tabposition() == 1);
        Q_EMIT pCore->hideBars(!KdenliveSettings::showtitlebars());
    }
}

void MainWindow::applyStoryAppearance(bool enabled)
{
    if (!enabled) {
        KColorSchemeManager::instance()->activateSchemeId(QString());
        if (m_storyBuiltInSaved) {
            KdenliveSettings::setEnableBuiltInEffects(m_storyPreviousBuiltIn);
            m_storyBuiltInSaved = false;
        }
        applyStoryMetrics(false);
        applyStoryDockChrome(false);
        if (pCore->bin() != nullptr) {
            pCore->bin()->applyStoryBrowser(false);
        }
        return;
    }
    if (!m_storyBuiltInSaved) {
        m_storyPreviousBuiltIn = KdenliveSettings::enableBuiltInEffects();
        m_storyBuiltInSaved = true;
    }
    // qtblend is Kdenlive's built-in Transform: rectangle, rotation, compositing, and opacity.
    // It stays hidden unless this existing preference is on. Story appearance turns it on for the session.
    KdenliveSettings::setEnableBuiltInEffects(true);
    const QString path = storySchemePath();
    if (QFile::exists(path)) {
        const QString genericDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/color-schemes");
        QDir().mkpath(genericDir);
        const QString published = genericDir + QStringLiteral("/StoryCharcoal.colors");
        if (!QFile::exists(published)) {
            QFile::copy(path, published);
        }
        auto *manager = KColorSchemeManager::instance();
        if (manager->indexForSchemeId(QStringLiteral("StoryCharcoal")).isValid()) {
            manager->activateSchemeId(QStringLiteral("StoryCharcoal"));
        } else {
            const KSharedConfigPtr config = KSharedConfig::openConfig(path, KConfig::SimpleConfig);
            qApp->setPalette(KColorScheme::createApplicationPalette(config));
        }
    }
    applyStoryMetrics(true);
    applyStoryDockChrome(true);
    if (pCore->bin() != nullptr) {
        pCore->bin()->applyStoryBrowser(true);
    }
}

void MainWindow::applyStoryDockChrome(bool story)
{
    const struct {
        const char *id;
        const char *fullText;
        const char *shortText;
    } items[] = {
        {"effect_stack", "Effect/Composition Stack", "Effects"},
        {"clip_properties", "Clip Properties", "Clip"},
        {"mixer", "Audio Mixer", "Mixer"},
    };
    for (const auto &item : items) {
        auto *dock = findChild<KDDockWidgets::QtWidgets::DockWidget *>(QLatin1String(item.id));
        if (dock == nullptr) {
            continue;
        }
        const QString full = i18n(item.fullText);
        const QString title = story ? i18n(item.shortText) : full;
        dock->setTitle(title);
        dock->setToolTip(full);
        if (QAction *toggle = dock->toggleAction()) {
            toggle->setText(title);
            toggle->setToolTip(full);
        }
    }
    const struct {
        const char *actionId;
        const char *dockId;
    } panels[] = {
        {"story_show_browser", "project_bin"},
        {"story_show_viewer", "projectmonitor"},
        {"story_show_clip", "clipmonitor"},
        {"story_show_inspector", "effect_stack"},
        {"story_show_transcript", "textedit"},
    };
    for (const auto &panel : panels) {
        QAction *action = actionCollection()->action(QLatin1String(panel.actionId));
        auto *dock = findChild<KDDockWidgets::QtWidgets::DockWidget *>(QLatin1String(panel.dockId));
        if (action == nullptr || dock == nullptr) {
            continue;
        }
        action->blockSignals(true);
        action->setChecked(dock->isOpen());
        action->blockSignals(false);
    }
    if (story && pCore->bin() != nullptr) {
        pCore->bin()->applyStoryBrowser(true);
    }
    applyStorySurfaces(story);
    const auto refresh = [this]() { applyStorySurfaces(KdenliveSettings::storyAppearance() == QLatin1String("story")); };
    QTimer::singleShot(0, this, refresh);
    QTimer::singleShot(400, this, refresh);
}

void MainWindow::applyStorySurfaces(bool story)
{
    const QColor backdrop(12, 12, 14);
    if (mainDockWindow != nullptr) {
        QPalette palette = mainDockWindow->palette();
        if (story) {
            palette.setColor(QPalette::Window, backdrop);
            mainDockWindow->setAutoFillBackground(true);
            mainDockWindow->setPalette(palette);
            // Fixed outer inset. This is not a saved layout and is restored with Standard appearance.
            mainDockWindow->setCenterWidgetMargins(QMargins(6, 6, 6, 6));
        } else {
            mainDockWindow->setAutoFillBackground(false);
            mainDockWindow->setPalette(QApplication::palette());
            mainDockWindow->setCenterWidgetMargins(QMargins(1, 1, 1, 1));
        }
        mainDockWindow->setContentsMargins(0, 0, 0, 0);
    }
    const auto areas = findChildren<KDDockWidgets::QtWidgets::DropArea *>();
    for (KDDockWidgets::QtWidgets::DropArea *area : areas) {
        if (story) {
            QPalette palette = area->palette();
            palette.setColor(QPalette::Window, backdrop);
            area->setAutoFillBackground(true);
            area->setPalette(palette);
        } else {
            area->setAutoFillBackground(false);
            area->setPalette(QApplication::palette());
        }
    }
    const auto groups = findChildren<KDDockWidgets::QtWidgets::Group *>();
    qInfo().noquote() << "story panel groups" << groups.size() << (story ? "rounded" : "restored");
    const auto seams = findChildren<KDDockWidgets::QtWidgets::Separator *>();
    for (KDDockWidgets::QtWidgets::Separator *seam : seams) {
        if (story) {
            seam->setAutoFillBackground(true);
            QPalette palette = seam->palette();
            palette.setColor(QPalette::Window, backdrop);
            seam->setPalette(palette);
            seam->setStyleSheet(QStringLiteral("background: #0c0c0e; border: none;"));
            seam->update();
        } else if (!seam->styleSheet().isEmpty()) {
            seam->setAutoFillBackground(false);
            seam->setPalette(QApplication::palette());
            seam->setStyleSheet(QString());
            seam->update();
        }
    }
    refreshStoryDockFrames(this);
    const auto bars = findChildren<KDDockWidgets::QtWidgets::TitleBar *>();
    const auto hidden = KDDockWidgets::TitleBarButtonType::AllTitleBarButtonTypes;
    for (KDDockWidgets::QtWidgets::TitleBar *bar : bars) {
        if (KDDockWidgets::Core::TitleBar *core = bar->titleBar()) {
            core->setUserHiddenButtons(story ? hidden : KDDockWidgets::TitleBarButtonTypes());
            core->updateButtons();
        }
        bar->setStyleSheet(story ? QStringLiteral("KDDockWidgets--QtWidgets--TitleBar {"
                                                  "  background: #2a2a2e;"
                                                  "  color: #e6e6ea;"
                                                  "  border: none;"
                                                  "  border-bottom: 1px solid #3a3a40;"
                                                  "  min-height: 30px;"
                                                  "  padding-left: 10px;"
                                                  "  font-size: 12px;"
                                                  "}")
                                  : QString());
    }
    const QString panelStyle = story ? QStringLiteral("QWidget#storyLibraryColumn, QWidget#storyThumbnailBrowser {"
                                                      "  background: #1c1c1f;"
                                                      "}"
                                                      "QWidget#storySourceBar {"
                                                      "  background: #26262a;"
                                                      "  border-radius: 6px;"
                                                      "}"
                                                      "QLabel#storyLibraryTitle {"
                                                      "  color: #b8b8be;"
                                                      "  font-size: 12px;"
                                                      "  padding: 2px 4px 6px 4px;"
                                                      "}") +
                                           storyControlStyle()
                                     : QString();
    if (pCore->bin() != nullptr) {
        pCore->bin()->setStyleSheet(panelStyle);
    }
    if (m_effectStackDock != nullptr && m_effectStackDock->widget() != nullptr) {
        m_effectStackDock->widget()->setStyleSheet(story ? storyControlStyle() + QStringLiteral("QAbstractScrollArea { background: #1c1c1f; border: none; }")
                                                         : QString());
    }
    if (m_projectMonitorDock != nullptr && m_projectMonitorDock->widget() != nullptr) {
        m_projectMonitorDock->widget()->setStyleSheet(story ? QStringLiteral("QWidget { background: #141416; }") : QString());
    }
}

void MainWindow::applyStartupStoryAppearance()
{
    QString choice = KdenliveSettings::storyAppearance();
    const QString active = KColorSchemeManager::instance()->activeSchemeId();
    if (choice.isEmpty()) {
        const bool savedLayout = KdenliveSettings::kdockLayout().contains(QLatin1String("KdenliveKDDock"));
        if (!active.isEmpty() || savedLayout) {
            KdenliveSettings::setStoryAppearance(QStringLiteral("standard"));
            if (QAction *action = actionCollection()->action(QStringLiteral("story_appearance"))) {
                action->setChecked(false);
            }
            return;
        }
        choice = QStringLiteral("story");
        KdenliveSettings::setStoryAppearance(choice);
    }
    if (choice == QLatin1String("story") && !active.isEmpty() && active != QLatin1String("StoryCharcoal")) {
        KdenliveSettings::setStoryAppearance(QStringLiteral("standard"));
        choice = QStringLiteral("standard");
    }
    const bool story = choice == QLatin1String("story");
    if (QAction *action = actionCollection()->action(QStringLiteral("story_appearance"))) {
        action->blockSignals(true);
        action->setChecked(story);
        action->blockSignals(false);
    }
    if (story) {
        applyStoryAppearance(true);
    }
}

void MainWindow::slotToggleStoryAppearance(bool enabled)
{
    KdenliveSettings::setStoryAppearance(enabled ? QStringLiteral("story") : QStringLiteral("standard"));
    applyStoryAppearance(enabled);
    pCore->displayMessage(enabled ? i18n("Story appearance is on.") : i18n("Standard appearance is on."), InformationMessage, 5000);
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
