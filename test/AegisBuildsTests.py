"""Private build export integrity and remote visibility regression tests."""
from pathlib import Path
import json
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import aegis_builds as builds
import aegis_git as ag


class BuildsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.source = self.root / "input"
        self.source.mkdir()
        (self.source / "owned.exe").write_bytes(b"MZ-owned-build")
        self.output = self.root / "export"

    def tearDown(self):
        self.temp.cleanup()

    def prepare(self):
        return builds.prepare(self.output, ["phase1=" + str(self.source)])

    def test_exact_archive_and_source_preserved(self):
        result = self.prepare()
        self.assertEqual(result["packages"], 1)
        with zipfile.ZipFile(self.output / "phases/phase1.zip") as archive:
            self.assertEqual(archive.read("owned.exe"), (self.source / "owned.exe").read_bytes())
        self.assertEqual(len(builds.verify(self.output)), 3)
        with self.assertRaises(ag.Error):
            self.prepare()

    def test_tamper_and_extra_files_rejected(self):
        self.prepare()
        (self.output / "unlisted.exe").write_bytes(b"MZ")
        with self.assertRaises(ag.Error):
            builds.verify(self.output)
        (self.output / "unlisted.exe").unlink()
        with (self.output / "phases/phase1.zip").open("ab") as stream:
            stream.write(b"tampered")
        with self.assertRaises(ag.Error):
            builds.verify(self.output)

    def test_secret_rejected_before_export_without_value_in_error(self):
        secret = "gh" + "p_" + "x" * 36
        (self.source / "settings.txt").write_text(secret)
        with self.assertRaises(ag.Error) as error:
            self.prepare()
        self.assertNotIn(secret, str(error.exception))
        self.assertFalse(self.output.exists())

    def test_unsafe_label_and_nested_export_rejected(self):
        for label in ("../escape", "", "/absolute"):
            with self.assertRaises(ag.Error):
                builds.prepare(self.output, [label + "=" + str(self.source)])
        with self.assertRaises(ag.Error):
            builds.prepare(self.source / "nested", ["phase1=" + str(self.source)])

    def test_public_remote_refused_before_push(self):
        self.prepare()
        ag.git(self.output, "init", "-b", "main")
        ag.git(self.output, "config", "user.name", "Aegis test")
        ag.git(self.output, "config", "user.email", "test@example.invalid")
        api = Mock()
        api.request.return_value = {"private": False, "full_name": "owner/builds"}
        with patch.object(ag, "git", wraps=ag.git) as git:
            with self.assertRaisesRegex(ag.Error, "privado"):
                builds.publish(self.output, "owner/builds", api=api)
            self.assertFalse(any("push" in call.args for call in git.call_args_list))
        api.git_env.assert_not_called()


if __name__ == "__main__":
    unittest.main(verbosity=2)
