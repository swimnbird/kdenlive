<!--
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: CC0-1.0
-->

# Story editing status

Baseline commit before this work: `4a4d449d97dae6dfdaa50e792e4c6b979d9fa2cf`.

## Environment

The host is Ubuntu 24.04 and cannot configure this tree (Qt 6.4.2, MLT 7.22.0, ECM 5.115, no KF6). Builds and tests ran in a debootstrap of Ubuntu 27.04 (tenacious) at `/opt/tenacious`: Qt 6.11.2, KDE Frameworks 6.30.0, MLT 7.42.0, KDDockWidgets 2.4.1, OpenTimelineIO 0.18.1.

The unmodified commit was configured and built in `/src-baseline`. The story branch was built in `/workspace/build-story`.

## What was executed

Unmodified `4a4d449d97` binaries under `/src-baseline/build/bin`, `QT_QPA_PLATFORM=offscreen`:

| Command | Result |
| --- | --- |
| `kdenlive --version` | `kdenlive 26.11.70`, exit 0 |
| `spacertest` | 217 assertions, 1 case, exit 0 |
| `documenttest` | 10 assertions, 2 cases, exit 0 |
| `trimmingtest` | 2539 assertions, 6 cases, exit 0 |
| `subtitlestest` | 72 assertions, 1 case, exit 0 |

Modified branch:

| Command | Result |
| --- | --- |
| `storyedittest` | 87 assertions, 9 cases, exit 0 |
| `storyslicetest "[StorySlice]"` | 48 assertions, 1 case, exit 0 |
| `subtitlestest` (after the subtitle snap fix) | 72 assertions, 1 case, exit 0 |
| `kdenlive` on display `:1` with software Qt Quick | Process opened the Welcome to Kdenlive Quick Setup dialog (476×504). The main timeline was not clicked through. |

`storyslicetest` builds a 2 second silent 320×240 H.264 file and a separate WAV (tone, silence, tone) with ffmpeg. It imports both, places a color B-roll and a subtitle after the pause, shortens the detected silence through `StoryTimelineBridge::applySourceCuts`, and checks:

- the video and the separate audio are each split and the removed range is the same length
- the B-roll and the subtitle move earlier by that many frames
- surviving transcript words keep their source times
- undo restores the B-roll and subtitle, redo applies the cut again
- the project saves, an `Mlt::Producer` reopens it, and `melt` plus `ffprobe` match the timeline duration within 0.2 s
- the source file size and mtime are unchanged

That is a generated fixture, not a recorded podcast and not a GUI session.

Whisper `20250625` with the tiny model ran on the host, on CPU, against an espeak-ng WAV of “hello again”. The model returned word times and the text “But go again.” No media was uploaded. Kdenlive’s speech-editor job was not pointed at this model.

## Still not a full acceptance pass

- The welcome dialog was not driven into a project, so bin import, the speech editor, and export were not clicked.
- Pause shortening in the speech editor is asynchronous (`SilenceAnalysisJob`) and applies through the same undoable cut as the fixture. The dialog itself was not shown.
- “Create sequence from selection” inserts the timed passage at frame 0 of a new sequence (video-only and audio-only copies). That path was not exercised in a window.
- Connected-anchor math is unit-tested. A connected clip was not anchored and then ripple-cut in `storyslicetest`.
- Magnetic mode still only ripples unlocked tracks for these story operations. Trim, slip, and spacer tools are unchanged. “Close unintended gaps” walks the primary video track.
- Speed-changed clips are skipped by transcript time mapping.
- There is no separate zoom tool. Z steps the existing zoom-in command.
- No modified package was built.
