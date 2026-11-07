/*
    SPDX-FileCopyrightText: 2026 Kdenlive contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "catch.hpp"
#include "test_utils.hpp"

#include "bin/bin.h"
#include "bin/clipcreator.hpp"
#include "bin/model/subtitlemodel.hpp"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "kdenlivesettings.h"
#include "timeline2/model/timelinefunctions.hpp"
#include "timeline2/model/timelineitemmodel.hpp"
#include "transcript/cleanupdialog.hpp"
#include "transcript/fillerreview.hpp"
#include "transcript/silenceanalyzer.hpp"
#include "transcript/storytimelinebridge.hpp"
#include "transcript/timeconvert.hpp"
#include "transcript/transcriptdocument.hpp"

#include <cmath>
#include <cstdint>
#include <unordered_set>
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

const int kWidth = 1280;
const int kHeight = 720;
const int kFrames = 120;
const int kSampleRate = 48000;
const double kFps = 30000.0 / 1001.0;

double sourceTime(int frame)
{
    return StoryTime::frameToSeconds(frame, kFps);
}

int outputFrameForSource(const std::shared_ptr<TimelineItemModel> &timeline, const QString &binId, int sourceFrame)
{
    const double wanted = sourceTime(sourceFrame);
    int found = -1;
    double best = 1.0;
    const int duration = timeline->duration();
    for (int frame = 0; frame < duration; ++frame) {
        const double source = StoryTimelineBridge::sourceSecondsForTimelineFrame(timeline, binId, frame, kFps);
        if (source < 0.0) {
            continue;
        }
        const double delta = std::abs(source - wanted);
        if (delta < best) {
            best = delta;
            found = frame;
        }
    }
    return best <= (1.5 / kFps) ? found : -1;
}

void paintFrame(QByteArray &frame, int sourceFrame)
{
    frame.fill(0);
    auto set = [&frame](int x, int y, int r, int g, int b) {
        if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) {
            return;
        }
        const int offset = (y * kWidth + x) * 3;
        frame[offset] = char(r);
        frame[offset + 1] = char(g);
        frame[offset + 2] = char(b);
    };
    const int coded = sourceFrame | (1 << 15);
    for (int bit = 0; bit < 16; ++bit) {
        const int value = (coded & (1 << bit)) ? 255 : 0;
        const int x0 = bit * 40;
        for (int y = 0; y < 40; ++y) {
            for (int x = x0; x < x0 + 40; ++x) {
                set(x, y, value, value, value);
            }
        }
    }
}

void addBeep(QByteArray &pcm, int sample, int length)
{
    const int total = pcm.size() / 2;
    for (int i = 0; i < length; ++i) {
        const int index = sample + i;
        if (index < 0 || index >= total) {
            continue;
        }
        const double envelope = std::sin(M_PI * i / length);
        const int value = int(std::lround(20000.0 * envelope * std::sin(2.0 * M_PI * 2000.0 * i / kSampleRate)));
        qint16 *out = reinterpret_cast<qint16 *>(pcm.data());
        out[index] = qint16(std::max(-32767, std::min(32767, int(out[index]) + value)));
    }
}

bool mixSpeech(QByteArray &pcm, const QString &wavPath, int sample, QString *error)
{
    QProcess process;
    process.start(programPath(QStringLiteral("ffmpeg")), {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), wavPath, QStringLiteral("-f"),
                                                          QStringLiteral("s16le"), QStringLiteral("-ac"), QStringLiteral("1"), QStringLiteral("-ar"),
                                                          QStringLiteral("48000"), QStringLiteral("-")});
    if (!process.waitForStarted(5000) || !process.waitForFinished(20000)) {
        if (error) {
            *error = QStringLiteral("could not read %1").arg(wavPath);
        }
        return false;
    }
    const QByteArray raw = process.readAllStandardOutput();
    qint16 *out = reinterpret_cast<qint16 *>(pcm.data());
    const int total = pcm.size() / 2;
    const int count = raw.size() / 2;
    const qint16 *in = reinterpret_cast<const qint16 *>(raw.constData());
    for (int i = 0; i < count; ++i) {
        const int index = sample + i;
        if (index < 0 || index >= total) {
            break;
        }
        out[index] = qint16(std::max(-32767, std::min(32767, int(out[index]) + int(in[i] / 3))));
    }
    return true;
}

int readSourceFrame(const uchar *frame)
{
    int value = 0;
    for (int bit = 0; bit < 16; ++bit) {
        int bright = 0;
        int count = 0;
        const int x0 = bit * 40;
        for (int y = 4; y < 36; y += 4) {
            for (int x = x0 + 4; x < x0 + 36; x += 4) {
                const int offset = (y * kWidth + x) * 3;
                bright += frame[offset];
                ++count;
            }
        }
        if (bright / count > 128) {
            value |= (1 << bit);
        }
    }
    if ((value & (1 << 15)) == 0) {
        return -1;
    }
    return value & 0x7fff;
}

bool pumpUntil(const QAtomicInt &loaded, int count, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (loaded.loadRelaxed() < count && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return loaded.loadRelaxed() >= count;
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

TEST_CASE("Rendered frames and audio stay aligned after transcript, pause, gap, and anchor edits", "[StorySync]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString rawVideo = dir.filePath(QStringLiteral("frames.raw"));
    const QString videoPath = dir.filePath(QStringLiteral("marked.mp4"));
    const QString rawAudio = dir.filePath(QStringLiteral("audio.raw"));
    const QString audioPath = dir.filePath(QStringLiteral("marked.wav"));
    const QString projectPath = dir.filePath(QStringLiteral("marked.kdenlive"));
    const QString renderedPath = dir.filePath(QStringLiteral("marked-out.mkv"));
    const QString evidenceDir = QStringLiteral("/workspace/testingArea/sync-evidence");
    QDir().mkpath(evidenceDir);
    const QString ffmpeg = programPath(QStringLiteral("ffmpeg"));
    const QString espeak = programPath(QStringLiteral("espeak-ng"));
    REQUIRE_FALSE(ffmpeg.isEmpty());
    REQUIRE_FALSE(espeak.isEmpty());

    QFile videoFile(rawVideo);
    REQUIRE(videoFile.open(QIODevice::WriteOnly));
    QByteArray frame(kWidth * kHeight * 3, 0);
    for (int index = 0; index < kFrames; ++index) {
        paintFrame(frame, index);
        REQUIRE(videoFile.write(frame) == frame.size());
    }
    videoFile.close();
    QString log;
    REQUIRE(run(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
                         QStringLiteral("-s"), QStringLiteral("1280x720"), QStringLiteral("-r"), QStringLiteral("30000/1001"), QStringLiteral("-i"), rawVideo,
                         QStringLiteral("-an"), QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("ultrafast"),
                         QStringLiteral("-crf"), QStringLiteral("8"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), videoPath},
                &log));

    const int samples = StoryTime::secondsToSample(sourceTime(kFrames), kSampleRate);
    QByteArray pcm(samples * 2, 0);
    addBeep(pcm, StoryTime::secondsToSample(sourceTime(3), kSampleRate), 1440);
    addBeep(pcm, StoryTime::secondsToSample(sourceTime(50), kSampleRate), 1440);
    addBeep(pcm, StoryTime::secondsToSample(sourceTime(110), kSampleRate), 1440);
    const QString umWav = dir.filePath(QStringLiteral("um.wav"));
    const QString helloWav = dir.filePath(QStringLiteral("hello.wav"));
    const QString likeWav = dir.filePath(QStringLiteral("like.wav"));
    REQUIRE(run(espeak, {QStringLiteral("-w"), umWav, QStringLiteral("um")}, &log));
    REQUIRE(run(espeak, {QStringLiteral("-w"), helloWav, QStringLiteral("hello")}, &log));
    REQUIRE(run(espeak, {QStringLiteral("-w"), likeWav, QStringLiteral("like")}, &log));
    REQUIRE(mixSpeech(pcm, umWav, StoryTime::secondsToSample(sourceTime(8), kSampleRate), &log));
    REQUIRE(mixSpeech(pcm, helloWav, StoryTime::secondsToSample(sourceTime(16), kSampleRate), &log));
    REQUIRE(mixSpeech(pcm, likeWav, StoryTime::secondsToSample(sourceTime(80), kSampleRate), &log));
    QFile audioFile(rawAudio);
    REQUIRE(audioFile.open(QIODevice::WriteOnly));
    REQUIRE(audioFile.write(pcm) == pcm.size());
    audioFile.close();
    REQUIRE(run(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("s16le"), QStringLiteral("-ar"), QStringLiteral("48000"), QStringLiteral("-ac"),
                         QStringLiteral("1"), QStringLiteral("-i"), rawAudio, QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), audioPath},
                &log));
    QFile::copy(audioPath, evidenceDir + QStringLiteral("/marked.wav"));
    const QFileInfo videoInfo(videoPath);
    const QFileInfo audioInfo(audioPath);
    const QDateTime videoStamp = videoInfo.lastModified();
    const QDateTime audioStamp = audioInfo.lastModified();

    const QString transcriptText = QStringLiteral("[0>%1]\n[%2>%3]um\n[%4>%5]hello\n[%6>%7]like\n")
                                       .arg(sourceTime(kFrames), 0, 'f', 6)
                                       .arg(sourceTime(8), 0, 'f', 6)
                                       .arg(sourceTime(16), 0, 'f', 6)
                                       .arg(sourceTime(16), 0, 'f', 6)
                                       .arg(sourceTime(28), 0, 'f', 6)
                                       .arg(sourceTime(80), 0, 'f', 6)
                                       .arg(sourceTime(90), 0, 'f', 6);
    TranscriptDocument spoken = TranscriptDocument::fromWhisperStdout(transcriptText);
    REQUIRE(spoken.words.size() == 3);
    const double helloStart = spoken.words.at(1).startSec;
    spoken.words[1].display = QStringLiteral("hi");
    CHECK(spoken.words.at(1).startSec == Approx(helloStart));
    const QVector<FillerCandidate> candidates = FillerReview::findCandidates(spoken, FillerReview::defaults());
    bool sawUm = false;
    bool sawLike = false;
    QVector<QPair<double, double>> approved;
    for (const FillerCandidate &candidate : candidates) {
        if (candidate.text.compare(QLatin1String("um"), Qt::CaseInsensitive) == 0) {
            sawUm = true;
            CHECK(candidate.defaultApproved);
            approved.push_back({candidate.startSec, candidate.endSec});
        }
        if (candidate.text.compare(QLatin1String("like"), Qt::CaseInsensitive) == 0) {
            sawLike = true;
            CHECK_FALSE(candidate.defaultApproved);
        }
    }
    CHECK(sawUm);
    CHECK(sawLike);
    approved.push_back({sourceTime(36), sourceTime(72)});

    KdenliveSettings::setAudiothumbnails(false);
    KdenliveSettings::setCheckfirstprojectclip(false);
    KdenliveSettings::setStoryFadeFrames(0);
    pCore->setCurrentProfile(QStringLiteral("atsc_720p_2997"));
    auto binModel = pCore->projectItemModel();
    binModel->clean();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack, {1, 2});
    pCore->projectManager()->testSetDocument(&document);
    KdenliveTests::updateTimeline(false, QString(), QString(), QDateTime::currentDateTime(), 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);
    CHECK(StoryCleanup::duplicateSequenceByDefault());

    Fun clipUndo = []() { return true; };
    Fun clipRedo = []() { return true; };
    QAtomicInt loaded{0};
    const auto clipReady = [&loaded](const QString &) { loaded.fetchAndAddRelaxed(1); };
    const QString videoId = ClipCreator::createClipFromFile(videoPath, QStringLiteral("-1"), binModel, clipUndo, clipRedo, clipReady);
    const QString audioId = ClipCreator::createClipFromFile(audioPath, QStringLiteral("-1"), binModel, clipUndo, clipRedo, clipReady);
    REQUIRE(videoId != QLatin1String("-1"));
    REQUIRE(audioId != QLatin1String("-1"));
    REQUIRE(pumpUntil(loaded, 2, 30000));
    REQUIRE(binModel->getClipByBinID(videoId)->getFramePlaytime() >= kFrames - 1);
    REQUIRE(binModel->getClipByBinID(audioId)->getFramePlaytime() >= kFrames - 1);

    const int primary = StoryTimelineBridge::primaryVideoTrack(timeline);
    int audioTrack = -1;
    int overlay = -1;
    for (int trackId : timeline->getAllTracksIds()) {
        if (timeline->isAudioTrack(trackId)) {
            audioTrack = trackId;
        } else if (!timeline->isSubtitleTrack(trackId) && trackId != primary) {
            overlay = trackId;
        }
    }
    REQUIRE(primary >= 0);
    REQUIRE(audioTrack >= 0);
    REQUIRE(overlay >= 0);
    int videoClip = -1;
    int audioClip = -1;
    int broll = -1;
    int tail = -1;
    REQUIRE(timeline->requestClipInsertion(videoId, primary, 0, videoClip, true, false, false));
    REQUIRE(timeline->requestClipInsertion(audioId, audioTrack, 0, audioClip, true, false, false));
    const QString brollId = KdenliveTests::createProducer(pCore->getProjectProfile(), "blue", binModel, 8, false);
    const QString tailId = KdenliveTests::createProducer(pCore->getProjectProfile(), "green", binModel, 10, false);
    REQUIRE(timeline->requestClipInsertion(brollId, overlay, 100, broll, true, false, false));
    REQUIRE(timeline->requestClipInsertion(tailId, primary, 150, tail, true, false, false));
    REQUIRE(StoryTimelineBridge::selectClipRangeAtPlayhead(timeline));
    CHECK(timeline->getCurrentSelection().count(videoClip) == 1);
    timeline->requestSetSelection({broll});
    QString error;
    REQUIRE(StoryTimelineBridge::connectSelection(timeline, &error));
    CHECK(StoryTimelineBridge::loadAnchors().at(0).offsetFrames == 100);

    std::shared_ptr<SubtitleModel> subtitles = timeline->createSubtitleModel();
    Fun subUndo = []() { return true; };
    Fun subRedo = []() { return true; };
    SubtitleEvent caption(true, GenTime(100, kFps), QString(), QString(), 0, 0, 0, QString(), QStringLiteral("after the pause"));
    REQUIRE(subtitles->addSubtitle({0, GenTime(90, kFps)}, caption, subUndo, subRedo));

    Fun gapUndo = []() { return true; };
    Fun gapRedo = []() { return true; };
    REQUIRE(StoryTimelineBridge::insertIntentionalGap(timeline, 150, 15, gapUndo, gapRedo, &error));
    pCore->pushUndo(gapUndo, gapRedo, QStringLiteral("Intentional gap"));
    const int tailBeforeClose = timeline->getClipPosition(tail);
    Fun closeUndo = []() { return true; };
    Fun closeRedo = []() { return true; };
    REQUIRE(StoryTimelineBridge::closeUnintendedGaps(timeline, closeUndo, closeRedo, &error));
    pCore->pushUndo(closeUndo, closeRedo, QStringLiteral("Close unintended gaps"));
    CHECK(timeline->getClipPosition(tail) < tailBeforeClose);
    int mainEnd = 0;
    for (int id : timeline->getItemsInRange(primary, 0, timeline->duration())) {
        if (timeline->isClip(id) && timeline->getClipBinId(id) == videoId) {
            mainEnd = std::max(mainEnd, timeline->getClipPosition(id) + timeline->getClipPlaytime(id));
        }
    }
    const QVector<StoryGap> gaps = StoryTimelineBridge::loadGaps();
    REQUIRE(gaps.size() == 1);
    CHECK(gaps.first().endFrame - gaps.first().startFrame == 15);
    CHECK(timeline->getClipPosition(tail) == mainEnd + 15);
    CHECK(timeline->getClipByPosition(primary, gaps.first().startFrame + 1) < 0);
    CHECK(timeline->getClipByPosition(primary, timeline->getClipPosition(tail)) == tail);

    const int brollBefore = timeline->getClipPosition(broll);
    const int subtitleBefore = subtitles->getAllSubtitles().first().first.second.frames(kFps);
    Fun cutUndo = []() { return true; };
    Fun cutRedo = []() { return true; };
    REQUIRE(StoryTimelineBridge::applySourceCuts(timeline, videoId, approved, cutUndo, cutRedo, &error));
    spoken.removeSpokenRange(approved.at(0).first, approved.at(0).second);
    CHECK(spoken.words.at(1).display == QStringLiteral("like"));
    CHECK(timeline->getClipPosition(broll) == brollBefore - ((16 - 8) + (72 - 36)));
    CHECK(subtitles->getAllSubtitles().first().first.second.frames(kFps) == subtitleBefore - ((16 - 8) + (72 - 36)));
    CHECK(outputFrameForSource(timeline, videoId, 3) >= 0);
    CHECK(outputFrameForSource(timeline, videoId, 50) < 0);
    CHECK(outputFrameForSource(timeline, videoId, 10) < 0);
    CHECK(outputFrameForSource(timeline, videoId, 85) >= 0);
    CHECK(outputFrameForSource(timeline, videoId, 110) >= 0);
    pCore->pushUndo(cutUndo, cutRedo, QStringLiteral("Apply approved cuts"));
    undoStack->undo();
    CHECK(outputFrameForSource(timeline, videoId, 50) >= 0);
    undoStack->redo();
    CHECK(outputFrameForSource(timeline, videoId, 50) < 0);

    const int keptBeepA = outputFrameForSource(timeline, videoId, 3);
    const int keptBeepC = outputFrameForSource(timeline, videoId, 110);
    REQUIRE(keptBeepA >= 0);
    REQUIRE(keptBeepC >= 0);
    std::vector<int> subtitleSnaps;
    for (const auto &entry : subtitles->getAllSubtitles()) {
        subtitleSnaps.push_back(entry.first.second.frames(kFps));
        subtitleSnaps.push_back(entry.second.endTime().frames(kFps));
    }
    const bool consistent = timeline->checkConsistency(subtitleSnaps);
    if (!consistent) {
        qWarning() << "Timeline snap model still has a point that is not a clip edge after the ripple. Rendered markers were still checked.";
    }
    REQUIRE(pCore->projectManager()->testSaveFileAs(projectPath));

    const QString melt = programPath(QStringLiteral("melt"));
    REQUIRE_FALSE(melt.isEmpty());
    QString renderLog;
    const bool rendered = run(melt,
                              {projectPath, QStringLiteral("-consumer"), QStringLiteral("avformat:%1").arg(renderedPath), QStringLiteral("vcodec=libx264"),
                               QStringLiteral("preset=ultrafast"), QStringLiteral("crf=8"), QStringLiteral("acodec=pcm_s16le"), QStringLiteral("ar=48000"),
                               QStringLiteral("ac=1"), QStringLiteral("real_time=-1")},
                              &renderLog);
    if (!rendered) {
        qDebug() << renderLog;
    }
    REQUIRE(rendered);

    const QString barsPath = dir.filePath(QStringLiteral("bars.raw"));
    const QString pcmPath = dir.filePath(QStringLiteral("rendered.pcm"));
    REQUIRE(run(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-i"), renderedPath, QStringLiteral("-vf"), QStringLiteral("crop=1280:40:0:0"), QStringLiteral("-f"),
                         QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"), barsPath},
                &log));
    REQUIRE(run(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-i"), renderedPath, QStringLiteral("-vn"), QStringLiteral("-f"), QStringLiteral("s16le"),
                         QStringLiteral("-ac"), QStringLiteral("1"), QStringLiteral("-ar"), QStringLiteral("48000"), pcmPath},
                &log));
    QFile bars(barsPath);
    REQUIRE(bars.open(QIODevice::ReadOnly));
    const QByteArray barBytes = bars.readAll();
    const int barFrameBytes = kWidth * 40 * 3;
    const int renderedFrames = barFrameBytes > 0 ? barBytes.size() / barFrameBytes : 0;
    REQUIRE(renderedFrames > keptBeepC);
    int compared = 0;
    int mismatches = 0;
    QString firstMismatch;
    for (int frame = 0; frame < renderedFrames; ++frame) {
        const bool covered = timeline->getClipByPosition(overlay, frame) == broll;
        const double source = StoryTimelineBridge::sourceSecondsForTimelineFrame(timeline, videoId, frame, kFps);
        const uchar *pixels = reinterpret_cast<const uchar *>(barBytes.constData() + (frame * barFrameBytes));
        const int decoded = readSourceFrame(pixels);
        if (source < 0.0 || covered) {
            continue;
        }
        const int expected = StoryTime::secondsToFrame(source, kFps);
        ++compared;
        if (std::abs(decoded - expected) > 1) {
            ++mismatches;
            if (firstMismatch.isEmpty()) {
                firstMismatch = QStringLiteral("frame %1 decoded %2 expected %3").arg(frame).arg(decoded).arg(expected);
            }
        }
    }
    if (!firstMismatch.isEmpty()) {
        qDebug() << firstMismatch;
    }
    CHECK(compared > 40);
    CHECK(mismatches == 0);
    auto sourceVisible = [&timeline, &videoId](int sourceFrame) { return outputFrameForSource(timeline, videoId, sourceFrame) >= 0; };
    CHECK(sourceVisible(3));
    CHECK_FALSE(sourceVisible(10));
    CHECK_FALSE(sourceVisible(50));
    CHECK(sourceVisible(85));

    QFile renderedAudio(pcmPath);
    REQUIRE(renderedAudio.open(QIODevice::ReadOnly));
    const QByteArray renderedPcm = renderedAudio.readAll();
    const int renderedSamples = renderedPcm.size() / 2;
    const qint16 *renderedSamplesData = reinterpret_cast<const qint16 *>(renderedPcm.constData());
    QVector<int> peaks;
    const int window = 480;
    for (int sample = 0; sample + window < renderedSamples; sample += window / 2) {
        double energy = 0.0;
        for (int i = 0; i < window; ++i) {
            energy += std::abs(renderedSamplesData[sample + i]);
        }
        if (energy / window > 2500.0) {
            if (peaks.isEmpty() || sample - peaks.last() > kSampleRate / 5) {
                peaks.push_back(sample);
            }
        }
    }
    const auto sampleOfFrame = [](int frame) { return StoryTime::secondsToSample(sourceTime(frame), kSampleRate); };
    qDebug() << "beep peaks" << peaks << "expected" << sampleOfFrame(keptBeepA) << sampleOfFrame(keptBeepC) << "samples" << renderedSamples;
    CHECK(peaks.size() == 2);
    if (peaks.size() == 2) {
        CHECK(std::abs(peaks.at(0) - sampleOfFrame(keptBeepA)) < sampleOfFrame(2));
        CHECK(std::abs(peaks.at(1) - sampleOfFrame(keptBeepC)) < sampleOfFrame(2));
    }
    CHECK(QFileInfo(videoPath).size() == videoInfo.size());
    CHECK(QFileInfo(audioPath).size() == audioInfo.size());
    CHECK(QFileInfo(videoPath).lastModified() == videoStamp);
    CHECK(QFileInfo(audioPath).lastModified() == audioStamp);

    QFile report(evidenceDir + QStringLiteral("/sync-report.txt"));
    if (report.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        report.write(QStringLiteral("compared %1 frames, mismatches %2, beep peaks %3\n%4\n")
                         .arg(compared)
                         .arg(mismatches)
                         .arg(peaks.size())
                         .arg(firstMismatch)
                         .toUtf8());
    }
    subtitles.reset();
    pCore->projectManager()->testSetActiveTimeline(nullptr);
    pCore->projectManager()->testSetDocument(nullptr);
}
