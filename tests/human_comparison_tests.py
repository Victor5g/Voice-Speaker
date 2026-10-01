import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('review', ROOT / 'tools/diagnostics/review-human-comparison.py')
review = importlib.util.module_from_spec(spec)
spec.loader.exec_module(review)


class HumanComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='human-review-')
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)

    def record(self, language='pt', **changes):
        row = dict(recordingResult=True, recording=language + '.wav', sha256='verified-fixture',
                   reference='Não paguei 20 reais.' if language == 'pt' else 'I did not pay 20 reais.',
                   original='Não paguei 20 reais.' if language == 'pt' else 'I did not pay 20 reais.',
                   translationReference='I did not pay 20 reais.', translation='I did not pay 20 reais.',
                   language=language, split='development', errors=[], unknownUnits=0, speakers=[1])
        row.update(changes)
        return row

    def trace(self, name, rows):
        path = self.directory / (name + '.jsonl')
        path.write_text(''.join(json.dumps(r, ensure_ascii=False) + '\n' for r in rows))
        return path

    def test_missing_and_partial_utf8_never_approve_completion(self):
        report = review.compare(self.directory, 1)
        self.assertFalse(report['comparisonComplete'])
        path = self.trace('pt-control', [self.record()])
        with path.open('ab') as stream:
            stream.write(b'{"unfinished":"\xc3')
        run = review.read_trace(path, 1)
        self.assertEqual(run['status'], 'OPEN_TRACE')
        self.assertEqual(len(run['records']), 1)

    def test_malformed_complete_line_and_duplicate_results_are_rejected(self):
        path = self.trace('pt-control', [self.record(), self.record()])
        with self.assertRaisesRegex(ValueError, 'Duplicate recording'):
            review.read_trace(path, 1)
        path.write_text('{invalid}\n')
        with self.assertRaises(json.JSONDecodeError):
            review.read_trace(path, 1)

    def test_native_pass_cannot_hide_missing_recordings_or_errors(self):
        path = self.trace('pt-control', [self.record(), dict(result='PASS', recordings=2)])
        self.assertEqual(review.read_trace(path, 2)['status'], 'INCOMPLETE')
        path = self.trace('pt-control', [self.record(errors=['revision-preserved']), dict(result='PASS', recordings=1)])
        self.assertEqual(review.read_trace(path, 1)['status'], 'FUNCTIONAL_FAIL')

    def test_fixture_hash_and_reference_must_match(self):
        self.trace('pt-control', [self.record()])
        self.trace('pt-comma-boundary', [self.record(sha256='other-audio')])
        with self.assertRaisesRegex(ValueError, 'Unmatched sha256'):
            review.compare(self.directory, 1)
        self.trace('pt-comma-boundary', [self.record(reference='Changed reference')])
        with self.assertRaisesRegex(ValueError, 'Unmatched reference'):
            review.compare(self.directory, 1)

    def test_functional_success_does_not_approve_meaning(self):
        for language in ('pt', 'en'):
            for mode in ('control', 'comma-boundary'):
                changes = {} if mode == 'control' else dict(original='Paguei 200 reais.')
                self.trace(language + '-' + mode, [self.record(language, **changes), dict(result='PASS', recordings=1)])
        report = review.compare(self.directory, 1)
        self.assertTrue(report['comparisonComplete'])
        self.assertEqual(report['meaningAcceptance'], 'PENDING')
        self.assertEqual(len(report['increasedWordErrorRecordings']), 2)
        self.assertEqual(len(report['pairs'][0]['candidate']['lexicalAlerts']), 2)
        self.assertTrue(all(r['reviewRequired'] for r in report['pairs']))

    def test_errors_are_attributed_to_candidate_and_revisions_retained(self):
        self.trace('pt-control', [self.record(), dict(result='PASS', recordings=1)])
        event = dict(recording='pt.wav', state='revision-preserved', original='Changed', timing={})
        self.trace('pt-comma-boundary', [event, self.record(errors=['revision-preserved']), dict(result='FAIL', recordings=1)])
        report = review.compare(self.directory, 1)
        self.assertFalse(report['comparisonComplete']) # EN has not been evaluated.
        self.assertEqual(report['functionalRegressionRecordings'], ['pt.wav'])
        self.assertEqual(report['pairs'][0]['candidate']['revisions'], [event])

    def test_portuguese_contraction_no_is_not_english_negation(self):
        self.assertEqual(review.lexical_alerts('No entanto, não paguei.', 'Eu não paguei.', 'pt'), [])
        self.assertTrue(review.lexical_alerts('No payment.', 'Payment.', 'en'))
        self.assertTrue(review.lexical_alerts('Na área reservada.', 'Nada reservado.', 'pt'))

    def test_filtered_content_is_preserved_for_review_not_automatically_wrong(self):
        event = dict(recording='pt.wav', state='direction-filtered', original='Great Yarmouth', timing={})
        for mode in ('control', 'comma-boundary'):
            self.trace('pt-' + mode, [event, self.record(), dict(result='PASS', recordings=1)])
        report = review.compare(self.directory, 1)
        self.assertEqual(report['runs']['pt-control']['nonSpokenEventsToReview'], 1)
        self.assertEqual(report['runs']['pt-control']['functionalFailures'], 0)
        self.assertEqual(report['pairs'][0]['candidate']['nonSpokenEvents'], [event])
        self.assertEqual(report['meaningAcceptance'], 'PENDING')

    def test_first_onset_is_one_sample_per_recording_not_per_unit(self):
        record = self.record(captureOrigin=1000, samples=32000)
        events = [dict(state='recognized', timing=dict(endpointReason=0)),
                  dict(state='audio-onset', timing=dict(playbackEvent=2400, ticksPerSecond=1000, endpointReason=0)),
                  dict(state='audio-onset', timing=dict(playbackEvent=3900, ticksPerSecond=1000, endpointReason=1, afterVadEndEstimatedMs=120))]
        result = review.record_timing(record, list(reversed(events)))
        self.assertEqual(result['captureToFirstOnsetMs'], 1400)
        self.assertIsNone(result['firstAfterVadEndEstimatedMs'])
        self.assertTrue(result['hasPartialConfirmation'])
        self.assertTrue(result['firstOnsetBeforeInputEnd'])

    def test_missing_clock_is_unmeasured_and_small_samples_have_no_percentiles(self):
        events = [dict(state='audio-onset', timing=dict(playbackEvent=5000, ticksPerSecond=1000))]
        self.assertIsNone(review.record_timing(self.record(), events)['captureToFirstOnsetMs'])
        self.assertEqual(review.distribution([None, float('nan'), -1]), dict(n=0, meanMs=None))
        self.assertNotIn('p95Ms', review.distribution([100] * 29))
        self.assertEqual(review.distribution([100] * 30)['p95Ms'], 100)

    def test_late_unit_invalidates_attribution_without_repairing_native_result(self):
        late = dict(recording='pt.wav', state='audio-onset', original='',
                    sourceSegment=8, timing=dict(windowEndFrame=16000,
                    playbackEvent=5000, ticksPerSecond=1000, endpointReason=1,
                    afterVadEndEstimatedMs=500))
        for language in ('pt', 'en'):
            for mode in ('control', 'comma-boundary'):
                tainted = language == 'pt' and mode == 'comma-boundary'
                record = self.record(language, captureFirstFrame=16000,
                                     captureOrigin=1000, samples=32000)
                if tainted:
                    record.update(original='Previous recording text. Não paguei 20 reais.',
                                  errors=['incomplete-playback'])
                self.trace(language + '-' + mode,
                           ([late] if tainted else []) + [record,
                           dict(result='FAIL' if tainted else 'PASS', recordings=1)])
        report = review.compare(self.directory, 1)
        run = report['runs']['pt-comma-boundary']
        self.assertEqual(run['status'], 'INVALID_ATTRIBUTION')
        self.assertEqual(run['nativeStatus'], 'FUNCTIONAL_FAIL')
        self.assertEqual(run['summary']['result'], 'FAIL')
        self.assertFalse(run['comparisonEligible'])
        self.assertIsNone(run['sourceWER'])
        self.assertGreater(run['rawSourceWER'], 0)
        self.assertEqual(run['captureToFirstOnset']['n'], 0)
        self.assertTrue(report['nativeRunsComplete'])
        self.assertFalse(report['comparisonComplete'])
        pair = report['pairs'][0]
        self.assertFalse(pair['comparisonEligible'])
        self.assertEqual(pair['candidate']['attributionIssues'], [late])
        self.assertTrue(pair['candidate']['original'].startswith('Previous recording text.'))
        self.assertIsNone(pair['candidate']['timing']['captureToFirstOnsetMs'])
        self.assertEqual(pair['candidate']['rawTiming']['captureToFirstOnsetMs'], 4000)
        self.assertNotIn('pt.wav', report['increasedWordErrorRecordings'])

    def test_preroll_is_allowed_when_unit_window_reaches_current_recording(self):
        record = self.record(captureFirstFrame=16000)
        event = dict(recording='pt.wav', state='recognized', original='Speech',
                     firstFrame=15000, timing=dict(firstFrame=15000, windowEndFrame=20000))
        self.assertEqual(review.attribution_issues(record, [event]), [])
        self.assertEqual(review.attribution_issues(record, [dict(timing={})]), [])
        self.assertEqual(review.attribution_issues(record, [dict(timing=dict(windowEndFrame=0))]), [])
        self.assertEqual(review.attribution_issues(self.record(captureFirstFrame=0), [event]), [])
        path = self.trace('pt-control', [event, record, dict(result='PASS', recordings=1)])
        self.assertEqual(review.read_trace(path, 1)['status'], 'FUNCTIONAL_PASS')

    def test_native_pass_cannot_approve_proven_late_attribution(self):
        event = dict(recording='pt.wav', state='translated', original='Previous',
                     timing=dict(windowEndFrame=10000))
        path = self.trace('pt-control', [event, self.record(captureFirstFrame=16000),
                                        dict(result='PASS', recordings=1)])
        run = review.read_trace(path, 1)
        self.assertEqual(run['status'], 'INVALID_ATTRIBUTION')
        self.assertEqual(run['nativeStatus'], 'FUNCTIONAL_PASS')
        self.assertEqual(run['summary']['result'], 'PASS')

    def test_tainted_run_also_withholds_wer_for_missing_source_record(self):
        late = dict(recording='next.wav', state='translated', original='Previous',
                    timing=dict(windowEndFrame=20000))
        self.trace('pt-control', [self.record(captureFirstFrame=16000),
                                 self.record(recording='next.wav', captureFirstFrame=32000)])
        self.trace('pt-comma-boundary', [self.record(original='', captureFirstFrame=16000),
                    late, self.record(recording='next.wav', captureFirstFrame=32000)])
        report = review.compare(self.directory, 2)
        source = next(pair for pair in report['pairs'] if pair['recording'] == 'pt.wav')
        self.assertTrue(source['candidate']['comparisonEligible']) # No mismatched event here.
        self.assertFalse(source['comparisonEligible']) # The run can have omitted its late unit.
        self.assertEqual(report['increasedWordErrorRecordings'], [])


if __name__ == '__main__':
    unittest.main()
