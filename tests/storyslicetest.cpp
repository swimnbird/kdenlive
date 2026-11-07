/*
    SPDX-FileCopyrightText: 2026 Kdenlive contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "catch.hpp"
#include "test_utils.hpp"

#include "bin/clipcreator.hpp"
#include "bin/model/subtitlemodel.hpp"
#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "kdenlivesettings.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "transcript/silenceanalyzer.hpp"
#include "transcript/storytimelinebridge.hpp"
#include "transcript/timeconvert.hpp"
#include "transcript/transcriptdocument.hpp"

#include <cmath>
#include <vector>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <QAtomicInt>

#include <mlt++/MltProducer.h>

namespace {
QString programPath(const QString &name)
{
    const QStringList names = name == QLatin1String("melt") ? QStringList{QStringLiteral("melt"), QStringLiteral("melt-7")} : QStringList{name};
    for (const QString &candidate : names) {
        const QString found = QStandardPaths::findExecutable(candidate);
        if (!found.isEmpty()) {
            return found;
        }
        const QString absolute = QStringLiteral("/usr/bin/") + candidate;
        if (QFile::exists(absolute)) {
            return absolute;
        }
    }
    return QString();
}

bool run(const QString &program, const QStringList &args, QString *output)
{
    QProcess process;
    process.start(program, args);
    if (!process.waitForStarted(5000) || !process.waitForFinished(120000)) {
        if (output) {
            *output = QStringLiteral("failed to run %1").arg(program);
        }
        return false;
    }
    if (output) {
        *output = QString::fromUtf8(process.readAllStandardError() + process.readAllStandardOutput());
    }
    return process.exitCode() == 0;
}
}

TEST_CASE("Pause shortening edits real media and survives save, undo, and render", "[StorySlice]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString videoPath = dir.filePath(QStringLiteral("podcast-video.mp4"));
    const QString audioPath = dir.filePath(QStringLiteral("podcast-audio.wav"));
    const QString projectPath = dir.filePath(QStringLiteral("podcast.kdenlive"));
    const QString renderedPath = dir.filePath(QStringLiteral("podcast-out.mkv"));
    const QString tone = QStringLiteral("0.4*sin(2*PI*440*t)*between(t\\,0\\,0.4)+0.4*sin(2*PI*880*t)*between(t\\,1.2\\,1.6)");
    const QString ffmpeg = programPath(QStringLiteral("ffmpeg"));
    REQUIRE_FALSE(ffmpeg.isEmpty());
    QString log;
    REQUIRE(run(ffmpeg,
                {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                 QStringLiteral("color=c=red:s=320x240:r=25:d=2"), QStringLiteral("-an"), QStringLiteral("-c:v"), QStringLiteral("libx264"),
                 QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), videoPath},
                &log));
    REQUIRE(run(ffmpeg,
                {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                 QStringLiteral("aevalsrc=%1:sample_rate=44100:duration=2").arg(tone), audioPath},
                &log));
    REQUIRE(QFile::exists(videoPath));
    REQUIRE(QFile::exists(audioPath));
    const QFileInfo videoInfo(videoPath);
    const QFileInfo audioInfo(audioPath);
    const QDateTime videoStamp = videoInfo.lastModified();
    const QDateTime audioStamp = audioInfo.lastModified();

    REQUIRE(run(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-i"), audioPath, QStringLiteral("-af"),
                         QStringLiteral("silencedetect=noise=-35dB:d=0.3"), QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")},
                &log));
    const QVector<SilenceRegion> regions = SilenceAnalyzer::parseFfmpegSilenceLog(log);
    REQUIRE_FALSE(regions.isEmpty());
    SilenceParams params;
    params.thresholdDb = -35;
    params.minDurationSec = 0.3;
    params.speechPaddingSec = 0.04;
    params.retainedPauseSec = 0.12;
    const QVector<PauseEdit> edits = SilenceAnalyzer::planPauseEdits(regions, PauseEditMode::Shorten, params, {});
    REQUIRE_FALSE(edits.isEmpty());
    QPair<double, double> chosen(0, 0);
    for (const PauseEdit &edit : edits) {
        if (edit.originalStartSec < 1.0 && edit.originalEndSec > 0.7 && edit.cutEndSec > edit.cutStartSec) {
            chosen = {edit.cutStartSec, edit.cutEndSec};
            break;
        }
    }
    REQUIRE(chosen.second > chosen.first);
    const QVector<QPair<double, double>> cuts = {chosen};
    const int cutFrames = StoryTime::secondsToFrame(cuts.first().second, 25.0) - StoryTime::secondsToFrame(cuts.first().first, 25.0);
    REQUIRE(cutFrames > 0);

    auto binModel = pCore->projectItemModel();
    binModel->clean();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveSettings::setAudiothumbnails(false);
    KdenliveSettings::setCheckfirstprojectclip(false);
    pCore->setCurrentProfile(QStringLiteral("dv_pal"));
    KdenliveDoc document(undoStack, {1, 2});
    pCore->projectManager()->testSetDocument(&document);
    KdenliveTests::updateTimeline(false, QString(), QString(), QDateTime::currentDateTime(), 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);

    Fun clipUndo = []() { return true; };
    Fun clipRedo = []() { return true; };
    QAtomicInt loaded{0};
    const auto clipReady = [&loaded](const QString &) { loaded.fetchAndAddRelaxed(1); };
    const QString videoId = ClipCreator::createClipFromFile(videoPath, QStringLiteral("-1"), binModel, clipUndo, clipRedo, clipReady);
    const QString audioId = ClipCreator::createClipFromFile(audioPath, QStringLiteral("-1"), binModel, clipUndo, clipRedo, clipReady);
    REQUIRE(videoId != QLatin1String("-1"));
    REQUIRE(audioId != QLatin1String("-1"));
    // File clips finish on a worker that posts setProducer back to this thread. Inserting before that returns deadlocks the producer lock.
    QElapsedTimer loadTimer;
    loadTimer.start();
    while (loaded.loadRelaxed() < 2 && loadTimer.elapsed() < 30000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    REQUIRE(loaded.loadRelaxed() == 2);
    REQUIRE(binModel->getClipByBinID(videoId));
    REQUIRE(binModel->getClipByBinID(audioId));
    REQUIRE(binModel->getClipByBinID(videoId)->getFramePlaytime() > 0);
    REQUIRE(binModel->getClipByBinID(audioId)->getFramePlaytime() > 0);

    const int videoTrack = StoryTimelineBridge::primaryVideoTrack(timeline);
    int audioTrack = -1;
    for (int trackId : timeline->getAllTracksIds()) {
        if (timeline->isAudioTrack(trackId)) {
            audioTrack = trackId;
            break;
        }
    }
    REQUIRE(videoTrack >= 0);
    REQUIRE(audioTrack >= 0);
    int videoClip = -1;
    int audioClip = -1;
    int broll = -1;
    REQUIRE(timeline->requestClipInsertion(videoId, videoTrack, 0, videoClip, true, false, false));
    REQUIRE(timeline->requestClipInsertion(audioId, audioTrack, 0, audioClip, true, false, false));
    const QString colorId = KdenliveTests::createProducer(pCore->getProjectProfile(), "blue", binModel, 10, false);
    REQUIRE(timeline->requestClipInsertion(colorId, videoTrack, 70, broll, true, false, false));
    const int brollBefore = timeline->getClipPosition(broll);
    const int videoPlaytime = timeline->getClipPlaytime(videoClip);
    const int audioPlaytime = timeline->getClipPlaytime(audioClip);

    std::shared_ptr<SubtitleModel> subtitles = timeline->createSubtitleModel();
    Fun subUndo = []() { return true; };
    Fun subRedo = []() { return true; };
    SubtitleEvent caption(true, GenTime(70, 25), QString(), QString(), 0, 0, 0, QString(), QStringLiteral("after the pause"));
    REQUIRE(subtitles->addSubtitle({0, GenTime(60, 25)}, caption, subUndo, subRedo));

    TranscriptDocument transcript = TranscriptDocument::fromWhisperStdout(QStringLiteral("[0>2]\n[0.0>0.4]hello\n[1.2>1.6]again\n"));
    const double againStart = transcript.words.at(1).startSec;

    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    QString error;
    REQUIRE(StoryTimelineBridge::applySourceCuts(timeline, videoId, cuts, undo, redo, &error));
    const TranscriptRippleResult spoken = transcript.removeSpokenRange(cuts.first().first, cuts.first().second);
    CHECK(spoken.removedIds.isEmpty());
    CHECK(transcript.words.at(1).startSec == Approx(againStart));
    CHECK(timeline->getClipPosition(broll) == brollBefore - cutFrames);
    CHECK(timeline->getClipPlaytime(videoClip) < videoPlaytime);
    CHECK(timeline->getClipPlaytime(audioClip) < audioPlaytime);
    CHECK(timeline->getClipPlaytime(videoClip) == timeline->getClipPlaytime(audioClip));
    CHECK(subtitles->getAllSubtitles().first().first.second.frames(25) == 60 - cutFrames);
    const double sourceAtJoin = StoryTimelineBridge::sourceSecondsForTimelineFrame(timeline, videoId, StoryTime::secondsToFrame(cuts.first().first, 25.0), 25.0);
    CHECK(sourceAtJoin == Approx(cuts.first().second).margin(0.08));
    std::vector<int> subtitleSnaps;
    for (const auto &entry : subtitles->getAllSubtitles()) {
        subtitleSnaps.push_back(entry.first.second.frames(25));
        subtitleSnaps.push_back(entry.second.endTime().frames(25));
    }
    CHECK(timeline->checkConsistency(subtitleSnaps));
    pCore->pushUndo(undo, redo, QStringLiteral("Shorten pauses"));
    undoStack->undo();
    CHECK(timeline->getClipPosition(broll) == brollBefore);
    CHECK(subtitles->getAllSubtitles().first().first.second.frames(25) == 60);
    undoStack->redo();
    CHECK(timeline->getClipPosition(broll) == brollBefore - cutFrames);

    REQUIRE(pCore->projectManager()->testSaveFileAs(projectPath));
    Mlt::Producer reopened(pCore->getProjectProfile(), projectPath.toUtf8().constData());
    REQUIRE(reopened.is_valid());
    const int expectedFrames = timeline->duration();
    CHECK(std::abs(reopened.get_length() - expectedFrames) <= 2);

    const QString melt = programPath(QStringLiteral("melt"));
    if (!melt.isEmpty()) {
        QString renderLog;
        const bool rendered = run(melt,
                                  {projectPath, QStringLiteral("-consumer"), QStringLiteral("avformat:%1").arg(renderedPath), QStringLiteral("vcodec=libx264"),
                                   QStringLiteral("acodec=aac"), QStringLiteral("ab=96k"), QStringLiteral("real_time=-1")},
                                  &renderLog);
        if (!rendered) {
            qDebug() << renderLog;
        }
        REQUIRE(rendered);
        QString probe;
        REQUIRE(run(programPath(QStringLiteral("ffprobe")),
                    {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"), QStringLiteral("format=duration"), QStringLiteral("-of"),
                     QStringLiteral("csv=p=0"), renderedPath},
                    &probe));
        const double seconds = probe.trimmed().toDouble();
        CHECK(std::abs(seconds - (expectedFrames / 25.0)) < 0.2);
    }

    CHECK(QFileInfo(videoPath).lastModified() == videoStamp);
    CHECK(QFileInfo(audioPath).lastModified() == audioStamp);
    CHECK(QFileInfo(videoPath).size() == videoInfo.size());
    subtitles.reset();
    pCore->projectManager()->testSetActiveTimeline(nullptr);
    pCore->projectManager()->testSetDocument(nullptr);
}
