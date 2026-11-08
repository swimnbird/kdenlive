/*
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

/** Frame and sample conversions shared by transcript edits.
 *  Half-away-from-zero rounding so the same values can be checked from Python tests.
 */
namespace StoryTime {

int secondsToFrame(double seconds, double fps);
double frameToSeconds(int frame, double fps);
int secondsToSample(double seconds, int sampleRate);
double sampleToSeconds(int sample, int sampleRate);

} // namespace StoryTime
