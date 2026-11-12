"""Lightweight V1.1 baseline guards; no browser/ESP-IDF dependency."""
import hashlib
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ASSET = ROOT / "prototypes/esp32-ui"

class TestEs3c28pUiContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.baseline = json.loads((ASSET / "ui-baseline.json").read_text(encoding="utf-8"))

    def test_approved_html_is_immutable(self):
        expected = self.baseline["source_sha256"]
        observed = hashlib.sha256((ASSET / "index.html").read_bytes()).hexdigest()
        self.assertEqual(observed, expected)

    def test_all_v11_pages_and_behaviors_are_listed(self):
        states = self.baseline["states"]
        self.assertEqual({s["page"] for s in states.values()},
                         {"home", "voice", "player", "calendar", "settings"})
        self.assertTrue({"voice.listening", "player.story.volume", "calendar.detail"} <= states.keys())
        self.assertTrue({"cal-pick", "voice-stop", "talk", "toggle-volume"} <=
                        set(self.baseline["actions"]))
        self.assertEqual(len(self.baseline["icons"]), 12)

    def test_viewport_and_anchor_bounds(self):
        self.assertEqual(self.baseline["viewport"]["width"], 320)
        self.assertEqual(self.baseline["viewport"]["height"], 240)
        for state, spec in self.baseline["states"].items():
            for key, rect, style in spec["anchors"]:
                with self.subTest(state=state, anchor=key):
                    x, y, width, height = rect
                    self.assertGreater(width, 0)
                    self.assertGreater(height, 0)
                    self.assertGreaterEqual(x, 0)
                    self.assertGreaterEqual(y, 0)
                    self.assertLessEqual(x + width, 322)
                    self.assertLessEqual(y + height, 242)

if __name__ == "__main__":
    unittest.main()
