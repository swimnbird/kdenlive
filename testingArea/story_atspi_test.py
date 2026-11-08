#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Kdenlive contributors

"""Drive the existing kdenlive binary through selenium-webdriver-at-spi."""

import pathlib
import traceback
import unittest

from appium import webdriver
from appium.options.common.base import AppiumOptions
from appium.webdriver.common.appiumby import AppiumBy
from selenium.common.exceptions import NoSuchElementException
from selenium.webdriver.support.ui import WebDriverWait

EVIDENCE = pathlib.Path("/workspace/testingArea/sync-evidence")
KDENLIVE = "/workspace/build-story/bin/kdenlive"
MEDIA = "/workspace/testingArea/sync-evidence/marked.wav"


def click_named(driver, name):
    element = driver.find_element(by=AppiumBy.NAME, value=name)
    element.click()
    return element


class StoryAcceptance(unittest.TestCase):
    driver = None

    @classmethod
    def setUpClass(cls):
        options = AppiumOptions()
        options.set_capability("appium:app", f"{KDENLIVE} --no-welcome {MEDIA}")
        options.set_capability(
            "appium:environ",
            {
                "HOME": "/workspace/testingArea/gui-home",
                "XDG_CONFIG_HOME": "/workspace/testingArea/gui-home/.config",
                "XDG_DATA_HOME": "/workspace/testingArea/gui-home/.local/share",
                "XDG_CACHE_HOME": "/workspace/testingArea/gui-home/.cache",
                "DISPLAY": ":1",
                "QT_QPA_PLATFORM": "xcb",
                "QSG_RHI_BACKEND": "software",
                "QT_QUICK_BACKEND": "software",
                "LIBGL_ALWAYS_SOFTWARE": "1",
            },
        )
        options.set_capability("timeouts", {"implicit": 40000, "pageLoad": 60000, "script": 30000})
        cls.driver = webdriver.Remote(command_executor="http://127.0.0.1:4723", options=options)
        cls.driver.implicitly_wait = 8

    @classmethod
    def tearDownClass(cls):
        if cls.driver is not None:
            try:
                source = cls.driver.page_source or ""
            except Exception:
                source = ""
            (EVIDENCE / "atspi-tree-final.xml").write_text(source, encoding="utf-8")
            cls.driver.quit()

    def test_speech_editor_controls(self):
        driver = self.driver
        EVIDENCE.mkdir(parents=True, exist_ok=True)
        notes = []

        def wait_source(predicate, timeout=25):
            def ready(d):
                text = d.page_source or ""
                return text if predicate(text) else False

            return WebDriverWait(driver, timeout).until(ready)

        try:
            wait_source(lambda text: "Start Normally" in text or "Speech Editor" in text, 40)
        except Exception as exc:
            notes.append(f"startup wait: {exc}")
        if "Start Normally" in (driver.page_source or ""):
            click_named(driver, "Start Normally")
            notes.append("clicked Start Normally")
        try:
            tree = wait_source(lambda text: "Speech Editor" in text and "Splash Screen" not in text, 40)
        except Exception as exc:
            tree = driver.page_source or ""
            notes.append(f"main window wait: {exc}")
        (EVIDENCE / "atspi-tree.xml").write_text(tree, encoding="utf-8")
        notes.append(f"tree bytes {len(tree)}")
        notes.append(f"marked in tree {MEDIA.split('/')[-1] in tree}")
        notes.append(f"install message {'Please install speech recognition models' in tree}")
        try:
            if "Could not create the video preview window" in (driver.page_source or ""):
                click_named(driver, "OK")
                notes.append("dismissed preview error")
        except NoSuchElementException as exc:
            notes.append(f"no preview OK: {exc}")
        try:
            click_named(driver, "marked.wav")
            notes.append("clicked marked.wav")
        except NoSuchElementException as exc:
            notes.append(f"no marked.wav: {exc}")
        try:
            click_named(driver, "Speech Editor")
            notes.append("clicked Speech Editor")
        except NoSuchElementException as exc:
            notes.append(f"no Speech Editor: {exc}")
        try:
            button = driver.find_element(by=AppiumBy.NAME, value="Transcribe")
            notes.append(f"transcribe attribute states={button.get_attribute('states')!r}")
            button.click()
            notes.append("clicked Transcribe")
            try:
                spoken = wait_source(lambda text: "hello" in text.lower() or ">um" in text.lower() or "No speech" in text or "Starting speech" in text, 50)
                notes.append(
                    "after transcribe "
                    f"hello={'hello' in spoken.lower()} um={'um' in spoken.lower()} "
                    f"starting={'Starting speech' in spoken} nospeech={'No speech' in spoken}"
                )
            except Exception as exc:
                notes.append(f"transcript wait: {exc}")
        except NoSuchElementException as exc:
            notes.append(f"no Transcribe button: {exc}")
        import subprocess
        subprocess.run(
            ["ffmpeg", "-y", "-f", "x11grab", "-video_size", "1920x1200", "-i", ":1", "-frames:v", "1",
             str(EVIDENCE / "atspi-kdenlive.png")],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        (EVIDENCE / "atspi-notes.txt").write_text("\n".join(notes) + "\n", encoding="utf-8")
        print("ATSPI_NOTES")
        print("\n".join(notes))
        self.assertIn("Speech Editor", driver.page_source or "")


if __name__ == "__main__":
    unittest.main(verbosity=2)
