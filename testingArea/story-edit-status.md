<!--
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: CC0-1.0
-->

# Story editing status

Baseline commit: `4a4d449d97dae6dfdaa50e792e4c6b979d9fa2cf`, built at `/src-baseline`. This branch is built at `/workspace/build-story` inside the Ubuntu 27.04 chroot.

## Commands that passed

```
QT_QPA_PLATFORM=offscreen /workspace/build-story/bin/storyslicetest "[StorySync]"
# 204 assertions, exit 0. Report: testingArea/sync-evidence/sync-report.txt
# compared 68 rendered frames, 0 barcode mismatches, 2 beep peaks

QT_QPA_PLATFORM=offscreen /workspace/build-story/bin/storyedittest "*preset*"
# 73 assertions, exit 0

python3 /workspace/data/scripts/whisper/whispertotext.py \
  -S /workspace/testingArea/sync-evidence/marked.wav \
  -M tiny -D cpu -L en --words_json /tmp/whisper-words.json
# exit 0. Words: Um 0.00–0.64, hello 0.74–0.96, Like 2.44–2.96
```

`storyslicetest "[StorySync]"` builds 1280×720 video at 30000/1001 with a per-frame barcode, and 48 kHz audio with 2 kHz beeps plus espeak-ng “um”, “hello”, and “like”. It loads both into a real timeline, connects a B-roll clip, keeps a 15-frame intentional gap while closing the blank in front of it, approves the “um” filler and not “like”, ripple-cuts those source ranges, undoes and redoes, saves, and renders with melt (H.264, PCM). Decoded barcodes matched the timeline’s source frame within 1 frame on 68 frames. Beep peaks were 5 and 106 samples from the timeline times (one frame is about 1602 samples). Source file size and mtime were unchanged. A stale snap remains at the original clip end (frame 120); it did not move the rendered markers.

`*preset*` applies the story keys on a real action collection (R range, W insert, E append, Q connect, A select) and checks copy, paste, undo, arrows, and Backspace are not taken. It then calls `selectClipRangeAtPlayhead`, `connectSelection`, and `TimelineFunctions::insertZone` (the append path) on a timeline.

## GUI

```
HOME=/workspace/testingArea/gui-home \
XDG_CONFIG_HOME=$HOME/.config \
DISPLAY=:1 QT_QPA_PLATFORM=xcb \
QSG_RHI_BACKEND=software QT_QUICK_BACKEND=software LIBGL_ALWAYS_SOFTWARE=1 \
/workspace/build-story/bin/kdenlive --no-welcome
```

The config skips the welcome wizard. The window “Untitled / HD 1080p 25 fps — Kdenlive” opened with the project bin, timeline (V2, V1, A1, A2), and a Speech Editor tab. Startup logged `Assigned 29 shortcuts` and no missing actions.

An earlier build blocked on KXmlGui “Ambiguous Shortcuts” because the typing alternates used Ctrl+O (Open), Ctrl+I (Insert Zone), and Ctrl+Space (Play Zone), and Shift+Delete was on both Cut and Extract. Those story alternates were removed and the extra Cut shortcut is cleared before `setupGUI`. Pause-while-typing is Ctrl+Shift+K.

Not done in the window: import, typing a correction, clicking delete, the in-app Whisper venv, the cleanup dialog, save, or export. The Speech Editor tab was visible and not operated. Kdenlive’s Whisper job uses its own Python venv; that venv was not created. The script above is the one the speech editor launches.

`duplicateSequenceByBinId` needs a Bin widget. Headless tests have `pCore->bin() == nullptr`, so the clone was not executed. `StoryCleanup::duplicateSequenceByDefault()` is true and the checkbox uses it.

No package was built.
