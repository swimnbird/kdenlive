/*
    SPDX-FileCopyrightText: 2026 Kdenlive contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "catch.hpp"
#include "test_utils.hpp"
#include "mainwindow.h"

#include "bin/model/subtitlemodel.hpp"
#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "timeline2/model/timelinefunctions.hpp"
#include "transcript/fillerreview.hpp"
#include "transcript/shortcutpreset.hpp"
#include "transcript/silenceanalyzer.hpp"
#include "transcript/storytimelinebridge.hpp"
#include "transcript/timeconvert.hpp"
#include "transcript/transcriptdocument.hpp"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "kdenlivesettings.h"

#include <KActionCollection>
#include <KToolBar>
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QToolButton>
#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/LayoutSaver.h>
#include <kddockwidgets/qtwidgets/views/DockWidget.h>
#include <kddockwidgets/qtwidgets/views/MainWindow.h>

#include <cmath>
#include <unordered_set>

TEST_CASE("Story time conversions are frame accurate", "[StoryEdit]")
{
    CHECK(StoryTime::secondsToFrame(1.0, 25.0) == 25);
    CHECK(StoryTime::secondsToFrame(0.5, 25.0) == 13);
    CHECK(StoryTime::frameToSeconds(25, 25.0) == Approx(1.0));
    CHECK(StoryTime::secondsToSample(0.5, 48000) == 24000);
    CHECK(StoryTime::secondsToFrame(1.0, 30000.0 / 1001.0) == 30);
}

TEST_CASE("Transcript correction does not move or cut words", "[StoryEdit]")
{
    TranscriptDocument doc = TranscriptDocument::fromWhisperStdout(QStringLiteral("[0>1.5]\n[0>0.4]Hello\n[0.5>1.0]um\n[1.0>1.5]there\n"));
    REQUIRE(doc.words.size() == 3);
    const double start = doc.words.at(1).startSec;
    const double end = doc.words.at(1).endSec;
    const QString spoken = doc.words.at(1).spoken;
    REQUIRE(doc.correctWord(doc.words.at(1).id, QStringLiteral("and")));
    CHECK(doc.words.at(1).display == QStringLiteral("and"));
    CHECK(doc.words.at(1).spoken == spoken);
    CHECK(doc.words.at(1).startSec == start);
    CHECK(doc.words.at(1).endSec == end);
    CHECK(doc.words.size() == 3);
    CHECK_FALSE(doc.correctWord(999, QStringLiteral("nope")));

    const QString json = doc.toJson();
    TranscriptDocument loaded = TranscriptDocument::fromJson(json);
    REQUIRE(loaded.wordById(doc.words.at(1).id) != nullptr);
    CHECK(loaded.wordById(doc.words.at(1).id)->display == QStringLiteral("and"));
    CHECK(loaded.wordById(doc.words.at(1).id)->spoken == spoken);
}

TEST_CASE("Transcript ripple keeps later timing and flags partial words", "[StoryEdit]")
{
    TranscriptDocument doc = TranscriptDocument::fromWhisperStdout(QStringLiteral("[0>3]\n[0.0>0.4]one\n[0.5>1.2]two\n[1.3>2.0]three\n[2.2>3.0]four\n"));
    const TranscriptRippleResult result = doc.applyRipple(0.5, 1.3);
    CHECK(result.removedIds.size() == 1);
    REQUIRE(doc.words.size() == 3);
    CHECK(doc.words.at(0).display == QStringLiteral("one"));
    CHECK(doc.words.at(0).endSec == Approx(0.4));
    CHECK(doc.words.at(1).display == QStringLiteral("three"));
    CHECK(doc.words.at(1).startSec == Approx(0.5));
    CHECK(doc.words.at(2).startSec == Approx(1.4));
    CHECK(doc.search(QStringLiteral("FOUR")).size() == 1);

    TranscriptDocument partial = TranscriptDocument::fromWhisperStdout(QStringLiteral("[0>2]\n[0.0>1.0]spanning\n"));
    const TranscriptRippleResult flagged = partial.applyRipple(0.4, 0.6);
    CHECK(flagged.flaggedIds.size() == 1);
    CHECK(partial.words.at(0).needsRetranscribe);
    CHECK(partial.words.at(0).endSec == Approx(0.8));
}

TEST_CASE("Filler review keeps meaningful phrases unchecked", "[StoryEdit]")
{
    TranscriptDocument doc = TranscriptDocument::fromWhisperStdout(
        QStringLiteral("[0>3]\n[0.0>0.2]Um,\n[0.3>0.5]like\n[0.6>0.8]you\n[0.8>1.1]know\n[1.2>1.6]yes\n"));
    const QVector<FillerCandidate> found = FillerReview::findCandidates(doc, FillerReview::defaults());
    REQUIRE(found.size() == 3);
    CHECK(found.at(0).kind == FillerKind::Filler);
    CHECK(found.at(0).defaultApproved);
    CHECK(found.at(1).kind == FillerKind::Caution);
    CHECK_FALSE(found.at(1).defaultApproved);
    CHECK(found.at(2).text.contains(QStringLiteral("you")));
    CHECK_FALSE(found.at(2).defaultApproved);
}

TEST_CASE("Silence is detected from samples rather than transcript gaps", "[StoryEdit]")
{
    QVector<float> samples(48000, 0.f);
    for (int i = 8000; i < 12000; ++i) {
        samples[i] = 0.5f;
    }
    SilenceParams params;
    params.thresholdDb = -40.0;
    params.minDurationSec = 0.1;
    params.speechPaddingSec = 0.02;
    params.retainedPauseSec = 0.05;
    const QVector<SilenceRegion> regions = SilenceAnalyzer::detectSilence(samples, 16000, params);
    REQUIRE(regions.size() >= 2);
    CHECK(regions.first().startSec == Approx(0.0).margin(0.02));
    CHECK(regions.first().endSec < 0.6);

    const QString log = QStringLiteral("silence_start: 1.25\nsilence_end: 2.5 | silence_duration: 1.25\n");
    const QVector<SilenceRegion> parsed = SilenceAnalyzer::parseFfmpegSilenceLog(log);
    REQUIRE(parsed.size() == 1);
    CHECK(parsed.at(0).startSec == Approx(1.25));
    CHECK(parsed.at(0).endSec == Approx(2.5));

    QVector<SilenceRegion> keep = {{regions.first().startSec, regions.first().endSec}};
    const QVector<PauseEdit> shortened = SilenceAnalyzer::planPauseEdits(regions, PauseEditMode::Shorten, params, {});
    CHECK_FALSE(shortened.isEmpty());
    CHECK(shortened.first().cutStartSec > shortened.first().originalStartSec);
    const QVector<PauseEdit> kept = SilenceAnalyzer::planPauseEdits(regions, PauseEditMode::Remove, params, keep);
    const bool exclusionRemovedTheFirstEdit = kept.size() < shortened.size();
    CHECK(exclusionRemovedTheFirstEdit);
}

TEST_CASE("Story shortcut conflicts keep the higher priority command", "[StoryEdit]")
{
    QVector<StoryShortcutBinding> desired = StoryShortcuts::bindings(StoryShortcuts::Preset::Story);
    StoryShortcutBinding clash;
    clash.actionId = QStringLiteral("audio_record");
    clash.sequences = {QKeySequence(Qt::Key_R)};
    clash.priority = 10;
    clash.note = QStringLiteral("lower priority");
    desired.push_back(clash);
    const StoryShortcutReport report = StoryShortcuts::resolve(desired);
    bool found = false;
    for (const StoryShortcutConflict &conflict : report.conflicts) {
        if (conflict.loserAction == QStringLiteral("audio_record") && conflict.winnerAction == QStringLiteral("story_range_tool")) {
            found = true;
        }
    }
    CHECK(found);
    CHECK(StoryShortcuts::suspendWhileTyping(QStringLiteral("monitor_seek_forward")));
    CHECK(StoryShortcuts::suspendWhileTyping(QStringLiteral("edit_copy")));
    CHECK(StoryShortcuts::suspendWhileTyping(QStringLiteral("edit_undo")));
    CHECK_FALSE(StoryShortcuts::suspendWhileTyping(QStringLiteral("story_text_forward")));
    CHECK(StoryShortcuts::isTextEditingShortcut(QKeySequence(QKeySequence::Copy)));
    CHECK(StoryShortcuts::isTextEditingShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z)));
    CHECK(StoryShortcuts::isTextEditingShortcut(QKeySequence(Qt::CTRL | Qt::Key_Left)));
    CHECK_FALSE(StoryShortcuts::isTextEditingShortcut(QKeySequence(Qt::CTRL | Qt::Key_J)));
    CHECK_FALSE(StoryShortcuts::isTextEditingShortcut(QKeySequence(Qt::Key_J)));
}

TEST_CASE("Removing spoken words keeps later source times", "[StoryEdit]")
{
    TranscriptDocument doc = TranscriptDocument::fromWhisperStdout(QStringLiteral("[0>3]\n[0.0>0.4]one\n[0.5>1.2]two\n[1.3>2.0]three\n"));
    const TranscriptRippleResult removed = doc.removeSpokenRange(0.5, 1.2);
    CHECK(removed.removedIds.size() == 1);
    REQUIRE(doc.words.size() == 2);
    CHECK(doc.words.at(1).display == QStringLiteral("three"));
    CHECK(doc.words.at(1).startSec == Approx(1.3));
}

TEST_CASE("Anchors and intentional gaps follow a ripple", "[StoryEdit]")
{
    QVector<StoryAnchor> anchors = {{7, 3, 100, 10, false}, {8, 3, 20, 0, false}};
    const QVector<StoryAnchor> shifted = StoryTimelineBridge::shiftAnchors(anchors, 40, 60, {8});
    REQUIRE(shifted.size() == 1);
    CHECK(shifted.at(0).clipId == 7);
    CHECK(shifted.at(0).anchorFrame == 80);

    QVector<StoryGap> gaps = {{0, 10}, {50, 70}, {90, 110}};
    const QVector<StoryGap> moved = StoryTimelineBridge::shiftGaps(gaps, 40, 60);
    REQUIRE(moved.size() == 3);
    CHECK(moved.at(0).endFrame == 10);
    CHECK(moved.at(1).startFrame == 40);
    CHECK(moved.at(1).endFrame == 50);
    CHECK(moved.at(2).startFrame == 70);
    CHECK(moved.at(2).endFrame == 90);
    CHECK(StoryTimelineBridge::gapIsIntentional(5, 8, moved));
    CHECK_FALSE(StoryTimelineBridge::gapIsIntentional(20, 30, moved));
}

TEST_CASE("Ripple delete uses the timeline model and undoes", "[StoryEdit]")
{
    auto binModel = pCore->projectItemModel();
    binModel->clean();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    pCore->setCurrentProfile(QStringLiteral("dv_pal"));
    KdenliveDoc document(undoStack, {1, 2});
    pCore->projectManager()->testSetDocument(&document);
    KdenliveTests::updateTimeline(false, QString(), QString(), QDateTime::currentDateTime(), 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);

    const int video = timeline->getTrackIndexFromPosition(2);
    const int other = timeline->getTrackIndexFromPosition(1);
    QString binId = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", binModel, 100, false);
    QString otherId = KdenliveTests::createProducer(pCore->getProjectProfile(), "blue", binModel, 80, false);
    int first = -1;
    int second = -1;
    int connected = -1;
    REQUIRE(timeline->requestClipInsertion(binId, video, 0, first, true, false, false));
    REQUIRE(timeline->requestClipInsertion(binId, video, 100, second, true, false, false));
    REQUIRE(timeline->requestClipInsertion(otherId, other, 120, connected, true, false, false));
    const int before = timeline->getClipPosition(second);

    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    QString error;
    REQUIRE(StoryTimelineBridge::rippleDeleteFrames(timeline, 40, 60, undo, redo, &error));
    CHECK(timeline->getClipPosition(second) == before - 20);
    CHECK(timeline->getClipPosition(connected) == 100);
    CHECK(timeline->checkConsistency());
    undo();
    CHECK(timeline->getClipPosition(second) == before);
    CHECK(timeline->getClipPosition(connected) == 120);
    CHECK(timeline->checkConsistency());

    std::shared_ptr<SubtitleModel> subtitles = timeline->createSubtitleModel();
    Fun subUndo = []() { return true; };
    Fun subRedo = []() { return true; };
    const double fps = pCore->getCurrentFps();
    SubtitleEvent event(true, GenTime(170, fps), QString(), QString(), 0, 0, 0, QString(), QStringLiteral("caption"));
    REQUIRE(subtitles->addSubtitle({0, GenTime(150, fps)}, event, subUndo, subRedo));
    Fun rippleUndo = []() { return true; };
    Fun rippleRedo = []() { return true; };
    REQUIRE(StoryTimelineBridge::rippleDeleteFrames(timeline, 40, 60, rippleUndo, rippleRedo, &error));
    const auto remaining = subtitles->getAllSubtitles();
    REQUIRE_FALSE(remaining.isEmpty());
    CHECK(remaining.first().first.second.frames(pCore->getCurrentFps()) == 130);
    rippleUndo();
    CHECK(subtitles->getAllSubtitles().first().first.second.frames(pCore->getCurrentFps()) == 150);
    pCore->projectManager()->testSetDocument(nullptr);
}

TEST_CASE("Story preset commands select, connect, and append on the timeline", "[StoryEdit]")
{
    QObject parent;
    KActionCollection collection(&parent, QStringLiteral("story-preset-test"));
    const QStringList names = {QStringLiteral("story_range_tool"), QStringLiteral("insert_to_in_point"), QStringLiteral("story_append"),
                               QStringLiteral("story_connect"), QStringLiteral("select_tool"), QStringLiteral("edit_copy"),
                               QStringLiteral("edit_paste"), QStringLiteral("edit_undo")};
    for (const QString &name : names) {
        auto *action = new QAction(&parent);
        action->setObjectName(name);
        collection.addAction(name, action);
    }
    StoryShortcuts::apply(&collection, StoryShortcuts::Preset::Story);
    CHECK(collection.action(QStringLiteral("story_range_tool"))->shortcut() == QKeySequence(Qt::Key_R));
    CHECK(collection.action(QStringLiteral("insert_to_in_point"))->shortcut() == QKeySequence(Qt::Key_W));
    CHECK(collection.action(QStringLiteral("story_append"))->shortcut() == QKeySequence(Qt::Key_E));
    CHECK(collection.action(QStringLiteral("story_connect"))->shortcut() == QKeySequence(Qt::Key_Q));
    CHECK(collection.action(QStringLiteral("select_tool"))->shortcut() == QKeySequence(Qt::Key_A));
    const QList<QKeySequence> textKeys = {QKeySequence(QKeySequence::Copy), QKeySequence(QKeySequence::Paste), QKeySequence(QKeySequence::Undo),
                                          QKeySequence(Qt::Key_Left), QKeySequence(Qt::Key_Right), QKeySequence(Qt::Key_Backspace)};
    const QList<QAction *> actions = collection.actions();
    for (QAction *action : actions) {
        for (const QKeySequence &textKey : textKeys) {
            CHECK(action->shortcut() != textKey);
        }
    }

    auto binModel = pCore->projectItemModel();
    binModel->clean();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    pCore->setCurrentProfile(QStringLiteral("dv_pal"));
    KdenliveDoc document(undoStack, {1, 2});
    pCore->projectManager()->testSetDocument(&document);
    KdenliveTests::updateTimeline(false, QString(), QString(), QDateTime::currentDateTime(), 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);
    const int primary = StoryTimelineBridge::primaryVideoTrack(timeline);
    int otherVideo = -1;
    for (int trackId : timeline->getAllTracksIds()) {
        if (!timeline->isAudioTrack(trackId) && !timeline->isSubtitleTrack(trackId) && trackId != primary) {
            otherVideo = trackId;
        }
    }
    REQUIRE(primary >= 0);
    REQUIRE(otherVideo >= 0);
    const QString mainId = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", binModel, 40, false);
    const QString connectedId = KdenliveTests::createProducer(pCore->getProjectProfile(), "blue", binModel, 8, false);
    const QString appendId = KdenliveTests::createProducer(pCore->getProjectProfile(), "green", binModel, 6, false);
    int mainClip = -1;
    int connected = -1;
    REQUIRE(timeline->requestClipInsertion(mainId, primary, 0, mainClip, true, false, false));
    REQUIRE(timeline->requestClipInsertion(connectedId, otherVideo, 12, connected, true, false, false));

    REQUIRE(StoryTimelineBridge::selectClipRangeAtPlayhead(timeline));
    CHECK(timeline->getCurrentSelection().count(mainClip) == 1);

    timeline->requestSetSelection({connected});
    QString error;
    REQUIRE(StoryTimelineBridge::connectSelection(timeline, &error));
    const QVector<StoryAnchor> anchors = StoryTimelineBridge::loadAnchors();
    REQUIRE(anchors.size() == 1);
    CHECK(anchors.at(0).clipId == connected);
    CHECK(anchors.at(0).primaryClipId == mainClip);
    CHECK(anchors.at(0).offsetFrames == 12);

    const int end = timeline->duration();
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    REQUIRE(TimelineFunctions::insertZone(timeline, {primary}, appendId, end, QPoint(0, 6), false, false, undo, redo));
    bool appended = false;
    for (int trackId : timeline->getAllTracksIds()) {
        const auto items = timeline->getItemsInRange(trackId, end, end + 6);
        for (int id : items) {
            if (timeline->isClip(id) && timeline->getClipBinId(id) == appendId && timeline->getClipPosition(id) == end) {
                appended = true;
            }
        }
    }
    CHECK(appended);
    CHECK(timeline->checkConsistency());
    pCore->projectManager()->testSetActiveTimeline(nullptr);
    pCore->projectManager()->testSetDocument(nullptr);
}

TEST_CASE("Story layout keeps a full-width timeline under the viewer", "[StoryLayout]")
{
    const QStringList candidates = {QStringLiteral("/workspace/data/layouts/story.json"),
                                    QStringLiteral("/usr/share/kdenlive/layouts/story.json")};
    QString path;
    for (const QString &candidate : candidates) {
        if (QFile::exists(candidate)) {
            path = candidate;
            break;
        }
    }
    REQUIRE_FALSE(path.isEmpty());
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject layout = root.value(QStringLiteral("mainWindows")).toArray().at(0).toObject().value(QStringLiteral("multiSplitterLayout")).toObject();
    const QJsonObject frames = layout.value(QStringLiteral("frames")).toObject();
    const QJsonObject tree = layout.value(QStringLiteral("layout")).toObject();
    REQUIRE(tree.value(QStringLiteral("orientation")).toInt() == 2);
    const QJsonArray rows = tree.value(QStringLiteral("children")).toArray();
    REQUIRE(rows.size() == 2);
    const QJsonObject top = rows.at(0).toObject();
    const QJsonObject bottom = rows.at(1).toObject();
    CHECK(top.value(QStringLiteral("orientation")).toInt() == 1);
    CHECK(bottom.value(QStringLiteral("guestId")).toString() == QStringLiteral("13767"));
    const QJsonArray topChildren = top.value(QStringLiteral("children")).toArray();
    REQUIRE(topChildren.size() == 3);
    const QString viewerId = topChildren.at(1).toObject().value(QStringLiteral("guestId")).toString();
    const QString inspectorId = topChildren.at(2).toObject().value(QStringLiteral("guestId")).toString();
    CHECK(frames.value(viewerId).toObject().value(QStringLiteral("dockWidgets")).toArray().contains(QStringLiteral("projectmonitor")));
    CHECK(frames.value(inspectorId).toObject().value(QStringLiteral("dockWidgets")).toArray().contains(QStringLiteral("effect_stack")));
    CHECK(frames.value(QStringLiteral("13767")).toObject().value(QStringLiteral("dockWidgets")).toArray().contains(QStringLiteral("timeline")));
    CHECK_FALSE(frames.contains(QStringLiteral("14001")));
    const double timelineShare = bottom.value(QStringLiteral("sizingInfo")).toObject().value(QStringLiteral("percentageWithinParent")).toDouble();
    CHECK(timelineShare > 0.35);
}

TEST_CASE("Timeline clip transform parameter can be undone", "[StoryInspector]")
{
    auto binModel = pCore->projectItemModel();
    binModel->clean();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    pCore->setCurrentProfile(QStringLiteral("dv_pal"));
    KdenliveDoc document(undoStack, {1, 2});
    pCore->projectManager()->testSetDocument(&document);
    KdenliveTests::updateTimeline(false, QString(), QString(), QDateTime::currentDateTime(), 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);
    const bool previous = KdenliveSettings::enableBuiltInEffects();
    KdenliveSettings::setEnableBuiltInEffects(true);
    const QString binId = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", binModel, 8, false);
    int clipId = -1;
    REQUIRE(timeline->requestClipInsertion(binId, StoryTimelineBridge::primaryVideoTrack(timeline), 0, clipId, true, false, false));
    auto stack = timeline->getClipEffectStackModel(clipId);
    REQUIRE(stack);
    REQUIRE(stack->appendEffect(QStringLiteral("qtblend")));
    auto asset = stack->getAssetModelById(QStringLiteral("qtblend"));
    REQUIRE(asset);
    auto effect = std::dynamic_pointer_cast<EffectItemModel>(asset);
    REQUIRE(effect);
    CHECK_FALSE(effect->hideFromStack());
    const QString before = effect->getParamFromName(QStringLiteral("rotation")).toString();
    effect->setParametersFromTask({{QStringLiteral("rotation"), QStringLiteral("15")}});
    const QString changed = effect->getParamFromName(QStringLiteral("rotation")).toString();
    CHECK(changed.contains(QLatin1String("15")));
    CHECK(changed != before);
    undoStack->undo();
    CHECK(effect->getParamFromName(QStringLiteral("rotation")).toString() == before);
    KdenliveSettings::setEnableBuiltInEffects(previous);
    pCore->projectManager()->testSetActiveTimeline(nullptr);
    pCore->projectManager()->testSetDocument(nullptr);
}

TEST_CASE("Story panel buttons hide and reopen the same dock", "[StoryPanels]")
{
    KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);
    auto *window = new KDDockWidgets::QtWidgets::MainWindow(QStringLiteral("StoryPanelToggleHarness"));
    window->resize(960, 640);
    window->show();
    QApplication::processEvents();

    const struct {
        const char *id;
        KDDockWidgets::Location location;
    } panels[] = {
        {"project_bin", KDDockWidgets::Location_OnLeft},
        {"projectmonitor", KDDockWidgets::Location_OnRight},
        {"effect_stack", KDDockWidgets::Location_OnRight},
        {"textedit", KDDockWidgets::Location_OnBottom},
    };
    QList<KDDockWidgets::QtWidgets::DockWidget *> docks;
    QList<QAction *> actions;
    QList<QToolButton *> buttons;
    auto *bar = new KToolBar(window);
    for (const auto &panel : panels) {
        auto *dock = new KDDockWidgets::QtWidgets::DockWidget(QLatin1String(panel.id));
        dock->setWidget(new QLabel(QLatin1String(panel.id)));
        window->addDockWidget(dock, panel.location, docks.isEmpty() ? nullptr : docks.constLast());
        auto *action = new QAction(QLatin1String(panel.id), window);
        action->setCheckable(true);
        action->setChecked(true);
        MainWindow::bindStoryPanelAction(dock, action);
        const QString dockId = QLatin1String(panel.id);
        QObject::connect(action, &QAction::triggered, action, [action, dockId](bool visible) {
            MainWindow::setStoryPanelVisible(dockId, action, visible);
        });
        bar->addAction(action);
        auto *button = qobject_cast<QToolButton *>(bar->widgetForAction(action));
        REQUIRE(button != nullptr);
        docks.append(dock);
        actions.append(action);
        buttons.append(button);
    }
    bar->show();
    QApplication::processEvents();

    KDDockWidgets::LayoutSaver saver;
    const QByteArray openLayout = saver.serializeLayout();
    REQUIRE_FALSE(openLayout.isEmpty());

    for (int i = 0; i < docks.size(); ++i) {
        auto *dock = docks.at(i);
        auto *action = actions.at(i);
        auto *button = buttons.at(i);
        REQUIRE(dock->isOpen());
        REQUIRE(action->isChecked());
        REQUIRE(dock->widget()->isVisible());

        button->click();
        QApplication::processEvents();
        CHECK_FALSE(dock->isOpen());
        CHECK_FALSE(action->isChecked());
        CHECK(MainWindow::storyDockByName(QLatin1String(panels[i].id)) == dock);

        button->click();
        QApplication::processEvents();
        CHECK(dock->isOpen());
        CHECK(action->isChecked());
        CHECK(dock->widget()->isVisible());

        button->click();
        QApplication::processEvents();
        CHECK_FALSE(dock->isOpen());
        dock->toggleAction()->trigger();
        QApplication::processEvents();
        CHECK(dock->isOpen());
        CHECK(action->isChecked());
        dock->toggleAction()->trigger();
        QApplication::processEvents();
        CHECK_FALSE(dock->isOpen());
        CHECK_FALSE(action->isChecked());
        button->click();
        QApplication::processEvents();
        CHECK(dock->isOpen());
        CHECK(action->isChecked());
    }

    window->resize(1200, 760);
    QApplication::processEvents();
    for (int i = 0; i < docks.size(); ++i) {
        CHECK(docks.at(i)->isOpen());
        CHECK(actions.at(i)->isChecked());
        MainWindow::bindStoryPanelAction(docks.at(i), actions.at(i));
        CHECK(actions.at(i)->isChecked() == docks.at(i)->isOpen());
    }

    buttons.at(2)->click();
    QApplication::processEvents();
    CHECK_FALSE(docks.at(2)->isOpen());
    const QByteArray closedLayout = saver.serializeLayout();
    buttons.at(2)->click();
    QApplication::processEvents();
    CHECK(docks.at(2)->isOpen());
    REQUIRE(saver.restoreLayout(closedLayout));
    QApplication::processEvents();
    for (int i = 0; i < docks.size(); ++i) {
        MainWindow::bindStoryPanelAction(docks.at(i), actions.at(i));
    }
    CHECK_FALSE(docks.at(2)->isOpen());
    CHECK_FALSE(actions.at(2)->isChecked());
    REQUIRE(saver.restoreLayout(openLayout));
    QApplication::processEvents();
    for (int i = 0; i < docks.size(); ++i) {
        MainWindow::bindStoryPanelAction(docks.at(i), actions.at(i));
        CHECK(docks.at(i)->isOpen());
        CHECK(actions.at(i)->isChecked());
    }

    window->hide();
    delete window;
}
