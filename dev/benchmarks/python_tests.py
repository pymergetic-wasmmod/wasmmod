"""Host-side validation tests; no benchmark results are fabricated by this test."""
import copy
import unittest

from python_run import summarize


class SummaryTests(unittest.TestCase):
    def setUp(self):
        self.rows = [
            {"path": path, "work": work, "sample": sample, "checksum": 42,
             "calls": 100, "elapsed_us": 100 + sample, "ns_per_call": -1}
            for path in ("native_ffi", "wasmmod_interp_call", "wasmmod_aot_call")
            for work in (0, 1, 64, 4096)
            for sample in range(7)
        ]

    def test_statistics_derive_from_raw_counts(self):
        summary = summarize(self.rows)
        self.assertEqual(len(summary), 12)
        self.assertEqual(summary[0]["median_ns_per_call"], 1030)
        self.assertEqual(summary[0]["min_ns_per_call"], 1000)
        self.assertEqual(summary[0]["max_ns_per_call"], 1060)

    def test_missing_duplicate_wrong_path_and_bad_measurements_refused(self):
        with self.assertRaises(ValueError):
            summarize(self.rows[:-1])
        for field, value in (("sample", 1), ("path", "unknown"), ("calls", 0),
                             ("elapsed_us", 0), ("checksum", 99)):
            rows = copy.deepcopy(self.rows)
            rows[0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                summarize(rows)


if __name__ == "__main__":
    unittest.main()
