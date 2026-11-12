"""Check ES3C28P icon references against the installed font component.

No second icon registry: the resolved xiaozhi-fonts header is authoritative.
Tests skip only when managed components have not been installed yet; the
ESP-IDF build resolves the dependency and compiler validates it as well.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "main" / "boards" / "lcdwiki-es3c28p"
FONT_PATHS = (
    ROOT / "managed_components/78__xiaozhi_fonts/include/material_symbols.h",
    ROOT / "components/78__xiaozhi_fonts/include/material_symbols.h",
    ROOT / "components/xiaozhi-fonts/include/material_symbols.h",
)

class TestEs3c28pIconGlyphs(unittest.TestCase):
    def test_declared_material_icons_exist(self):
        font_header = next((p for p in FONT_PATHS if p.is_file()), None)
        if font_header is None:
            self.skipTest("resolved 78/xiaozhi-fonts header is absent; ESP-IDF build still required")
        definitions = set(re.findall(
            r"^#define (MATERIAL_SYMBOLS_[A-Z0-9_]+)\\b",
            font_header.read_text(encoding="utf-8"), re.MULTILINE))
        referenced = set()
        for name in ("pixel_touch_ui.cc", "pixel_robot_display.cc"):
            referenced.update(re.findall(
                r"\\bMATERIAL_SYMBOLS_[A-Z0-9_]+\\b",
                (BOARD / name).read_text(encoding="utf-8")))
        self.assertTrue(definitions, "installed icon header has no known glyphs")
        self.assertFalse(referenced - definitions,
                         f"undefined Material Symbols: {sorted(referenced - definitions)}")

if __name__ == "__main__":
    unittest.main()
