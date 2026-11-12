"""Regression tests for the overlapping text observed on the real LCD."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from check_es3c28p_ui import collision_issues


def state(**rectangles):
    return {"anchors": rectangles}


class TestEs3c28pRealTextCollisions(unittest.TestCase):
    def test_detects_each_photo_reported_collision(self):
        fixtures = {
            "home": state(**{"home-lunar": [17, 146, 192, 18],
                             "home-hint-text": [31, 158, 145, 18]}),
            "player.story.idle": state(**{"play-status": [127, 51, 168, 32],
                                         "track-title": [115, 73, 186, 29]}),
            "calendar.default": state(**{"cal-picked": [23, 200, 180, 20],
                                         "cal-lunar": [23, 216, 178, 18]}),
            "settings.child-on": state(**{"settings-layout-detail": [14, 200, 164, 20],
                                         "setting-foot": [15, 212, 220, 16]}),
        }
        errors = collision_issues(fixtures)
        self.assertEqual(len(errors), 4, errors)

    def test_fixed_font_layout_has_no_known_overlap(self):
        states = {
            "home": state(**{"home-lunar": [17, 146, 192, 14],
                             "home-hint-text": [31, 165, 145, 16]}),
            "player.story.idle": state(**{"play-status": [127, 51, 168, 16],
                                         "track-title": [115, 73, 186, 29]}),
            "calendar.default": state(**{"cal-picked": [23, 200, 180, 16],
                                         "cal-lunar": [23, 216, 178, 14]}),
            "settings.child-on": state(**{"settings-layout-detail": [14, 200, 164, 14],
                                         "setting-foot": [15, 220, 220, 15]}),
        }
        self.assertEqual(collision_issues(states), [])

    def test_missing_measured_label_fails_closed(self):
        errors = collision_issues({"home": state(**{"home-lunar": [17, 146, 192, 14]})})
        self.assertEqual(len(errors), 1)
        self.assertIn("missing collision evidence", errors[0])


if __name__ == "__main__":
    unittest.main()
