/*
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QVector>

/** Silence measured from audio samples or from an ffmpeg silencedetect log.
 *  Transcript gaps are not an input.
 */
struct SilenceRegion {
    double startSec{0.};
    double endSec{0.};
};

struct SilenceParams {
    /** RMS threshold in dBFS. A window quieter than this is silence. */
    double thresholdDb{-40.};
    double minDurationSec{0.4};
    /** Audio kept on each side of a detected silence so consonants are not clipped. */
    double speechPaddingSec{0.06};
    /** Pause left in place by shorten mode. Remove mode cuts the padded region. */
    double retainedPauseSec{0.12};
};

enum class PauseEditMode { Remove, Shorten };

struct PauseEdit {
    /** Timeline/source range to ripple-delete, in seconds. */
    double cutStartSec{0.};
    double cutEndSec{0.};
    double originalStartSec{0.};
    double originalEndSec{0.};
};

class SilenceAnalyzer
{
public:
    static QVector<SilenceRegion> detectSilence(const QVector<float> &samples, int sampleRate, const SilenceParams &params);
    static QVector<SilenceRegion> parseFfmpegSilenceLog(const QString &log);
    static QVector<PauseEdit> planPauseEdits(const QVector<SilenceRegion> &regions, PauseEditMode mode, const SilenceParams &params,
                                             const QVector<SilenceRegion> &keepRegions);
};

/** Runs ffmpeg silencedetect without blocking the caller. The media file is only read. */
class SilenceAnalysisJob : public QObject
{
    Q_OBJECT
public:
    explicit SilenceAnalysisJob(QObject *parent = nullptr);
    void start(const QString &mediaUrl, const SilenceParams &params);
    void cancel();
    bool isRunning() const;

Q_SIGNALS:
    void finished(QVector<SilenceRegion> regions, QString error);

private:
    QProcess m_process;
    bool m_cancelled{false};
};
