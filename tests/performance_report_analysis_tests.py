import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('performance', ROOT / 'scripts/analyze-performance-report.py')
performance = importlib.util.module_from_spec(spec)
spec.loader.exec_module(performance)


class PerformanceReportTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='performance-parser-')
        self.addCleanup(temporary.cleanup)
        self.path = Path(temporary.name) / 'report.txt'

    def parse(self, rows, header=''):
        self.path.write_text(header + ''.join(rows), encoding='utf-8')
        return performance.analyze(self.path)

    def sample(self, number, body, session=1):
        return f'AMOSTRA {number} | UTC=2026-09-27T15:00:00Z | elapsedMs={number * 1000}\nsession={session} mode=call\n{body}\n'

    def test_repeated_panel_latencies_are_not_resampled_as_events(self):
        report = self.parse([self.sample(n, '[outgoing]\nttsLatencyMs=46\nttsCacheMisses=4') for n in range(1, 31)])
        metrics = next(iter(report['paths'].values()))['metrics']
        self.assertEqual(metrics['ttsLatencyMs']['measuredSamples'], 30)
        self.assertEqual(metrics['ttsLatencyMs']['min'], 46)
        self.assertEqual(metrics['ttsLatencyMs']['max'], 46)
        self.assertNotIn('p95', metrics['ttsLatencyMs'])
        self.assertEqual(metrics['ttsCacheMisses']['counterDelta'], 0)

    def test_path_session_isolation_and_counter_resets(self):
        report = self.parse([self.sample(1, '[outgoing]\nttsCacheMisses=4\n[incoming]\nttsCacheMisses=8'),
                             self.sample(2, '[outgoing]\nttsCacheMisses=1\n[incoming]\nttsCacheMisses=9'),
                             self.sample(3, '[outgoing]\nttsCacheMisses=0', session=2)])
        self.assertEqual(len(report['paths']), 3)
        self.assertIsNone(report['paths']['session 1 / outgoing']['metrics']['ttsCacheMisses']['counterDelta'])
        self.assertEqual(report['paths']['session 1 / incoming']['metrics']['ttsCacheMisses']['counterDelta'], 1)
        self.assertEqual(report['paths']['session 2 / outgoing']['metrics']['ttsCacheMisses']['counterDelta'], 0)

    def test_invalid_values_and_content_are_not_output(self):
        report = self.parse([self.sample(1, '[outgoing]\nttsLatencyMs=NOT MEASURED\ntext=123\noriginal=PRIVATE\naudioError=PRIVATE\nloadedVoice=en_US-lessac-medium'),
                             self.sample(2, '[outgoing]\nttsLatencyMs=nan\nttsCacheHits=0')])
        metrics = report['paths']['session 1 / outgoing']['metrics']
        self.assertEqual(metrics['ttsLatencyMs']['measuredSamples'], 0)
        self.assertEqual(metrics['ttsCacheHits']['zeroSamples'], 1)
        self.assertNotIn('text', metrics)
        self.assertNotIn('PRIVATE', str(report))
        self.assertEqual(report['paths']['session 1 / outgoing']['observedIdentifiers']['loadedVoice'], ['en_US-lessac-medium'])

    def test_header_counts_hashes_and_duplicate_rejection(self):
        report = self.parse([self.sample(1, 'asrModel=Whisper small\nasrModelSHA256=123abc\nrequestedBackend=metal\n[outgoing]\nttsLatencyMs=42')], 'Duração: 2.5 s\nAmostras de painel: 2\n')
        self.assertEqual(report['metadata']['declaredDurationSeconds'], 2.5)
        self.assertTrue(report['warnings'])
        self.assertEqual(len(report['input']['sha256']), 64)
        self.assertEqual(report['observedConfigurations']['asrModel'], ['Whisper small'])
        self.assertEqual(report['observedConfigurations']['asrModelSHA256'], ['123abc'])
        with self.assertRaisesRegex(ValueError, 'Duplicate metric'):
            self.parse([self.sample(1, '[outgoing]\nttsLatencyMs=42\nttsLatencyMs=43')])

    def test_reset_does_not_become_negative_work(self):
        result = performance.summarize_values([10, 11, 0, 2], counter=True)
        self.assertEqual(result['counterResetObservations'], 1)
        self.assertIsNone(result['counterDelta'])


if __name__ == '__main__':
    unittest.main()
