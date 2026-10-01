#!/usr/bin/python3
"""Pure parsing/preflight tests; no models loaded and no inference executed."""
from datetime import datetime, timedelta, timezone
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('candidate_screen', ROOT / 'scripts/compare-asr-candidate.py')
screen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(screen)


class ScreenTests(unittest.TestCase):
    def setUp(self):
        self.recordings = screen.chosen_recordings(json.loads((ROOT / 'tests/data/latency-human/manifest.json').read_text()))
        self.model = ROOT / 'models/optional/ggml-small.bin'
        self.rows = []
        for recording in self.recordings:
            for iteration in range(3):
                self.rows.append({'fixture': str(ROOT / recording['filename']), 'iteration': iteration,
                                  'model': str(self.model), 'backend': 0, 'metal': True,
                                  'encoderReuse': True, 'asrLatencyMs': 100 + iteration,
                                  'modelLoadMs': 300, 'warmupMs': 200, 'physicalFootprint': 9000,
                                  'melMs': 2, 'languageMs': 40, 'decodeMs': 58, 'audioDurationMs': 5000,
                                  'rtf': .02, 'peakRSS': 10000, 'metalAllocatedBytes': 4000,
                                  'thermalState': 0, 'sourceText': recording['reference'],
                                  'language': recording['language'][:2]})

    def test_explicit_selection_is_six_development_recordings(self):
        self.assertEqual(len(self.recordings), 6)
        self.assertEqual([r['developmentLanguageIndex'] for r in self.recordings], [0, 19, 29, 8, 19, 24])
        self.assertTrue(all(r['split'] == 'development' for r in self.recordings))

    def test_complete_summary_contains_per_iteration_wer_without_percentiles(self):
        rows, report = screen.evaluate(self.rows, self.recordings, self.model)
        self.assertEqual(len(rows), 18)
        self.assertEqual(report['latencyMs'], {'samples': 18, 'mean': 101, 'min': 100, 'max': 102})
        self.assertEqual(report['weightedWER'], 0)
        self.assertEqual(report['languages']['pt']['samples'], 9)
        self.assertEqual(len(report['fixtures']), 6)
        self.assertTrue(all(r['wer'] == 0 and r['meaningReview'] == 'PENDING' for r in rows))
        self.assertNotIn('p50', json.dumps(report).lower())
        self.assertNotIn('p95', json.dumps(report).lower())

    def test_missing_and_duplicate_rows_rejected(self):
        for rows in (self.rows[:-1], self.rows[:-1] + [self.rows[0]]):
            with self.subTest(rows=len(rows)), self.assertRaisesRegex(ValueError, 'Incomplete'):
                screen.evaluate(rows, self.recordings, self.model)

    def test_wrong_model_backend_and_nonfinite_measurements_rejected(self):
        for change in ({'model': '/wrong'}, {'backend': 1}, {'metal': False},
                       {'asrLatencyMs': float('nan')}, {'physicalFootprint': 0},
                       {'thermalState': 9}):
            rows = [dict(row) for row in self.rows]
            rows[0].update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                screen.evaluate(rows, self.recordings, self.model)

    def test_language_errors_recorded_without_pretending_quality_pass(self):
        self.rows[0].update({'language': 'en', 'sourceText': ''})
        rows, report = screen.evaluate(self.rows, self.recordings, self.model)
        self.assertEqual(rows[0]['wer'], 1)
        self.assertFalse(report['allLanguagesCorrect'])

    def test_wer_handles_case_accents_punctuation_and_numbers(self):
        self.assertEqual(screen.errors('Cálcio, 11:35.', 'CÁLCIO 11:35'), (0, 3))
        self.assertEqual(screen.errors('não ferver água', 'ferver água'), (1, 3))

    def test_preflight_rejects_external_build_or_inference(self):
        for name in ('xcodebuild', 'clang', 'swift-frontend', 'asr-benchmark', 'conversation-golden', 'human-corpus'):
            snapshot = {'capturedAt': datetime.now(timezone.utc).isoformat(),
                        'processes': [{'pid': 123, 'command': '/other/project/' + name}]}
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'Concurrent'):
                screen.validate_preflight(snapshot)

    def test_preflight_requires_recent_nonempty_inventory(self):
        now = datetime.now(timezone.utc)
        snapshot = {'capturedAt': now.isoformat(), 'processes': [{'pid': 1, 'command': '/sbin/launchd'}]}
        self.assertEqual(screen.validate_preflight(snapshot, now), snapshot)
        with self.assertRaises(ValueError):
            screen.validate_preflight(snapshot, now + timedelta(seconds=61))
        snapshot['processes'] = []
        with self.assertRaises(ValueError):
            screen.validate_preflight(snapshot, now)


if __name__ == '__main__':
    unittest.main(verbosity=2)
