# SPDX-FileCopyrightText: 2026 Kdenlive contributors
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
"""Contract tests for the Whisper word-timing adapter.

These tests do not load a Whisper model and do not prove transcription.
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "data", "scripts", "whisper"))
import wordtiming  # noqa: E402


class WordTimingTests(unittest.TestCase):
    def test_frame_rounding_matches_half_away_from_zero(self):
        self.assertEqual(wordtiming.seconds_to_frame(1.0, 25), 25)
        self.assertEqual(wordtiming.seconds_to_frame(0.5, 25), 13)
        self.assertEqual(wordtiming.seconds_to_frame(0.02, 24), 0)
        self.assertEqual(wordtiming.frame_to_seconds(25, 25), 1.0)

    def test_words_from_result_keep_times_and_skip_empty(self):
        result = {
            "segments": [
                {
                    "words": [
                        {"start": 0.10, "end": 0.40, "word": " Hello"},
                        {"start": 0.40, "end": 0.40, "word": "nope"},
                        {"start": 0.50, "end": 0.80, "word": " there"},
                    ]
                }
            ]
        }
        words = wordtiming.words_from_whisper_result(result, offset_sec=1.0)
        self.assertEqual([word["spoken"] for word in words], [" Hello", " there"])
        self.assertEqual(words[0]["start"], 1.10)
        self.assertEqual(words[0]["display"], words[0]["spoken"])
        self.assertFalse(words[0]["needsRetranscribe"])

    def test_stdout_round_trip_shape(self):
        words = wordtiming.words_from_whisper_result(
            {"segments": [{"words": [{"start": 0.0, "end": 0.2, "word": "um"}]}]}
        )
        text = wordtiming.whisper_stdout(words)
        self.assertIn("[0.0>0.2]um", text)


if __name__ == "__main__":
    unittest.main()
