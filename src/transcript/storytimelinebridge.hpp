/*
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "undohelper.hpp"

#include <QPair>
#include <QString>
#include <QVector>
#include <memory>

class TimelineItemModel;

struct StoryAnchor {
    int clipId{-1};
    int primaryClipId{-1};
    int anchorFrame{0};
    int offsetFrames{0};
    bool needsReview{false};
};

struct StoryGap {
    int startFrame{0};
    int endFrame{0};
};

/** Timeline edits go through TimelineFunctions / TimelineItemModel, not project XML text. */
class StoryTimelineBridge
{
public:
    static int primaryVideoTrack(const std::shared_ptr<TimelineItemModel> &timeline);
    static QVector<int> unlockedTracks(const std::shared_ptr<TimelineItemModel> &timeline);

    static QVector<StoryAnchor> loadAnchors();
    static QVector<StoryGap> loadGaps();
    static void storeAnchors(const QVector<StoryAnchor> &anchors, Fun &undo, Fun &redo);
    static void storeGaps(const QVector<StoryGap> &gaps, Fun &undo, Fun &redo);

    static QVector<StoryAnchor> shiftAnchors(const QVector<StoryAnchor> &anchors, int startFrame, int endFrame, const QVector<int> &removedClipIds);
    static QVector<StoryGap> shiftGaps(const QVector<StoryGap> &gaps, int startFrame, int endFrame);
    static bool gapIsIntentional(int startFrame, int endFrame, const QVector<StoryGap> &gaps);

    /** Ripple-delete [startFrame, endFrame) on every unlocked track, then shift subtitles, anchors, and gaps.
     *  Adds a short audio fade at the join. Does not write to source media files.
     */
    static bool rippleDeleteFrames(const std::shared_ptr<TimelineItemModel> &timeline, int startFrame, int endFrame, Fun &undo, Fun &redo, QString *error);

    static bool insertIntentionalGap(const std::shared_ptr<TimelineItemModel> &timeline, int frame, int durationFrames, Fun &undo, Fun &redo, QString *error);
    static bool closeUnintendedGaps(const std::shared_ptr<TimelineItemModel> &timeline, Fun &undo, Fun &redo, QString *error);

    static QVector<QPair<int, int>> spansForBinRange(const std::shared_ptr<TimelineItemModel> &timeline, const QString &binId, int sourceStartFrame,
                                                    int sourceEndFrame, QStringList *skipped);

    static bool nudgeSelection(const std::shared_ptr<TimelineItemModel> &timeline, int deltaFrames);
    static bool selectClipRangeAtPlayhead(const std::shared_ptr<TimelineItemModel> &timeline);
    static bool connectSelection(const std::shared_ptr<TimelineItemModel> &timeline, QString *error);

private:
    static void fadeJoin(const std::shared_ptr<TimelineItemModel> &timeline, int frame, int fadeFrames, Fun &undo, Fun &redo);
};
