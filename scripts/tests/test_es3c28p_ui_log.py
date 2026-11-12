"""Parse real ES3C28P LVGL geometry logs without inventing states."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from parse_es3c28p_ui_log import parse

class TestEs3c28pUiLog(unittest.TestCase):
    def test_runtime_log_round_trip(self):
        fixture = """I (123) ES3C28P_UI: state=home anchor=.clock x=17 y=52 w=192 h=62
I (124) OTHER: unrelated
I (130) ES3C28P_UI: state=voice.listening anchor=voice-title x=8 y=151 w=304 h=24
"""
        result, count = parse(fixture)
        self.assertEqual(count, 2)
        self.assertEqual(result["states"]["home"]["anchors"][".clock"], [17, 52, 192, 62])
        self.assertEqual(result["states"]["voice.listening"]["anchors"]["voice-title"],
                         [8, 151, 304, 24])
        self.assertNotIn("player.story.playing", result["states"])

    def test_detects_real_font_clipping_warnings(self):
        log = """W (91) ES3C28P_UI: label clipped: text=农历 box_h=14 font_h=16
I (99) ES3C28P_UI: state=home begin=1
I (101) ES3C28P_UI: state=home anchor=.clock x=17 y=52 w=192 h=62
"""
        result, count = parse(log)
        self.assertEqual(count, 1)
        self.assertEqual(len(result["layout_warnings"]), 1)
        self.assertIn("font_h=16", result["layout_warnings"][0])

    def test_no_fake_defaults(self):
        result, count = parse("NO ESP32 LCD DATA\n")
        self.assertEqual(count, 0)
        self.assertEqual(result["states"], {})

    def test_capture_boundary_discards_stale_geometry(self):
        log = """ES3C28P_UI: state=home begin=1
ES3C28P_UI: state=home anchor=.clock x=17 y=52 w=192 h=54
ES3C28P_UI: state=home anchor=@story x=13 y=185 w=93 h=43
ES3C28P_UI: state=home begin=1
ES3C28P_UI: state=home anchor=.clock x=18 y=52 w=192 h=54
"""
        result, count = parse(log)
        self.assertEqual(count, 3)
        self.assertEqual(result["capture_format"], 2)
        self.assertEqual(result["states"]["home"]["anchors"],
                         {".clock": [18, 52, 192, 54]})

    def test_missing_boundaries_cannot_be_strict_evidence(self):
        log = "ES3C28P_UI: state=home anchor=.clock x=17 y=52 w=192 h=54"
        result, count = parse(log)
        self.assertEqual(count, 1)
        self.assertEqual(result["capture_format"], 1)

    def test_latest_measurement_wins(self):
        log = """ES3C28P_UI: state=home anchor=.clock x=17 y=52 w=192 h=62
ES3C28P_UI: state=home anchor=.clock x=16 y=52 w=192 h=62
"""
        result, count = parse(log)
        self.assertEqual(count, 2)
        self.assertEqual(result["states"]["home"]["anchors"][".clock"][0], 16)

if __name__ == "__main__":
    unittest.main()
