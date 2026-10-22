"""Offline integration tests for publication, Git safety and revision generation."""
from pathlib import Path
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import aegis_git as ag
import aegis_sandbox_revisions as rev
import prepare_source_fixtures as fixtures


class ToolsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="aegis-tools-")
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def write(self, path, data):
        p = self.root / path
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data if isinstance(data, bytes) else data.encode())
        return p

    def init(self):
        ag.command(["git", "init", "-b", "main", self.root])
        ag.git(self.root, "config", "user.name", "Aegis test")
        ag.git(self.root, "config", "user.email", "test@example.invalid")

    def commit(self):
        ag.git(self.root, "add", "--all")
        ag.git(self.root, "commit", "-m", "owned test")

    def test_source_selection_excludes_all_builds_and_binaries(self):
        self.write("README.md", "Aegis")
        self.write("src/main.cpp", "int main(){}")
        for name in ("build/test.cpp", "build-phase13/test.cpp", "dist/test.py", ".env",
                     "test/program.exe", "test/library.dll", "test/image.gk", "test/lib.a",
                     "tools/__pycache__/generated.py", "src/build-local/test.cpp"):
            self.write(name, "excluded")
        self.assertEqual(ag.audit(self.root)["files"], 2)

    def test_disguised_executable_is_rejected(self):
        self.write("src/bad.cpp", b"MZ\0fake executable")
        with self.assertRaisesRegex(ag.Error, "Binário"):
            ag.audit(self.root)

    def test_secret_is_rejected_without_disclosing_value(self):
        secret = "gh" + "p_" + "x" * 36
        self.write("config/key.json", json.dumps({"key": secret}))
        with self.assertRaises(ag.Error) as e:
            ag.audit(self.root)
        self.assertNotIn(secret, str(e.exception))

    @unittest.skipUnless(hasattr(os, "symlink") and os.name != "nt", "Symlink support")
    def test_symlink_is_rejected(self):
        target = self.write("private.txt", "private")
        (self.root / "src").mkdir()
        (self.root / "src/link.cpp").symlink_to(target)
        with self.assertRaisesRegex(ag.Error, "Link"):
            ag.audit(self.root)

    def test_stage_real_git_and_preserve_commit_message(self):
        self.init()
        self.write("src/main.cpp", "int main(){}")
        self.write("build/main.cpp", "excluded")
        self.assertEqual(ag.stage_sources(self.root), 1)
        self.assertEqual(ag.git(self.root, "ls-files"), "src/main.cpp")
        message = 'literal $(do-not-run) `do-not-run` "quote"'
        ag.git(self.root, "commit", "-m", message)
        self.assertEqual(ag.git(self.root, "log", "-1", "--format=%B"), message)
        ag.check_publish(self.root)

    def test_tracked_binary_is_not_silently_uploaded(self):
        self.init()
        self.write("test/test.exe", b"MZpayload")
        self.commit()
        with self.assertRaisesRegex(ag.Error, "rastreados"):
            ag.stage_sources(self.root)

    def test_history_rejects_deleted_build_artifact(self):
        self.init()
        self.write("README.md", "owned")
        self.write("build/test.exe", b"MZfake")
        self.commit()
        ag.git(self.root, "rm", "build/test.exe")
        ag.git(self.root, "commit", "-m", "remove")
        with self.assertRaisesRegex(ag.Error, "Histórico"):
            ag.check_publish(self.root)

    def test_unsafe_remote_is_rejected(self):
        self.init()
        ag.git(self.root, "remote", "add", "origin", "https://example.invalid/a/b.git")
        with self.assertRaises(ag.Error):
            ag.github_repo(self.root)
        ag.git(self.root, "remote", "set-url", "origin", "https://github.com/owner/Aegis.git")
        self.assertEqual(ag.github_repo(self.root), "owner/Aegis")

    def test_api_pagination(self):
        api = ag.GitHub("test-token")
        with patch.object(api, "request", side_effect=[[{}] * 100, [{"name": "last"}]]) as request:
            self.assertEqual(len(api.pages("/user/repos?sort=updated")), 101)
            self.assertIn("&per_page=100&page=2", request.call_args.args[1])

    def test_creation_visibility_and_name_validation(self):
        api = ag.GitHub("test-token")
        with patch.object(api, "request", return_value={}) as request:
            api.create_repo("Aegis", True)
            self.assertFalse(request.call_args.args[2]["private"])
            self.assertFalse(request.call_args.args[2]["auto_init"])
            with self.assertRaises(ag.Error):
                api.create_repo("a/b")

    def test_credentials_never_appear_in_git_command_arguments(self):
        api = ag.GitHub("private-test-value")
        env = api.git_env()
        self.assertEqual(env["GIT_TERMINAL_PROMPT"], "0")
        self.assertNotIn("private-test-value", repr({k: v for k, v in env.items() if not k.startswith("GIT_CONFIG_VALUE_")}))

    def test_revision_generation_is_immutable_and_reproducible(self):
        first = rev.generate(self.root / "one", 42)
        data = Path(first["script"]).read_bytes()
        second = rev.generate(self.root / "one", 42)
        repeat = rev.generate(self.root / "two", 42)
        self.assertEqual(Path(first["script"]).read_bytes(), data)
        self.assertEqual(first["sha256"], repeat["sha256"])
        self.assertEqual(second["revision"], 2)
        compile(Path(second["script"]).read_text(), second["script"], "exec")
        with self.assertRaises(ValueError):
            rev.generate(self.root / "three", -1)

    def test_generated_suite_reports_missing_manager_failure(self):
        script = Path(rev.generate(self.root / "scripts", 1)["script"])
        output = self.root / "failed-run"
        p = subprocess.run([sys.executable, str(script), "--manager", str(self.root / "missing"),
                            "--kernel", str(self.root / "missing.gk"), "--output", str(output)],
                           capture_output=True, text=True)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn('"status": "FAIL"', p.stderr)
        self.assertEqual(json.loads((output / "result.json").read_text())["status"], "FAIL")

    def test_generated_suite_does_not_hide_denial_errors(self):
        spec = importlib.util.spec_from_file_location("sandbox_template", rev.TEMPLATE)
        template = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(template)
        bad = {"finished": True, "complete": True, "dropped": 0, "rejected": 0, "pending": 0,
               "events": [{"stage": "intent", "sequence": 1, "operationId": 1, "pid": 1,
                           "tid": 1, "actorPid": 1, "actorTid": 1, "domain": "file",
                           "operation": "write", "resource": "c:\\a.txt", "decision": "deny"}],
               "findings": []}
        bad["events"].append({**bad["events"][0], "stage": "result", "sequence": 2, "status": 0})
        with self.assertRaisesRegex(RuntimeError, "Recusa"):
            template.inspect_report(bad, "denied", "canary")

    def test_fresh_fixture_manifest_is_verified(self):
        guest = self.root / "guest"
        for name in ("guardian-pe-console.exe", "guardian-pe-runtime.dll", "guardian-kernel.gk"):
            self.write("guest/" + name, b"MZowned-parser-input")
        output = self.root / "fixtures"
        result = fixtures.prepare(guest, output)
        self.assertFalse(result["nativeWindowsReference"])
        import verify_native_fixtures
        verify_native_fixtures.verify(output)
        with self.assertRaises(FileExistsError):
            fixtures.prepare(guest, output)

    def test_subdirectory_cannot_commit_parent_repository(self):
        self.init()
        self.write("src/main.cpp", "owned")
        with self.assertRaisesRegex(ag.Error, "raiz"):
            ag.stage_sources(self.root / "src")

    def test_publish_prepares_real_commit_before_creating_remote(self):
        self.init()
        self.write("README.md", "Aegis")
        api = Mock()
        api.request.return_value = {"login": "owner"}
        def create(name, public):
            self.assertTrue(ag.git(self.root, "rev-parse", "HEAD"))
            self.assertTrue(public)
            return {"full_name": "owner/Aegis", "html_url": "https://github.com/owner/Aegis", "private": False}
        api.create_repo.side_effect = create
        with patch.object(ag, "push") as push:
            result = ag.publish(self.root, "owner/Aegis", create=True, public=True, api=api)
        self.assertTrue(result["public"])
        self.assertEqual(ag.github_repo(self.root), "owner/Aegis")
        push.assert_called_once_with(self.root, api)

    def test_publish_never_creates_remote_when_source_audit_fails(self):
        self.write("src/disguised.cpp", b"MZfake")
        api = Mock()
        with self.assertRaises(ag.Error):
            ag.publish(self.root, "owner/Aegis", create=True, public=True, api=api)
        api.create_repo.assert_not_called()


if __name__ == "__main__":
    unittest.main(verbosity=2)
