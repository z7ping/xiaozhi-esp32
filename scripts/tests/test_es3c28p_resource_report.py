import unittest

from scripts.es3c28p_resource_report import parse_samples, summarize


class ResourceReportTests(unittest.TestCase):
    def test_parses_esp_idf_prefixed_lines_and_skips_others(self):
        lines = [
            'I (5700) ES3C28P_RES: uptime_ms=5700 state=idle internal_free=61000 '
            'internal_min=58000 internal_largest=33000 psram_free=2400000 '
            'psram_min=2100000 psram_largest=1200000\n',
            'I (6000) AfeAudioEngine: normal log\n',
            'I (8700) ES3C28P_RES: uptime_ms=8700 state=speaking internal_free=60000 '
            'internal_min=57000 internal_largest=32000 psram_free=2200000 '
            'psram_min=2100000 psram_largest=1100000\n',
        ]
        samples = list(parse_samples(lines))
        self.assertEqual(2, len(samples))
        self.assertEqual('speaking', samples[1]['state'])
        summary = summarize(samples)
        self.assertEqual(2, len(summary))
        self.assertEqual(32000, summary['speaking']['min_internal_largest'])

    def test_empty_result(self):
        self.assertEqual({}, summarize(parse_samples(['unrelated\n'])))


if __name__ == '__main__':
    unittest.main()
