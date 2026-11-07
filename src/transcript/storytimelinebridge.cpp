/*
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "storytimelinebridge.hpp"

#include "bin/model/subtitlemodel.hpp"
#include "core.h"
#include "mainwindow.h"
#include "timeline2/view/timelinewidget.h"
#include "doc/kdenlivedoc.h"
#include "kdenlivesettings.h"
#include "transcript/timeconvert.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "effects/effectsrepository.hpp"
#include "timeline2/model/clipmodel.hpp"
#include "timeline2/model/timelinefunctions.hpp"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/model/timelinemodel.hpp"

#include <KLocalizedString>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
const QString kAnchors = QStringLiteral("kdenlive:storyanchors");
const QString kGaps = QStringLiteral("kdenlive:storygaps");

QString anchorsToJson(const QVector<StoryAnchor> &anchors)
{
    QJsonArray array;
    for (const StoryAnchor &anchor : anchors) {
        QJsonObject obj;
        obj.insert(QStringLiteral("clip"), anchor.clipId);
        obj.insert(QStringLiteral("primary"), anchor.primaryClipId);
        obj.insert(QStringLiteral("frame"), anchor.anchorFrame);
        obj.insert(QStringLiteral("offset"), anchor.offsetFrames);
        obj.insert(QStringLiteral("review"), anchor.needsReview);
        array.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QVector<StoryAnchor> anchorsFromJson(const QString &json)
{
    QVector<StoryAnchor> anchors;
    const QJsonArray array = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        StoryAnchor anchor;
        anchor.clipId = obj.value(QStringLiteral("clip")).toInt(-1);
        anchor.primaryClipId = obj.value(QStringLiteral("primary")).toInt(-1);
        anchor.anchorFrame = obj.value(QStringLiteral("frame")).toInt();
        anchor.offsetFrames = obj.value(QStringLiteral("offset")).toInt();
        anchor.needsReview = obj.value(QStringLiteral("review")).toBool();
        if (anchor.clipId >= 0) {
            anchors.push_back(anchor);
        }
    }
    return anchors;
}

QString gapsToJson(const QVector<StoryGap> &gaps)
{
    QJsonArray array;
    for (const StoryGap &gap : gaps) {
        QJsonObject obj;
        obj.insert(QStringLiteral("start"), gap.startFrame);
        obj.insert(QStringLiteral("end"), gap.endFrame);
        array.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QVector<StoryGap> gapsFromJson(const QString &json)
{
    QVector<StoryGap> gaps;
    const QJsonArray array = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        StoryGap gap;
        gap.startFrame = obj.value(QStringLiteral("start")).toInt();
        gap.endFrame = obj.value(QStringLiteral("end")).toInt();
        if (gap.endFrame > gap.startFrame) {
            gaps.push_back(gap);
        }
    }
    return gaps;
}

void rememberProperty(const QString &name, const QString &value, Fun &undo, Fun &redo)
{
    const QString previous = pCore->currentDoc() ? pCore->currentDoc()->getDocumentProperty(name) : QString();
    Fun operation = [name, value]() {
        if (pCore->currentDoc()) {
            pCore->currentDoc()->setDocumentProperty(name, value);
        }
        return true;
    };
    Fun reverse = [name, previous]() {
        if (pCore->currentDoc()) {
            pCore->currentDoc()->setDocumentProperty(name, previous);
        }
        return true;
    };
    operation();
    PUSH_LAMBDA(operation, redo);
    PUSH_FRONT_LAMBDA(reverse, undo);
}

} // namespace

void StoryTimelineBridge::fadeJoin(const std::shared_ptr<TimelineItemModel> &timeline, int frame, int fadeFrames, Fun &undo, Fun &redo)
{
    if (fadeFrames <= 0 || !EffectsRepository::get()->exists(QStringLiteral("fadeout"))) {
        return;
    }
    const QVector<int> tracks = StoryTimelineBridge::unlockedTracks(timeline);
    for (int trackId : tracks) {
        const int leftId = frame > 0 ? timeline->getClipByPosition(trackId, frame - 1) : -1;
        const int rightId = timeline->getClipByPosition(trackId, frame);
        const int sides[2] = {leftId, rightId};
        const bool fromStart[2] = {false, true};
        for (int i = 0; i < 2; ++i) {
            const int clipId = sides[i];
            if (clipId < 0 || !timeline->isClip(clipId)) {
                continue;
            }
            std::shared_ptr<ClipModel> clip = timeline->getClipPtr(clipId);
            if (!clip || (!clip->isAudioOnly() && !clip->audioEnabled())) {
                continue;
            }
            auto stack = timeline->getClipEffectStack(clipId);
            if (!stack) {
                continue;
            }
            const int oldLength = fromStart[i] ? clip->fadeIn() : clip->fadeOut();
            const bool start = fromStart[i];
            Fun operation = [stack, fadeFrames, start]() {
                stack->adjustFadeLength(fadeFrames, start, true, false, false);
                return true;
            };
            Fun reverse = [timeline, clipId, oldLength, start]() {
                if (oldLength <= 0) {
                    timeline->removeFade(clipId, start);
                } else if (auto again = timeline->getClipEffectStack(clipId)) {
                    again->adjustFadeLength(oldLength, start, true, false, false);
                }
                return true;
            };
            operation();
            PUSH_LAMBDA(operation, redo);
            PUSH_FRONT_LAMBDA(reverse, undo);
        }
    }
}

namespace {
bool shiftSubtitles(const std::shared_ptr<TimelineItemModel> &timeline, int startFrame, int endFrame, Fun &undo, Fun &redo)
{
    std::shared_ptr<SubtitleModel> model = timeline->getSubtitleModel();
    if (!model || model->isLocked()) {
        return true;
    }
    const double fps = pCore->getCurrentFps();
    if (!(fps > 0.0)) {
        return false;
    }
    const int delta = endFrame - startFrame;
    struct Item {
        int id;
        int layer;
        int start;
        int end;
        SubtitleEvent event;
        GenTime startTime;
    };
    QVector<Item> items;
    const auto all = model->getAllSubtitles();
    for (const auto &entry : all) {
        Item item;
        item.layer = entry.first.first;
        item.startTime = entry.first.second;
        item.id = model->getIdForStartPos(item.layer, item.startTime);
        item.start = item.startTime.frames(fps);
        item.end = entry.second.endTime().frames(fps);
        item.event = entry.second;
        if (item.id >= 0) {
            items.push_back(item);
        }
    }
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.start > b.start; });
    for (const Item &item : items) {
        if (item.end <= startFrame) {
            continue;
        }
        if (item.start >= endFrame) {
            const GenTime newPos(item.start - delta, fps);
            const int id = item.id;
            const int layer = item.layer;
            const GenTime oldPos = item.startTime;
            Fun operation = [model, id, layer, newPos]() { return model->moveSubtitle(id, layer, newPos, true, true); };
            Fun reverse = [model, id, layer, oldPos]() { return model->moveSubtitle(id, layer, oldPos, true, true); };
            if (!operation()) {
                return false;
            }
            PUSH_LAMBDA(operation, redo);
            PUSH_FRONT_LAMBDA(reverse, undo);
            continue;
        }
        if (item.start >= startFrame && item.end <= endFrame) {
            const int id = item.id;
            const auto start = std::make_pair(item.layer, item.startTime);
            const SubtitleEvent event = item.event;
            if (!model->removeSubtitle(id)) {
                return false;
            }
            Fun operation = [model, id]() { return model->removeSubtitle(id); };
            Fun reverse = [model, start, event]() {
                Fun unusedUndo = []() { return true; };
                Fun unusedRedo = []() { return true; };
                return model->addSubtitle(start, event, unusedUndo, unusedRedo, true);
            };
            PUSH_LAMBDA(operation, redo);
            PUSH_FRONT_LAMBDA(reverse, undo);
            continue;
        }
        if (item.start < startFrame && item.end > startFrame && item.end <= endFrame) {
            const int newSize = startFrame - item.start;
            if (!model->requestResize(item.id, newSize, true, undo, redo, false)) {
                return false;
            }
            continue;
        }
        if (item.start >= startFrame && item.start < endFrame && item.end > endFrame) {
            const int newSize = item.end - endFrame;
            if (!model->requestResize(item.id, newSize, false, undo, redo, false)) {
                return false;
            }
            const GenTime shifted(startFrame, fps);
            const int id = item.id;
            const int layer = item.layer;
            Fun operation = [model, id, layer, shifted]() { return model->moveSubtitle(id, layer, shifted, true, true); };
            Fun reverse = [model, id, layer, start = item.startTime]() { return model->moveSubtitle(id, layer, start, true, true); };
            if (!operation()) {
                return false;
            }
            PUSH_LAMBDA(operation, redo);
            PUSH_FRONT_LAMBDA(reverse, undo);
        }
    }
    return true;
}
} // namespace

int StoryTimelineBridge::primaryVideoTrack(const std::shared_ptr<TimelineItemModel> &timeline)
{
    int bestPos = std::numeric_limits<int>::max();
    int bestId = -1;
    if (!timeline) {
        return -1;
    }
    for (int trackId : timeline->getAllTracksIds()) {
        if (timeline->isAudioTrack(trackId) || timeline->isSubtitleTrack(trackId)) {
            continue;
        }
        const int pos = timeline->getTrackPosition(trackId);
        if (pos < bestPos) {
            bestPos = pos;
            bestId = trackId;
        }
    }
    return bestId;
}

QVector<int> StoryTimelineBridge::unlockedTracks(const std::shared_ptr<TimelineItemModel> &timeline)
{
    QVector<int> tracks;
    if (!timeline) {
        return tracks;
    }
    for (int trackId : timeline->getAllTracksIds()) {
        if (timeline->isSubtitleTrack(trackId)) {
            continue;
        }
        if (timeline->getTrackById_const(trackId)->isLocked()) {
            continue;
        }
        tracks.push_back(trackId);
    }
    return tracks;
}

QVector<StoryAnchor> StoryTimelineBridge::loadAnchors()
{
    if (!pCore->currentDoc()) {
        return {};
    }
    return anchorsFromJson(pCore->currentDoc()->getDocumentProperty(kAnchors));
}

QVector<StoryGap> StoryTimelineBridge::loadGaps()
{
    if (!pCore->currentDoc()) {
        return {};
    }
    return gapsFromJson(pCore->currentDoc()->getDocumentProperty(kGaps));
}

void StoryTimelineBridge::storeAnchors(const QVector<StoryAnchor> &anchors, Fun &undo, Fun &redo)
{
    rememberProperty(kAnchors, anchorsToJson(anchors), undo, redo);
}

void StoryTimelineBridge::storeGaps(const QVector<StoryGap> &gaps, Fun &undo, Fun &redo)
{
    rememberProperty(kGaps, gapsToJson(gaps), undo, redo);
}

QVector<StoryAnchor> StoryTimelineBridge::shiftAnchors(const QVector<StoryAnchor> &anchors, int startFrame, int endFrame, const QVector<int> &removedClipIds)
{
    QVector<StoryAnchor> shifted;
    const int delta = endFrame - startFrame;
    for (StoryAnchor anchor : anchors) {
        if (removedClipIds.contains(anchor.clipId) || removedClipIds.contains(anchor.primaryClipId)) {
            continue;
        }
        if (anchor.anchorFrame >= endFrame) {
            anchor.anchorFrame -= delta;
        } else if (anchor.anchorFrame >= startFrame) {
            anchor.anchorFrame = startFrame;
            anchor.needsReview = true;
        }
        shifted.push_back(anchor);
    }
    return shifted;
}

QVector<StoryGap> StoryTimelineBridge::shiftGaps(const QVector<StoryGap> &gaps, int startFrame, int endFrame)
{
    QVector<StoryGap> shifted;
    const int delta = endFrame - startFrame;
    for (StoryGap gap : gaps) {
        if (gap.endFrame <= startFrame) {
            shifted.push_back(gap);
            continue;
        }
        if (gap.startFrame >= endFrame) {
            gap.startFrame -= delta;
            gap.endFrame -= delta;
            shifted.push_back(gap);
            continue;
        }
        if (gap.startFrame >= startFrame && gap.endFrame <= endFrame) {
            continue;
        }
        if (gap.startFrame < startFrame) {
            gap.endFrame = startFrame;
        } else {
            const int remain = gap.endFrame - endFrame;
            gap.startFrame = startFrame;
            gap.endFrame = startFrame + remain;
        }
        if (gap.endFrame > gap.startFrame) {
            shifted.push_back(gap);
        }
    }
    return shifted;
}

bool StoryTimelineBridge::gapIsIntentional(int startFrame, int endFrame, const QVector<StoryGap> &gaps)
{
    for (const StoryGap &gap : gaps) {
        if (endFrame > gap.startFrame && startFrame < gap.endFrame) {
            return true;
        }
    }
    return false;
}

bool StoryTimelineBridge::rippleDeleteFrames(const std::shared_ptr<TimelineItemModel> &timeline, int startFrame, int endFrame, Fun &undo, Fun &redo,
                                            QString *error)
{
    if (!timeline || endFrame <= startFrame) {
        if (error) {
            *error = i18n("The transcript range is empty.");
        }
        return false;
    }
    const QVector<int> tracks = unlockedTracks(timeline);
    if (tracks.isEmpty()) {
        if (error) {
            *error = i18n("Every track is locked.");
        }
        return false;
    }
    QVector<int> removed;
    for (int trackId : tracks) {
        const std::unordered_set<int> items = timeline->getItemsInRange(trackId, startFrame, endFrame);
        for (int id : items) {
            if (timeline->isClip(id)) {
                const int pos = timeline->getClipPosition(id);
                const int out = pos + timeline->getClipPlaytime(id);
                if (pos >= startFrame && out <= endFrame) {
                    removed.push_back(id);
                }
            }
        }
    }
    if (!TimelineFunctions::extractZoneWithUndo(timeline, tracks, QPoint(startFrame, endFrame), false, -1, {}, undo, redo)) {
        if (error) {
            *error = i18n("The timeline could not ripple-delete that range.");
        }
        return false;
    }
    if (!shiftSubtitles(timeline, startFrame, endFrame, undo, redo)) {
        if (error) {
            *error = i18n("Subtitles could not follow the dialogue cut.");
        }
        return false;
    }
    const QVector<StoryAnchor> anchors = shiftAnchors(loadAnchors(), startFrame, endFrame, removed);
    storeAnchors(anchors, undo, redo);
    const QVector<StoryGap> gaps = shiftGaps(loadGaps(), startFrame, endFrame);
    storeGaps(gaps, undo, redo);
    const int fadeFrames = std::max(0, KdenliveSettings::storyFadeFrames());
    fadeJoin(timeline, startFrame, fadeFrames, undo, redo);
    return true;
}

bool StoryTimelineBridge::insertIntentionalGap(const std::shared_ptr<TimelineItemModel> &timeline, int frame, int durationFrames, Fun &undo, Fun &redo,
                                              QString *error)
{
    if (!timeline || durationFrames <= 0) {
        if (error) {
            *error = i18n("The gap length is empty.");
        }
        return false;
    }
    const QVector<int> tracks = unlockedTracks(timeline);
    if (!TimelineFunctions::requestInsertSpace(timeline, QPoint(frame, frame + durationFrames), undo, redo, tracks)) {
        if (error) {
            *error = i18n("The gap could not be inserted.");
        }
        return false;
    }
    QVector<StoryGap> gaps = loadGaps();
    gaps.push_back({frame, frame + durationFrames});
    storeGaps(gaps, undo, redo);
    return true;
}

bool StoryTimelineBridge::closeUnintendedGaps(const std::shared_ptr<TimelineItemModel> &timeline, Fun &undo, Fun &redo, QString *error)
{
    if (!timeline) {
        return false;
    }
    const int primary = primaryVideoTrack(timeline);
    if (primary < 0) {
        if (error) {
            *error = i18n("There is no primary video track.");
        }
        return false;
    }
    QVector<StoryGap> intentional = loadGaps();
    bool closed = false;
    int guard = 0;
    int cursor = 0;
    while (guard++ < 1000) {
        const int blank = timeline->getTrackById_const(primary)->getNextBlankStart(cursor);
        if (blank < 0) {
            break;
        }
        const int blankEnd = timeline->getTrackById_const(primary)->getBlankEnd(blank);
        if (blankEnd < blank) {
            break;
        }
        const std::pair<int, int> spacer = TimelineFunctions::requestSpacerStartOperation(timeline, primary, blank);
        if (spacer.first < 0) {
            break;
        }
        const int start = timeline->getItemPosition(spacer.first);
        const int spaceStart = start - spacer.second;
        if (spaceStart >= start) {
            break;
        }
        int keep = 0;
        for (const StoryGap &gap : intentional) {
            const int overlapStart = std::max(gap.startFrame, spaceStart);
            const int overlapEnd = std::min(gap.endFrame, start);
            if (overlapEnd > overlapStart) {
                keep += overlapEnd - overlapStart;
            }
        }
        const int destination = spaceStart + keep;
        if (destination >= start) {
            cursor = blankEnd + 1;
            continue;
        }
        if (!TimelineFunctions::requestSpacerEndOperation(timeline, spacer.first, start, destination, primary, -1, undo, redo, false)) {
            if (error) {
                *error = i18n("A gap could not be closed.");
            }
            return false;
        }
        const int removedLength = start - destination;
        QVector<StoryGap> updated = loadGaps();
        bool movedGap = false;
        for (StoryGap &gap : updated) {
            if (gap.endFrame <= spaceStart) {
                continue;
            }
            if (gap.startFrame >= start) {
                gap.startFrame -= removedLength;
                gap.endFrame -= removedLength;
                movedGap = true;
                continue;
            }
            const int overlapStart = std::max(gap.startFrame, spaceStart);
            const int overlapEnd = std::min(gap.endFrame, start);
            if (overlapEnd > overlapStart) {
                const int kept = overlapEnd - overlapStart;
                gap.startFrame = spaceStart;
                gap.endFrame = spaceStart + kept;
                movedGap = true;
            }
        }
        if (movedGap) {
            storeGaps(updated, undo, redo);
            intentional = loadGaps();
        }
        closed = true;
        cursor = 0;
    }
    if (!closed && error) {
        *error = i18n("No unintended gap was found on the primary storyline.");
    }
    return closed;
}

QVector<QPair<int, int>> StoryTimelineBridge::spansForBinRange(const std::shared_ptr<TimelineItemModel> &timeline, const QString &binId, int sourceStartFrame,
                                                              int sourceEndFrame, QStringList *skipped)
{
    QVector<QPair<int, int>> spans;
    if (!timeline || binId.isEmpty() || sourceEndFrame <= sourceStartFrame) {
        return spans;
    }
    const int duration = timeline->duration();
    for (int trackId : timeline->getAllTracksIds()) {
        if (timeline->isSubtitleTrack(trackId)) {
            continue;
        }
        const std::unordered_set<int> items = timeline->getItemsInRange(trackId, 0, std::max(0, duration));
        for (int clipId : items) {
            if (!timeline->isClip(clipId) || timeline->getClipBinId(clipId) != binId) {
                continue;
            }
            std::shared_ptr<ClipModel> clip = timeline->getClipPtr(clipId);
            const double speed = clip ? clip->getSpeed() : 1.0;
            if (speed > 0.0 && std::abs(speed - 1.0) > 0.001) {
                if (skipped) {
                    skipped->push_back(i18n("Clip %1 has a speed change, so transcript times were not mapped onto it.", clipId));
                }
                continue;
            }
            const int inPoint = timeline->getClipIn(clipId);
            const int playtime = timeline->getClipPlaytime(clipId);
            const int visibleEnd = inPoint + playtime;
            const int overlapStart = std::max(sourceStartFrame, inPoint);
            const int overlapEnd = std::min(sourceEndFrame, visibleEnd);
            if (overlapEnd <= overlapStart) {
                continue;
            }
            const int position = timeline->getClipPosition(clipId);
            spans.push_back({position + (overlapStart - inPoint), position + (overlapEnd - inPoint)});
        }
    }
    std::sort(spans.begin(), spans.end(), [](const QPair<int, int> &a, const QPair<int, int> &b) { return a.first > b.first; });
    return spans;
}

bool StoryTimelineBridge::nudgeSelection(const std::shared_ptr<TimelineItemModel> &timeline, int deltaFrames)
{
    if (!timeline || deltaFrames == 0) {
        return false;
    }
    const std::unordered_set<int> selection = timeline->getCurrentSelection();
    if (selection.empty()) {
        return false;
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    QSet<int> leaders;
    bool moved = false;
    for (int id : selection) {
        if (!timeline->isClip(id)) {
            continue;
        }
        int leader = id;
        if (timeline->isInGroup(id)) {
            const std::unordered_set<int> group = timeline->getGroupElements(id);
            if (!group.empty()) {
                leader = *std::min_element(group.begin(), group.end());
            }
        }
        if (leaders.contains(leader)) {
            continue;
        }
        leaders.insert(leader);
        if (!timeline->isClip(leader)) {
            continue;
        }
        const int trackId = timeline->getItemTrackId(leader);
        const int position = timeline->getItemPosition(leader);
        const auto result = timeline->requestClipMove(leader, trackId, std::max(0, position + deltaFrames), true, true, true, true, undo, redo);
        if (result == TimelineModel::MoveSuccess) {
            moved = true;
        }
    }
    if (!moved) {
        undo();
        return false;
    }
    pCore->pushUndo(undo, redo, i18n("Nudge clip"));
    return true;
}

bool StoryTimelineBridge::selectClipRangeAtPlayhead(const std::shared_ptr<TimelineItemModel> &timeline)
{
    if (!timeline) {
        return false;
    }
    const int frame = pCore->getMonitorPosition();
    const int primary = primaryVideoTrack(timeline);
    int clipId = primary >= 0 ? timeline->getClipByPosition(primary, frame) : -1;
    if (clipId < 0) {
        for (int trackId : unlockedTracks(timeline)) {
            clipId = timeline->getClipByPosition(trackId, frame);
            if (clipId >= 0) {
                break;
            }
        }
    }
    if (clipId < 0) {
        return false;
    }
    const int position = timeline->getClipPosition(clipId);
    const int end = position + timeline->getClipPlaytime(clipId);
    timeline->requestSetSelection({clipId});
    if (pCore->window() && pCore->window()->getCurrentTimeline()) {
        pCore->window()->getCurrentTimeline()->controller()->setZone(QPoint(position, end), true);
    }
    return true;
}

bool StoryTimelineBridge::connectSelection(const std::shared_ptr<TimelineItemModel> &timeline, QString *error)
{
    if (!timeline) {
        return false;
    }
    const int primaryTrack = primaryVideoTrack(timeline);
    const int frame = pCore->getMonitorPosition();
    const int primaryClip = primaryTrack >= 0 ? timeline->getClipByPosition(primaryTrack, frame) : -1;
    if (primaryClip < 0) {
        if (error) {
            *error = i18n("No primary storyline clip is under the playhead.");
        }
        return false;
    }
    const int primaryPos = timeline->getClipPosition(primaryClip);
    QVector<StoryAnchor> anchors = loadAnchors();
    bool added = false;
    for (int id : timeline->getCurrentSelection()) {
        if (!timeline->isClip(id) || id == primaryClip || timeline->getItemTrackId(id) == primaryTrack) {
            continue;
        }
        StoryAnchor anchor;
        anchor.clipId = id;
        anchor.primaryClipId = primaryClip;
        anchor.anchorFrame = frame;
        anchor.offsetFrames = timeline->getClipPosition(id) - primaryPos;
        anchors.push_back(anchor);
        added = true;
    }
    if (!added) {
        if (error) {
            *error = i18n("Select a clip on another track to connect it to the primary storyline.");
        }
        return false;
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    storeAnchors(anchors, undo, redo);
    pCore->pushUndo(undo, redo, i18n("Connect clip"));
    return true;
}

bool StoryTimelineBridge::applySourceCuts(const std::shared_ptr<TimelineItemModel> &timeline, const QString &binId,
                                          const QVector<QPair<double, double>> &sourceRanges, Fun &undo, Fun &redo, QString *error)
{
    if (!timeline || binId.isEmpty()) {
        if (error) {
            *error = i18n("There is no timeline clip to shorten.");
        }
        return false;
    }
    const double fps = pCore->getCurrentFps();
    struct Span {
        int start;
        int end;
    };
    QVector<Span> spans;
    QStringList skipped;
    for (const QPair<double, double> &range : sourceRanges) {
        if (!(range.second > range.first)) {
            continue;
        }
        const int sourceStart = StoryTime::secondsToFrame(range.first, fps);
        const int sourceEnd = StoryTime::secondsToFrame(range.second, fps);
        const QVector<QPair<int, int>> pieces = spansForBinRange(timeline, binId, sourceStart, sourceEnd, &skipped);
        for (const QPair<int, int> &piece : pieces) {
            if (piece.second > piece.first) {
                spans.push_back({piece.first, piece.second});
            }
        }
    }
    if (!skipped.isEmpty() && error) {
        *error = skipped.join(QLatin1Char('\n'));
    }
    if (spans.isEmpty()) {
        if (error && error->isEmpty()) {
            *error = i18n("No timeline media overlapped the silence.");
        }
        return false;
    }
    std::sort(spans.begin(), spans.end(), [](const Span &a, const Span &b) { return a.start > b.start; });
    for (const Span &span : spans) {
        QString spanError;
        if (!rippleDeleteFrames(timeline, span.start, span.end, undo, redo, &spanError)) {
            if (error) {
                *error = spanError;
            }
            return false;
        }
    }
    return true;
}

double StoryTimelineBridge::sourceSecondsForTimelineFrame(const std::shared_ptr<TimelineItemModel> &timeline, const QString &binId, int frame, double fps)
{
    if (!timeline || !(fps > 0.0) || binId.isEmpty()) {
        return -1.0;
    }
    for (int trackId : timeline->getAllTracksIds()) {
        if (timeline->isSubtitleTrack(trackId)) {
            continue;
        }
        const int clipId = timeline->getClipByPosition(trackId, frame);
        if (clipId < 0 || !timeline->isClip(clipId) || timeline->getClipBinId(clipId) != binId) {
            continue;
        }
        const int position = timeline->getClipPosition(clipId);
        const int playtime = timeline->getClipPlaytime(clipId);
        if (frame < position || frame >= position + playtime) {
            continue;
        }
        const int sourceFrame = timeline->getClipIn(clipId) + (frame - position);
        return StoryTime::frameToSeconds(sourceFrame, fps);
    }
    return -1.0;
}
