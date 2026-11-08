# SPDX-FileCopyrightText: 2026 Kdenlive contributors
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
"""Word-timing helpers shared with the C++ transcript model.

These functions do not import torch or whisper. They only describe the
JSON the editor reads. Running them does not prove that Whisper itself ran.
"""

import math


def seconds_to_frame(seconds, fps):
    """Half-away-from-zero, matching StoryTime::secondsToFrame."""
    if fps is None or fps <= 0:
        return 0
    value = float(seconds) * float(fps)
    if value >= 0:
        return int(math.floor(value + 0.5))
    return int(math.ceil(value - 0.5))


def frame_to_seconds(frame, fps):
    if fps is None or fps <= 0:
        return 0.0
    return float(frame) / float(fps)


def words_from_whisper_result(result, offset_sec=0.0):
    """Convert a Whisper result dict into the editor's word records.

    Expected shape is the object returned by whisper's transcribe():
    segments[].words[] with start, end, and word. Missing word lists are skipped.
    """
    words = []
    next_id = 1
    if not isinstance(result, dict):
        return words
    for segment in result.get("segments") or []:
        for entry in segment.get("words") or []:
            try:
                start = float(entry["start"]) + float(offset_sec)
                end = float(entry["end"]) + float(offset_sec)
            except (KeyError, TypeError, ValueError):
                continue
            text = entry.get("word") or ""
            if not text or not end > start:
                continue
            words.append(
                {
                    "id": next_id,
                    "spoken": text,
                    "display": text,
                    "start": start,
                    "end": end,
                    "needsRetranscribe": False,
                    "excluded": False,
                }
            )
            next_id += 1
    return words


def whisper_stdout(words):
    """Serialize words in the same block format whispertotext.py prints."""
    lines = []
    if not words:
        return ""
    lines.append("[%s>%s]" % (words[0]["start"], words[-1]["end"]))
    for word in words:
        lines.append("[%s>%s]%s" % (word["start"], word["end"], word["spoken"]))
    lines.append("")
    return "\n".join(lines)
