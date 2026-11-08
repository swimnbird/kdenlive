/*
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "timeconvert.hpp"

#include <cmath>

namespace StoryTime {

static long long roundHalfAway(double value)
{
    if (value >= 0.0) {
        return static_cast<long long>(std::floor(value + 0.5));
    }
    return static_cast<long long>(std::ceil(value - 0.5));
}

int secondsToFrame(double seconds, double fps)
{
    if (!(fps > 0.0) || !std::isfinite(seconds)) {
        return 0;
    }
    return static_cast<int>(roundHalfAway(seconds * fps));
}

double frameToSeconds(int frame, double fps)
{
    if (!(fps > 0.0)) {
        return 0.0;
    }
    return static_cast<double>(frame) / fps;
}

int secondsToSample(double seconds, int sampleRate)
{
    if (sampleRate <= 0 || !std::isfinite(seconds)) {
        return 0;
    }
    return static_cast<int>(roundHalfAway(seconds * static_cast<double>(sampleRate)));
}

double sampleToSeconds(int sample, int sampleRate)
{
    if (sampleRate <= 0) {
        return 0.0;
    }
    return static_cast<double>(sample) / static_cast<double>(sampleRate);
}

} // namespace StoryTime
