/*
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "silenceanalyzer.hpp"

#include <QRegularExpression>

#include <cmath>
#include <algorithm>

namespace {
bool overlaps(double a0, double a1, double b0, double b1)
{
    return a1 > b0 && a0 < b1;
}
}

QVector<SilenceRegion> SilenceAnalyzer::detectSilence(const QVector<float> &samples, int sampleRate, const SilenceParams &params)
{
    QVector<SilenceRegion> regions;
    if (sampleRate <= 0 || samples.isEmpty() || !(params.minDurationSec >= 0.0)) {
        return regions;
    }
    const double amplitude = std::pow(10.0, params.thresholdDb / 20.0);
    const int window = std::max(1, sampleRate / 100);
    const int minSamples = std::max(1, static_cast<int>(std::llround(params.minDurationSec * sampleRate)));
    int runStart = -1;
    for (int i = 0; i + window <= samples.size(); i += window) {
        double energy = 0.0;
        for (int k = 0; k < window; ++k) {
            const double sample = samples.at(i + k);
            energy += sample * sample;
        }
        const double rms = std::sqrt(energy / window);
        const bool silent = rms < amplitude;
        if (silent && runStart < 0) {
            runStart = i;
        } else if (!silent && runStart >= 0) {
            if (i - runStart >= minSamples) {
                regions.push_back({static_cast<double>(runStart) / sampleRate, static_cast<double>(i) / sampleRate});
            }
            runStart = -1;
        }
    }
    if (runStart >= 0 && samples.size() - runStart >= minSamples) {
        regions.push_back({static_cast<double>(runStart) / sampleRate, static_cast<double>(samples.size()) / sampleRate});
    }
    return regions;
}

QVector<SilenceRegion> SilenceAnalyzer::parseFfmpegSilenceLog(const QString &log)
{
    QVector<SilenceRegion> regions;
    const QRegularExpression startRe(QStringLiteral("silence_start:\\s*([0-9.+-eE]+)"));
    const QRegularExpression endRe(QStringLiteral("silence_end:\\s*([0-9.+-eE]+)"));
    double open = -1.0;
    const QStringList lines = log.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QRegularExpressionMatch startMatch = startRe.match(line);
        if (startMatch.hasMatch()) {
            open = startMatch.captured(1).toDouble();
            continue;
        }
        const QRegularExpressionMatch endMatch = endRe.match(line);
        if (endMatch.hasMatch() && open >= 0.0) {
            const double end = endMatch.captured(1).toDouble();
            if (end > open) {
                regions.push_back({open, end});
            }
            open = -1.0;
        }
    }
    return regions;
}

QVector<PauseEdit> SilenceAnalyzer::planPauseEdits(const QVector<SilenceRegion> &regions, PauseEditMode mode, const SilenceParams &params,
                                                   const QVector<SilenceRegion> &keepRegions)
{
    QVector<PauseEdit> edits;
    for (const SilenceRegion &region : regions) {
        const double paddedStart = region.startSec + std::max(0.0, params.speechPaddingSec);
        const double paddedEnd = region.endSec - std::max(0.0, params.speechPaddingSec);
        if (!(paddedEnd - paddedStart > 0.0) || paddedEnd - paddedStart < params.minDurationSec) {
            continue;
        }
        bool kept = false;
        for (const SilenceRegion &keep : keepRegions) {
            if (overlaps(paddedStart, paddedEnd, keep.startSec, keep.endSec)) {
                kept = true;
                break;
            }
        }
        if (kept) {
            continue;
        }
        PauseEdit edit;
        edit.originalStartSec = region.startSec;
        edit.originalEndSec = region.endSec;
        if (mode == PauseEditMode::Remove) {
            edit.cutStartSec = paddedStart;
            edit.cutEndSec = paddedEnd;
        } else {
            const double keep = std::clamp(params.retainedPauseSec, 0.0, paddedEnd - paddedStart);
            edit.cutStartSec = paddedStart + keep;
            edit.cutEndSec = paddedEnd;
        }
        if (edit.cutEndSec - edit.cutStartSec > 1e-4) {
            edits.push_back(edit);
        }
    }
    return edits;
}

SilenceAnalysisJob::SilenceAnalysisJob(QObject *parent)
    : QObject(parent)
{
    connect(&m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        const QString log = QString::fromUtf8(m_process.readAllStandardError());
        if (m_cancelled) {
            Q_EMIT finished({}, QString());
            return;
        }
        if (status != QProcess::NormalExit || exitCode != 0) {
            Q_EMIT finished({}, log.isEmpty() ? QStringLiteral("ffmpeg failed") : log);
            return;
        }
        Q_EMIT finished(SilenceAnalyzer::parseFfmpegSilenceLog(log), QString());
    });
}

void SilenceAnalysisJob::start(const QString &mediaUrl, const SilenceParams &params)
{
    if (m_process.state() != QProcess::NotRunning) {
        return;
    }
    m_cancelled = false;
    const QString filter = QStringLiteral("silencedetect=noise=%1dB:d=%2").arg(params.thresholdDb).arg(params.minDurationSec);
    m_process.start(QStringLiteral("ffmpeg"), {QStringLiteral("-hide_banner"), QStringLiteral("-i"), mediaUrl, QStringLiteral("-af"), filter,
                                              QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")});
}

void SilenceAnalysisJob::cancel()
{
    m_cancelled = true;
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
    }
}

bool SilenceAnalysisJob::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}
