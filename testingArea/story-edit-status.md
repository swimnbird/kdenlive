<!--
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: CC0-1.0
-->

# Story editing status

Baseline commit before this work: `4a4d449d97dae6dfdaa50e792e4c6b979d9fa2cf`.

## Environment

The host is Ubuntu 24.04. Its packages cannot configure this tree:

- Qt 6.4.2 (required 6.10.0)
- MLT 7.22.0 (required 7.42.0)
- extra-cmake-modules 5.115 (KF6 6.21 needs ECM 6)
- no KF6 or KDDockWidgets development packages

The build that ran used a debootstrap of Ubuntu 27.04 (tenacious) at `/opt/tenacious`:

- Qt 6.11.2
- KDE Frameworks 6.30.0
- MLT 7.42.0
- KDDockWidgets 2.4.1
- OpenTimelineIO 0.18.1

The unmodified tree was not configured or run. The first successful CMake configure was this branch.

## What was executed

- `ninja storyedittest kdenlive` in `/workspace/build-story`
- `QT_QPA_PLATFORM=offscreen ./bin/storyedittest "[StoryEdit]"` — 8 cases, 77 assertions, exit 0
- `QT_QPA_PLATFORM=offscreen ./bin/spacertest` — 217 assertions, exit 0
- `QT_QPA_PLATFORM=offscreen ./bin/kdenlive --version` — `kdenlive 26.11.70`, exit 0
- `python3 -m unittest tests/python/test_wordtiming.py` — 3 tests, exit 0
- Host `ffmpeg` `silencedetect` on synthetic sine-plus-silence audio printed `silence_start` / `silence_end`

No Whisper model was loaded. No project was rendered. The GUI was not clicked through.

## Not claimed as working

- A full Final Cut interaction set. Mapped keys are cited in `src/transcript/shortcutpreset.cpp`. Z steps the existing zoom command. There is no separate zoom tool.
- Audio-only skimming. S toggles hover-seek.
- Pause review in the speech editor blocks on ffmpeg and offers the shorten plan. Remove mode is covered by `SilenceAnalyzer` tests.
- Magnetic editing does not rewrite every existing trim. It ripples unlocked tracks, records intentional gaps, and can close other gaps on the primary video track.
- Speed-changed clips are skipped by transcript time mapping.
- Acceptance import, transcribe, export, and rendered sync were not run.
