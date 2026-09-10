"""Guard the argument order required by Apple's variable-length -verify_arch."""

import shlex
import unittest
from pathlib import Path


GITHUB = Path(__file__).resolve().parents[1]


class LipoInvocationTests(unittest.TestCase):
    def test_input_precedes_architecture_list(self):
        for relative in ("workflows/build.yml", "scripts/sign-macos-package.sh"):
            source = (GITHUB / relative).read_text().replace("\\\n", " ")
            invocations = [line.strip() for line in source.splitlines() if line.strip().startswith("lipo ")]
            self.assertTrue(invocations, f"No lipo check found in {relative}")
            for line in invocations:
                with self.subTest(file=relative, command=line):
                    args = shlex.split(line)
                    self.assertEqual(len(args), 4)
                    self.assertFalse(args[1].startswith("-"), "The input file must precede -verify_arch")
                    self.assertEqual(args[2], "-verify_arch")
                    self.assertIn(args[3], ("$arch", "$TARGET_ARCH", "x86_64", "arm64"))


if __name__ == "__main__":
    unittest.main()
