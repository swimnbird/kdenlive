<!--
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: CC0-1.0
-->

# Story editing status

Result: **incomplete**. The official AT-SPI driver now starts and can see the real `kdenlive` window. Import of the speech fixture reached the clip monitor. The Transcribe button stayed disabled, and a preview-window error blocked the rest of the click path.

## Snap model

Closing a blank that is kept as an intentional gap called `requestSpacerStartOperation`, which adds a temporary snap, then returned without `requestSpacerEndOperation`. That left a snap at frame 120, the old clip end. `TimelineFunctions::abortSpacerOperation` drops that snap. `SnapModel::removePoint` no longer inserts a point when the position is already gone.

```
QT_QPA_PLATFORM=offscreen /workspace/build-story/bin/storyslicetest "[StorySync]"
```

209 assertions, exit 0. The snap model matches clip edges and subtitle times after the cut, after undo, and after redo. Log: `testingArea/sync-evidence/storysync.log`. Summary: `testingArea/sync-evidence/sync-report.txt`.

## AT-SPI driver

The driver was built from the public repository `https://invent.kde.org/sdk/selenium-webdriver-at-spi` inside the Ubuntu 27.04 chroot, using the packages named by that project's `CMakeLists.txt` and the KDE Selenium wiki (KF6 equivalents of the wiki's KF5 list). Build log: `/opt/cursor/artifacts/atspi-build.log`. Python requirements from `requirements.txt` installed. Log: `/opt/cursor/artifacts/atspi-pip.log`.

The first run failed before the server listened:

```
ValueError: Namespace Gdk not available
```

`gir1.2-gtk-3.0` supplies `Gdk` 3.0 and `Gtk` 3.0, which `selenium-webdriver-at-spi.py` requires. After that package, `selenium-webdriver-at-spi-run` reached the test. The runner was started with `TEST_WITH_KWIN_WAYLAND=0` because this display is X11 `:1`, which the runner documents as the switch that skips its nested `kwin_wayland`.

The test is `testingArea/story_atspi_test.py`. It launches `/workspace/build-story/bin/kdenlive --no-welcome` plus `marked.wav`. One run logged:

```
createClipFromFile "/workspace/testingArea/sync-evidence/marked.wav"
```

The accessibility tree contained `marked.wav` and no longer showed "Please install speech recognition models" once `HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, and `XDG_CACHE_HOME` were passed in the `appium:environ` capability. The tree is `/opt/cursor/artifacts/atspi-tree-final.xml`. Screenshot: `/opt/cursor/artifacts/atspi-kdenlive.png`.

That screenshot shows the waveform in the clip monitor, an empty timeline, a grey Transcribe button, and this dialog:

```
Could not create the video preview window.
There is something wrong with your Kdenlive install or your driver settings, please fix it.
```

The Transcribe button's accessible states were `focusable, showing, visible` and did not include `enabled`. Clicking it did not print a speech-job start line. No transcript text appeared. Selection-to-sequence, cleanup, undo, save, and render were not reached.

## Whisper venv

The app looks for `~/.local/share/kdenlive/venv/bin/python3`. Creating it failed until `python3.14-venv` was installed. After that, the chroot venv imports whisper (`WHISPER_OK`). Log: `/opt/cursor/artifacts/venv-whisper.log`. The model file is `testingArea/gui-home/.cache/whisper/tiny.pt`. The in-app job did not start.

## Still unverified

- Transcript typing versus ripple deletion
- Filler and pause review, including duplicate-before-batch
- Selection to a new sequence
- Undo, redo, save, and reopen from the window
- File → Render or `kdenlive --render`

## Commands for the driver

```
sudo chroot /opt/tenacious
apt-get install cmake extra-cmake-modules g++ git pkg-config \
  qt6-wayland-dev qt6-base-private-dev qt6-wayland-private-dev \
  libkf6windowsystem-dev libkf6coreaddons-dev libkpipewire-dev \
  libwayland-dev plasma-wayland-protocols kwayland-dev libxkbcommon-dev \
  at-spi2-core gir1.2-atspi-2.0 python3-pyatspi python3-gi python3-lxml \
  python3-pip gobject-introspection gir1.2-gtk-3.0 ruby dbus-x11
git clone --depth 1 https://invent.kde.org/sdk/selenium-webdriver-at-spi.git
cmake -S selenium-webdriver-at-spi -B selenium-webdriver-at-spi/build
cmake --build selenium-webdriver-at-spi/build -j2
cmake --install selenium-webdriver-at-spi/build
python3 -m pip install --break-system-packages -r /usr/share/selenium-webdriver-at-spi/requirements.txt
```

Then, with the prepared home and display `:1`:

```
TEST_WITH_KWIN_WAYLAND=0 TEST_WITH_VIDEO_RECORDER=0 TEST_WITH_CLEAN_HOME=0 \
  selenium-webdriver-at-spi-run python3 /workspace/testingArea/story_atspi_test.py
```

No package was built.
