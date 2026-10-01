#!/usr/bin/python3
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('safe_clean', ROOT / 'scripts/safe_clean.py')
clean = importlib.util.module_from_spec(spec)
spec.loader.exec_module(clean)

class CleanupTests(unittest.TestCase):
    def test_reject_root(self):
        with self.assertRaises(ValueError): clean.checked_target(ROOT, '.')

    def test_reject_escape(self):
        with self.assertRaises(ValueError): clean.checked_target(ROOT, '../other-project')

    def test_reject_absolute(self):
        with self.assertRaises(ValueError): clean.checked_target(ROOT, '/Library')

    def test_allow_only_descendant(self):
        self.assertEqual(clean.checked_target(ROOT, 'build/test'), ROOT / 'build/test')

    def test_reject_symlink_escape(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as fixture:
            (Path(fixture) / 'escape').symlink_to('/Library', target_is_directory=True)
            with self.assertRaises(ValueError): clean.checked_target(ROOT, str(Path(fixture).relative_to(ROOT) / 'escape'))

if __name__ == '__main__': unittest.main(verbosity=2)
