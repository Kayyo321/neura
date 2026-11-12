#!/usr/bin/env python3
"""Both research candidates and stable releases require main ancestry."""
import pathlib
import subprocess
import sys
import tempfile
import unittest

if len(sys.argv) > 1 and not sys.argv[1].startswith('-'):
    sys.argv.pop(1)  # CTest passes the compiler path uniformly.
CHECK = pathlib.Path(__file__).resolve().parents[2] / 'scripts/check-release-tag.py'

class ReleaseGate(unittest.TestCase):
    def test_release_requires_main_ancestry(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            def git(*args): return subprocess.run(['git', '-C', str(root), *args], check=True, capture_output=True, text=True).stdout.strip()
            git('init', '-b', 'main'); git('config', 'user.name', 'Release gate'); git('config', 'user.email', 'test@example.invalid')
            (root / 'VERSION').write_text('0.2.1\n')
            (root / 'CHANGELOG.md').write_text('# Changelog\n\n## [0.2.1] - 2026-11-12\n\nAdded tooling.\n')
            git('add', '.'); git('commit', '-m', 'base'); git('switch', '-c', 'tooling')
            (root / 'own').write_text('own change'); git('add', '.'); git('commit', '-m', 'tooling')
            def check(tag, *args): return subprocess.run([sys.executable, str(CHECK), tag, '--root', str(root), '--main', 'main', *args], capture_output=True, text=True)
            self.assertNotEqual(check('v0.2.1-rc.1').returncode, 0)
            self.assertNotEqual(check('v0.2.1').returncode, 0)
            parent = git('rev-parse', 'HEAD~1'); git('switch', '--detach', parent)
            # A main commit remains valid by the ordinary rule.
            self.assertEqual(check('v0.2.1-rc.1').returncode, 0)
            self.assertEqual(check('v0.2.1').returncode, 0)
            git('switch', '-c', 'unrelated')
            (root / 'unrelated').write_text('unrelated'); git('add', '.'); git('commit', '-m', 'unrelated')
            self.assertNotEqual(check('v0.2.1-rc.1').returncode, 0)
            git('switch', 'main'); git('merge', '--ff-only', 'tooling')
            self.assertEqual(check('v0.2.1-rc.1').returncode, 0)
            self.assertEqual(check('v0.2.1').returncode, 0)

if __name__ == '__main__': unittest.main()
