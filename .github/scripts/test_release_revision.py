"""Artifact labels use tags or commit IDs, never workspace dirtiness."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).with_name("release-revision.sh").resolve()


class ReleaseRevisionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="release revision ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.env = {key: value for key, value in os.environ.items()
                    if not key.startswith("GITHUB_") and key not in
                    ("K230_BURNING_REVISION", "GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE")}
        self.git("init", "-q")
        (self.root / "source").write_text("initial\n")
        self.git("add", "source")
        self.git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "-c", "commit.gpgsign=false", "commit", "-qm", "fixture")
        self.sha = self.git("rev-parse", "HEAD")

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.root), *args], env=self.env, text=True).strip()

    def revision(self, **values):
        result = subprocess.run(["bash", str(SCRIPT), str(self.root)],
                                env=dict(self.env, **values), capture_output=True, text=True, check=True)
        return result.stdout.strip(), result.stderr

    def test_commit_label_is_short_and_stable_when_dirty(self):
        self.assertEqual(self.revision()[0], self.sha[:12])
        (self.root / "source").write_text("modified\n")
        revision, warning = self.revision()
        self.assertEqual(revision, self.sha[:12])
        self.assertIn("tracked workspace changes", warning)

    def test_exact_local_tag(self):
        self.git("tag", "v1.2.3")
        self.assertEqual(self.revision()[0], "v1.2.3")

    def test_ci_tag_and_branch(self):
        self.assertEqual(self.revision(GITHUB_REF_TYPE="tag", GITHUB_REF_NAME="v2.3.4", GITHUB_SHA=self.sha)[0], "v2.3.4")
        self.assertEqual(self.revision(GITHUB_REF_TYPE="branch", GITHUB_REF_NAME="dev", GITHUB_SHA=self.sha)[0], self.sha[:12])

    def test_explicit_override_is_sanitized(self):
        self.assertEqual(self.revision(K230_BURNING_REVISION="customer/build 1\nlabel")[0], "customer-build-1-label")


if __name__ == "__main__":
    unittest.main()
