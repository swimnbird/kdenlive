<!--
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: CC0-1.0
-->

# Story editing status

Result: **incomplete**. The editor now has a selectable Story appearance and Story layout. That does not close the speech-editor acceptance. The Transcribe button stayed disabled, and the video-preview error is still unresolved.

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

No Linux release package was built. A native macOS package was not produced. See the packaging section.

## Story appearance

References, used for layout and hierarchy only. No Apple artwork, icons, or product name is in the interface.

- [Final Cut Pro for Mac interface](https://support.apple.com/guide/final-cut-pro/final-cut-pro-interface-ver92bd100a/mac): libraries and browser on the left, viewer for playback, magnetic timeline along the bottom.
- [Arrange the main window](https://support.apple.com/guide/final-cut-pro/arrange-the-main-window-ver2a27194eb/mac): browser, viewer, and inspector can be shown together; the inspector can use the full height.
- [Change the workspace layout](https://support.apple.com/en-ae/guide/final-cut-pro/ver6b3626147/mac): workspaces are selectable and do not replace the editor.

What changed:

- `data/color-schemes/StoryCharcoal.colors` is a KDE color scheme. Window background is 32,32,34 and window text is 230,230,232. Selection is 70,108,148 with text 245,246,247. It is installed as a normal color scheme and chosen with `KColorSchemeManager`.
- Settings → Story editing → Story appearance toggles it and sets the main and timeline toolbars to 16 px icons. The usual theme menu still selects other schemes.
- On a profile with no saved dock layout and no saved color scheme, the first run selects Story appearance. A saved layout or a saved scheme is left alone.
- `data/layouts/story.json` places the project bin on the upper left, with the speech editor, media browser, and effects as tabs. The project monitor is the center viewer, with the clip monitor as a tab. The effect stack fills the right side, with clip properties and the audio mixer as tabs. The timeline fills the lower left, with subtitles as a tab. Editing, Audio, Effects, and Color layouts are unchanged.
- On macOS only, the bundle name is Kdenlive Story and the identifier is `org.kde.kdenlive.story`. The `.kdenlive` association rank is Alternate, so this build does not claim the existing app's documents.

Screenshots of `/workspace/build-story/bin/kdenlive --no-welcome` on display `:1`, fresh config, Story Charcoal saved as `ColorScheme`:

- Before, lighter default docks: `/opt/cursor/artifacts/kdenlive-main-window.png`
- 1280×800: `/opt/cursor/artifacts/story-appearance-1280.png`
- 1680×1000: `/opt/cursor/artifacts/story-appearance-1680.png`
- 1920×1040: `/opt/cursor/artifacts/story-appearance-1920.png`

At 1280 the long inspector titles were clipped. With Story appearance on, those docks use the shorter titles Effects, Clip, and Mixer. The full names stay on the tooltips, and the dock object names are unchanged. Turning the appearance off restores Effect/Composition Stack, Clip Properties, and Audio Mixer. Screenshots of that toggle, still with an empty project and no clip playing:

- Story appearance, 1280×800: `/opt/cursor/artifacts/story-tabs-1280.png`
- After Settings → Story appearance is unchecked, same layout, light theme, status text "Standard appearance is on.": `/opt/cursor/artifacts/story-tabs-1280-standard.png`

Quit wrote `storyAppearance=standard` and kept `kdockLayout` with `project_bin` and `effect_stack`. The empty viewer is grey. Playback was not exercised.

## macOS runner check

`swimnbird/kdenlive` is public. There was no `.github/workflows` file. A standard `macos-15` runner is arm64, 3 CPUs, 7 GB RAM, and 14 GB SSD, and it is free on a public repository. Larger runners are billed even for public repositories and are not used.

KDE's own macOS package job is not a GitHub runner. `.gitlab-ci.yml` includes `sysadmin/craft-ci` `macos-arm64-qt6.yml`, which runs on one self-hosted GitLab machine tagged `macOS`, with `KDECI_CRAFT_CACHE=/mnt/craft-cache/` and signing enabled. The public cache index is `https://files.kde.org/craft/Qt6/26.10/macos/clang/arm64/`. That cache is the dependency set, not a finished package of this fork. A 14 GB standard runner does not have room to unpack Qt, Frameworks, MLT, and FFmpeg and then link Kdenlive.

A manually dispatched workflow is on this branch: `.github/workflows/macos-arm64-package.yml`. Its first step records `df` and RAM and stops before Craft when free disk is under 18 GiB or RAM is under 10 GiB. It does not call ci-notary and it sets `CodeSigning/Enabled=False`. No `.dmg` was produced.

`QT_QPA_PLATFORM=offscreen /workspace/build-story/bin/storyedittest "*preset*"` — 73 assertions, exit 0.

## macOS package, not built

This cloud environment is Linux x86_64. It cannot produce an arm64 Mach-O `.app` or `.dmg`. There is no connected Mac worker, and none was started. The Linux `kdenlive` binary must not be copied onto the Mac.

Official route, from `dev-docs/build.md`, `dev-docs/packaging.md`, and `.gitlab-ci.yml`:

- KDE Craft on macOS: `craft --package kdenlive`, output under `CraftRoot/tmp`. Setup: https://community.kde.org/Craft
- Blueprint: https://invent.kde.org/packaging/craft-blueprints-kde/-/blob/master/kde/kdemultimedia/kdenlive/kdenlive.py
- GitLab CI job `craft_macos_qt6_arm64` from `sysadmin/craft-ci` `gitlab-templates/macos-arm64-qt6.yml`, platform `macos-arm-clang`. That job runs on KDE's GitLab, then calls `ci-notary-service/publishbuild.py`. Do not trigger that publish step. This GitHub fork has no macOS workflow.

The stock blueprint clones `invent.kde.org`. A package of this fork has to point Craft at `https://github.com/swimnbird/kdenlive` branch `cursor/story-editing-70d9` and the commit that contains these notes. `craft kdenlive` without that override builds upstream.

Minimum for a native arm64 build: an Apple Silicon Mac, macOS 13 or newer (`LSMinimumSystemVersion` is 13.0.0; 15.7.3 qualifies), Xcode/clang, CMake, and a Craft root with Qt 6, the KDE Frameworks 6 tiers listed in the blueprint, MLT, FFmpeg, and OpenTimelineIO. About 10 GiB free is not enough for that from-scratch prefix. Homebrew alone does not replace Craft here.

If a `.dmg` is built later, record architecture `arm64`, minimum OS 13.0, and that it is unsigned and not notarized. No Apple ID was used. Install it beside the existing app, for example as a separate "Kdenlive Story" bundle, and do not replace `/Applications/kdenlive.app`.

### Distribution obligations, before any package is handed over

The program is `GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL`. A recipient may use either. The KDE alternative is `LICENSES/LicenseRef-KDE-Accepted-GPL.txt`: GPL version 3 or a later version accepted by KDE e.V. as a Section 14 proxy. `COPYING` is the GPL-3 text. Other component notices live under `LICENSES/` (LGPL, BSD, MIT, Apache, CC).

Before a modified binary is conveyed:

- Say that it is modified and is not a KDE release, and give the date and the exact commit.
- Keep the same license. Do not add further restrictions.
- Ship `COPYING` and `LICENSES/` with the bundle, including the notices for Qt, KDE Frameworks, MLT, and FFmpeg that the Craft package already collects.
- Corresponding source is the public commit on `https://github.com/swimnbird/kdenlive` branch `cursor/story-editing-70d9`. The binary and that commit have to match. GPL section 6 requires the source to be offered with a binary; a public commit of that exact tree meets that if the repository stays available.
- Do not include Apple trademarks, Final Cut assets, or a signature made with credentials this run does not have.
- Do not upload it to download.kde.org or mark it as a public release.

No checksum, because no `.app` or `.dmg` was produced.
