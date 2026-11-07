<!--
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: CC0-1.0
-->

# Story editing status

Result: **incomplete**. The snap leak is fixed and the rendered marker check still passes. The speech-editor button, import, typing, cleanup dialog, save, and export were not clicked.

## Snap model

Closing a blank that is kept as an intentional gap called `requestSpacerStartOperation`, which adds a temporary snap, then returned without `requestSpacerEndOperation`. That left a snap at frame 120, the old clip end. `TimelineFunctions::abortSpacerOperation` drops that snap. `SnapModel::removePoint` no longer inserts a point when the position is already gone.

```
QT_QPA_PLATFORM=offscreen /workspace/build-story/bin/storyslicetest "[StorySync]"
```

209 assertions, exit 0. The snap model matches clip edges and subtitle times after the cut, after undo, and after redo. Log: `testingArea/sync-evidence/storysync.log`. Summary: `testingArea/sync-evidence/sync-report.txt`.

## Why the window was not driven

Three launches only mapped the window and took screenshots. They did not send widget events.

- CMake reports `SeleniumWebDriverATSPI` was not found. The log says it is needed for GUI tests. No AT-SPI driver is installed, so buttons cannot be addressed by name.
- A Qt harness, `tests/storyguiharness`, called `Core::build` and `initGUI` on the real window path. It segfaulted before any widget existed:

```
QQmlApplicationEngine failed to load component
module "org.kde.kdenlive" plugin "kdenliveLibplugin" not found
```

The QML plugin is registered with the `kdenlive` executable, not with a second binary. The harness was removed so it is not left as a broken target. Log: `testingArea/sync-evidence/gui-harness.log`.

Catch tests call `Core::build(..., testMode=true)`, which never constructs `MainWindow` or `Bin`, so `pCore->bin()` is null. There is no test setter for the window.

## Whisper venv

The app looks for `~/.local/share/kdenlive/venv/bin/python3`. Creating it failed until `python3.14-venv` was installed (`ensurepip is not available`). After that package:

```
/workspace/testingArea/gui-home/.local/share/kdenlive/venv/bin/python3 -c "import whisper"
```

prints `WHISPER_OK` inside the chroot. Log: `testingArea/sync-evidence/venv-whisper.log`. The model file is `testingArea/gui-home/.cache/whisper/tiny.pt`. The Speech Editor start button was not clicked, so this is not the in-app job.

## Manual steps still required

1. `HOME=/workspace/testingArea/gui-home XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share DISPLAY=:1 /workspace/build-story/bin/kdenlive --no-welcome`
2. Import `testingArea/sync-evidence/marked.wav` into the bin and select it.
3. Open the Speech Editor tab and press the start button. The venv python and tiny model are already on disk.
4. Type a correction and confirm the timeline does not move. Delete a spoken word and confirm a ripple.
5. Run pause review and filler review. Leave “like” unchecked. Confirm “Duplicate the sequence before applying” is checked, apply, and confirm the original sequence is unchanged.
6. Create a sequence from a selection. Undo, redo, save, reopen.
7. Use File → Render (or `kdenlive --render project.kdenlive output.mkv`) and repeat the barcode and beep check.

No package was built.
