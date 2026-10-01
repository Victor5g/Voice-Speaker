#!/usr/bin/env python3
"""Exercise concurrent real sandbox launches and cleanup after publication failure."""
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import unittest


ROOT = Path(__file__).resolve().parent.parent
WRAPPER = ROOT / "scripts/project-local.sh"


class ProjectLocalTests(unittest.TestCase):
    def test_twenty_concurrent_real_sandbox_launches(self):
        gate = threading.Barrier(20)

        def launch(_):
            gate.wait(timeout=10)
            return subprocess.run(
                [str(WRAPPER), "/usr/bin/true"], cwd=ROOT,
                capture_output=True, text=True, timeout=30,
            )

        with ThreadPoolExecutor(max_workers=20) as pool:
            results = list(pool.map(launch, range(20)))
        for number, result in enumerate(results):
            self.assertEqual(result.returncode, 0, f"launch {number}: {result.stderr}")
        profile = (ROOT / "build/project-local.sb").read_text()
        self.assertTrue(profile.startswith("(version 1)\n"))
        self.assertEqual(profile.count("(version 1)"), 1)
        self.assertIn("(deny file-write*)\n", profile)

    def test_failed_publication_cleans_temp_and_preserves_previous_profile(self):
        # Inject only the shell's mv command via BASH_ENV in this subprocess.
        # No permissions, production files, system executables or PATH are changed.
        subprocess.run([str(WRAPPER), "/usr/bin/true"], cwd=ROOT, check=True, timeout=30)
        before = (ROOT / "build/project-local.sb").read_bytes()
        with tempfile.TemporaryDirectory(prefix="project-local-test-", dir=ROOT / "build") as folder:
            folder = Path(folder)
            setup = folder / "failure-env.sh"
            log = folder / "profile-path.txt"
            setup.write_text('mv() { printf \'%s\\n\' "$2" > "$LVT_TEST_TEMP_LOG"; return 73; }\n')
            environment = os.environ.copy()
            environment.update(BASH_ENV=str(setup), LVT_TEST_TEMP_LOG=str(log))
            result = subprocess.run(
                ["/bin/bash", str(WRAPPER), "/usr/bin/true"], cwd=ROOT,
                env=environment, capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(result.returncode, 73, result.stderr)
            temporary_profile = Path(log.read_text().strip())
            self.assertEqual(temporary_profile.parent, ROOT / "build")
            self.assertTrue(temporary_profile.name.startswith(".project-local.sb."))
            self.assertFalse(temporary_profile.exists(), "Failed publication left its profile behind")
        self.assertEqual((ROOT / "build/project-local.sb").read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
