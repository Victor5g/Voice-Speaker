import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('acceptance_evidence', ROOT / 'scripts/acceptance_evidence.py')
evidence = importlib.util.module_from_spec(spec)
spec.loader.exec_module(evidence)


class AcceptanceEvidenceTests(unittest.TestCase):
    def check_records(self, records):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            path = Path(temporary) / 'evidence.jsonl'
            path.write_text(''.join(json.dumps(record) + '\n' for record in records))
            return evidence.require_stable_conversation(path)

    def test_recovery_does_not_hide_wrong_confirmed_word(self):
        for state in ('revision-preserved', 'revision-conflict'):
            with self.subTest(state=state), self.assertRaises(ValueError):
                self.check_records([{'state': state}, {'result': 'PASS'}])

    def test_summary_or_duplex_counter_cannot_hide_lost_events(self):
        for key in ('preservedRevisions', 'revisionConflicts', 'recoveredContinuations',
                    'outgoingRevisionConflicts', 'incomingRevisionConflicts'):
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.check_records([{'result': 'PASS', key: 1}])

    def test_complete_stable_run_is_accepted(self):
        self.assertEqual(self.check_records([{'state': 'partial'}, {'state': 'played'},
                                             {'result': 'PASS', 'revisionConflicts': 0}])['result'], 'PASS')

    def test_truncated_or_failed_run_is_rejected(self):
        for records in ([], [{'state': 'played'}], [{'result': 'PASS'}, {'result': 'FAIL'}]):
            with self.subTest(records=records), self.assertRaises(ValueError):
                self.check_records(records)


if __name__ == '__main__':
    unittest.main()
