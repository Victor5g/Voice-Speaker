#!/usr/bin/python3
"""No network/model inference: acquisition integrity and project-local boundaries."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('fetch_asr_candidate', ROOT / 'scripts/fetch-asr-candidate.py')
fetch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fetch)


class AcquisitionTests(unittest.TestCase):
    def setUp(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='asr-acquisition-test-', dir=ROOT / 'build')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'models/optional').mkdir(parents=True)
        self.payload = b'fixture model; not real inference weights'
        self.lock = json.loads((ROOT / 'models/asr-candidates.lock.json').read_text())
        self.model = self.lock['candidates'][0]['model']
        self.model['sha256'] = hashlib.sha256(self.payload).hexdigest()
        self.model['expectedSize'] = len(self.payload)
        self.model['filename'] = 'optional/ggml-large-v3-turbo-q5_0.bin'
        self.target = self.root / 'models' / self.model['filename']
        self.manifest = self.root / 'models/manifest.json'
        self.original = b'{"schemaVersion":1,"note":"retain unrelated metadata","models":[]}\n'
        self.manifest.write_bytes(self.original)
        self.selection = self.root / 'models/selection.json'
        self.selection.write_bytes(b'{"filename":"existing.bin","profiles":["unchanged"]}\n')
        self.save_lock()

    def save_lock(self):
        (self.root / 'models/asr-candidates.lock.json').write_text(json.dumps(self.lock))

    def fetch(self, downloader=None):
        def mock_download(url, target):
            self.assertEqual(url, self.model['source'])
            self.assertIn(self.root, target.parents)
            target.write_bytes(self.payload)
        return fetch.acquire(self.root, 'large-v3-turbo-q5_0', downloader or mock_download)

    def assert_preserved(self):
        self.assertEqual(self.manifest.read_bytes(), self.original)
        self.assertFalse(list((self.root / 'models/optional').glob('.asr-candidate-*')))

    def test_valid_download_registers_only_inventory(self):
        selection = self.selection.read_bytes()
        self.fetch()
        self.assertEqual(self.target.read_bytes(), self.payload)
        catalog = json.loads(self.manifest.read_text())
        self.assertEqual(catalog['models'], [self.model])
        self.assertEqual(catalog['note'], 'retain unrelated metadata')
        self.assertEqual(selection, self.selection.read_bytes())

    def test_cached_registered_candidate_skips_download_and_preserves_manifest_bytes(self):
        self.fetch()
        before = self.manifest.read_bytes()
        self.fetch(lambda *_: self.fail('Unexpected download'))
        self.assertEqual(self.manifest.read_bytes(), before)

    def test_verified_unregistered_cache_only_registers(self):
        self.target.write_bytes(self.payload)
        self.fetch(lambda *_: self.fail('Unexpected download'))
        self.assertEqual(json.loads(self.manifest.read_text())['models'], [self.model])

    def test_registration_preserves_unrelated_model_entries(self):
        existing = {'filename': 'optional/current.bin', 'custom': {'keep': True}}
        self.manifest.write_text(json.dumps({'schemaVersion': 1, 'models': [existing]}))
        self.fetch()
        self.assertEqual(json.loads(self.manifest.read_text())['models'], [existing, self.model])

    def test_wrong_size_preserves_existing_file_and_manifest(self):
        self.target.write_bytes(b'existing corrupt candidate')
        with self.assertRaisesRegex(ValueError, 'size/SHA-256'):
            self.fetch(lambda _, path: path.write_bytes(b'bad'))
        self.assertEqual(self.target.read_bytes(), b'existing corrupt candidate')
        self.assert_preserved()

    def test_wrong_hash_of_correct_size_preserves_manifest(self):
        with self.assertRaisesRegex(ValueError, 'size/SHA-256'):
            self.fetch(lambda _, path: path.write_bytes(b'x' * len(self.payload)))
        self.assertFalse(self.target.exists())
        self.assert_preserved()

    def test_transport_failure_preserves_existing_file(self):
        self.target.write_bytes(b'previous')
        def fail(_, path):
            path.write_bytes(b'partial')
            raise OSError('connection lost')
        with self.assertRaisesRegex(OSError, 'connection lost'):
            self.fetch(fail)
        self.assertEqual(self.target.read_bytes(), b'previous')
        self.assert_preserved()

    def test_target_symlink_rejected(self):
        self.target.symlink_to(self.selection)
        with self.assertRaisesRegex(ValueError, 'Symlink'):
            self.fetch()
        self.assert_preserved()

    def test_parent_symlink_rejected(self):
        (self.root / 'models/optional').rmdir()
        other = self.root / 'elsewhere'
        other.mkdir()
        (self.root / 'models/optional').symlink_to(other, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'Symlink'):
            self.fetch()
        self.assertEqual(list(other.iterdir()), [])

    def test_manifest_symlink_rejected(self):
        self.manifest.unlink()
        self.manifest.symlink_to(self.selection)
        with self.assertRaisesRegex(ValueError, 'Symlink'):
            self.fetch()

    def test_candidate_lock_symlink_rejected(self):
        lock = self.root / 'models/asr-candidates.lock.json'
        lock.unlink()
        lock.symlink_to(ROOT / 'models/asr-candidates.lock.json')
        with self.assertRaisesRegex(ValueError, 'Symlink'):
            self.fetch()
        self.assert_preserved()

    def test_process_lock_symlink_rejected(self):
        (self.root / 'build').mkdir()
        (self.root / 'build/asr-candidate-acquisition.lock').symlink_to(self.selection)
        with self.assertRaisesRegex(ValueError, 'Symlink'):
            self.fetch()
        self.assert_preserved()

    def test_duplicate_candidate_rejected(self):
        self.lock['candidates'].append(copy.deepcopy(self.lock['candidates'][0]))
        self.save_lock()
        with self.assertRaisesRegex(ValueError, 'duplicate candidate'):
            self.fetch()
        self.assert_preserved()

    def test_missing_download_output_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'size/SHA-256'):
            self.fetch(lambda *_: None)
        self.assertFalse(self.target.exists())
        self.assert_preserved()

    def test_traversal_and_absolute_paths_rejected(self):
        for name in ('../outside.bin', '/tmp/outside.bin', 'optional/../../outside.bin', 'optional//bad.bin'):
            with self.subTest(name=name):
                self.model['filename'] = name
                self.save_lock()
                with self.assertRaises(ValueError):
                    self.fetch()
                self.assert_preserved()

    def test_downloaded_symlink_rejected(self):
        self.selection.write_bytes(self.payload)
        with self.assertRaisesRegex(ValueError, 'Symlink'):
            self.fetch(lambda _, path: path.symlink_to(self.selection))
        self.assertFalse(self.target.exists())
        self.assert_preserved()

    def test_unpinned_source_rejected_before_network(self):
        self.model['source'] = self.model['source'].replace(self.model['version'], 'main')
        self.save_lock()
        with self.assertRaisesRegex(ValueError, 'pinned HTTPS'):
            self.fetch(lambda *_: self.fail('Unexpected download'))
        self.assert_preserved()

    def test_manifest_conflict_rejected(self):
        conflicting = copy.deepcopy(self.model)
        conflicting['sha256'] = 'a' * 64
        self.manifest.write_text(json.dumps({'schemaVersion': 1, 'models': [conflicting]}))
        self.original = self.manifest.read_bytes()
        with self.assertRaisesRegex(ValueError, 'Conflicting manifest'):
            self.fetch(lambda *_: self.fail('Unexpected download'))
        self.assert_preserved()

    def test_manifest_changed_during_download_not_overwritten(self):
        changed = b'{"schemaVersion":1,"models":[],"new":"concurrent edit"}'
        def modify(_, path):
            path.write_bytes(self.payload)
            self.manifest.write_bytes(changed)
        with self.assertRaisesRegex(ValueError, 'changed during acquisition'):
            self.fetch(modify)
        self.assertEqual(self.manifest.read_bytes(), changed)
        self.assertFalse(self.target.exists())

    def test_manifest_replace_failure_rolls_back_existing_model(self):
        self.target.write_bytes(b'previous')
        real_replace = fetch.os.replace
        def fail_manifest(source, target):
            if Path(target) == self.manifest:
                raise OSError('simulated manifest write failure')
            return real_replace(source, target)
        with patch.object(fetch.os, 'replace', side_effect=fail_manifest):
            with self.assertRaisesRegex(OSError, 'simulated'):
                self.fetch()
        self.assertEqual(self.target.read_bytes(), b'previous')
        self.assert_preserved()

    def test_manifest_replace_failure_removes_new_model(self):
        real_replace = fetch.os.replace
        def fail_manifest(source, target):
            if Path(target) == self.manifest:
                raise OSError('simulated manifest write failure')
            return real_replace(source, target)
        with patch.object(fetch.os, 'replace', side_effect=fail_manifest):
            with self.assertRaises(OSError):
                self.fetch()
        self.assertFalse(self.target.exists())
        self.assert_preserved()

    def test_corrupt_cached_candidate_is_replaced_after_verification(self):
        self.target.write_bytes(b'old incomplete file')
        self.fetch()
        self.assertEqual(self.target.read_bytes(), self.payload)


if __name__ == '__main__':
    unittest.main(verbosity=2)
