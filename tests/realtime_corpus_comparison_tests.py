import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('comparison', ROOT / 'scripts/compare-realtime-corpus.py')
comparison = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparison)


class RealtimeCorpusComparisonTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='realtime-comparison-')
        self.addCleanup(temp.cleanup)
        self.directory = Path(temp.name)

    def row(self, language='en', **changes):
        row = dict(recordingResult=True, recording=language + '.wav', sha256='fixture-hash',
                   language=language, split='development', reference='speech', original='speech',
                   translation='fala', translationReference='fala', captureOrigin=1000,
                   captureFirstFrame=0, samples=16000, translatedUnits=1, playedUnits=1, errors=[])
        row.update(changes)
        return row

    def event(self, state, ticks=0, language='en', **changes):
        event = dict(recording=language + '.wav', state=state, session=1, segment=8,
                     timing=dict(playbackEvent=ticks, ticksPerSecond=1000,
                                 endpointReason=1, afterVadEndEstimatedMs=50, windowEndFrame=16000))
        event.update(changes)
        return event

    def write(self, phase, language, omitted=False):
        events = [self.event('translated', language=language)]
        if omitted:
            events.append(self.event('audio-skipped', language=language))
        else:
            events += [self.event('audio-onset', 3000, language), self.event('played', 4000, language)]
        rows = events + [self.row(language, playedUnits=0 if omitted else 1), dict(result='PASS', recordings=1, peakFootprintBytes=1234)]
        path = self.directory / f'corpus-{phase}-{language}.jsonl'
        path.write_text(''.join(json.dumps(row) + '\n' for row in rows))
        return path

    def test_unique_identity_and_capture_end_have_different_meanings(self):
        onset = self.event('audio-onset', 3000)
        events = [self.event('translated'), onset, onset, self.event('playing', 2999), self.event('played', 4000)]
        result = comparison.recording_result(self.row(), events)
        self.assertEqual(result['onsetUnits'], 1)
        self.assertEqual(result['repeatedStatesIgnored'], 1)
        self.assertEqual(result['firstAudioFromCaptureStartMs'], 2000)
        self.assertEqual(result['lastPlaybackFromCaptureStartMs'], 3000)
        self.assertEqual(result['lastPlaybackFromCaptureEndMs'], 2000)
        self.assertEqual(result['afterVadEndEstimatedMsByUnit'], [50])
        self.assertTrue(result['timingEligible'])
        self.assertIsNone(comparison.stats([50] * 29)['p95Ms'])
        self.assertEqual(comparison.stats([50] * 30)['p95Ms'], 50)

    def test_omitted_output_does_not_become_faster_timing(self):
        for language in ('en', 'pt'):
            self.write('before', language)
            self.write('after', language, omitted=True)
        report = comparison.compare(self.directory, expected=1)
        self.assertTrue(report['comparisonComplete'])
        self.assertEqual(report['comparablePairs'], 0)
        self.assertEqual(report['pairedTimingDeltas']['firstAudioFromCaptureStartMs']['n'], 0)
        self.assertTrue(all(not pair['timingComparable'] for pair in report['pairs']))
        self.assertEqual(report['meaningAcceptance'], 'PENDING')

    def test_before_only_and_mismatched_inputs_never_form_valid_comparison(self):
        self.write('before', 'en')
        report = comparison.compare(self.directory, expected=1)
        self.assertFalse(report['comparisonComplete'])
        self.assertEqual(report['runs']['after-en']['status'], 'MISSING')
        path = self.write('after', 'en')
        path.write_text(path.read_text().replace('fixture-hash', 'wrong-hash'))
        with self.assertRaisesRegex(ValueError, 'Unmatched sha256'):
            comparison.compare(self.directory, expected=1)

    def test_bad_clock_cross_recording_and_conflicting_repeats_are_explicit(self):
        events = [self.event('translated'), self.event('audio-onset', 3000), self.event('audio-onset', 3500), self.event('played', 4000)]
        result = comparison.recording_result(self.row(captureFirstFrame=32000), events)
        self.assertFalse(result['timingEligible'])
        self.assertIn('event-from-previous-recording', result['comparisonIssues'])
        self.assertIn('conflicting-duplicate-audio-onset', result['comparisonIssues'])
        events[1]['timing']['ticksPerSecond'] = 0
        result = comparison.recording_result(self.row(), events)
        self.assertIn('missing-or-invalid-playback-clock', result['comparisonIssues'])

    def test_equal_inputs_and_coverage_still_do_not_approve_meaning(self):
        for language in ('en', 'pt'):
            for phase in ('before', 'after'):
                self.write(phase, language)
        report = comparison.compare(self.directory, expected=1)
        self.assertTrue(report['comparisonComplete'])
        self.assertEqual(report['comparablePairs'], 2)
        self.assertEqual(report['pairedTimingDeltas']['firstAudioFromCaptureStartMs']['meanMs'], 0)
        self.assertIsNone(report['pairedTimingDeltas']['firstAudioFromCaptureStartMs']['p50Ms'])
        self.assertTrue(all(pair['meaningAcceptance'] == 'PENDING' for pair in report['pairs']))


if __name__ == '__main__':
    unittest.main()
