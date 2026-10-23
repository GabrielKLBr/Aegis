"""Native CLI behavior: real files, known SHA-256 answers and failure states."""
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tempfile

binary = pathlib.Path(sys.argv[1]).resolve()
checks = 0


def check(ok, message):
    global checks
    checks += 1
    if not ok:
        raise AssertionError(message)


with tempfile.TemporaryDirectory(prefix="guardian-linux-") as folder:
    root = pathlib.Path(folder)
    traps = root / "traps"
    traps.mkdir()
    sentinel = root / "windows-program-used"
    for name in ("wine", "wine64", "cmd.exe", "powershell.exe"):
        path = traps / name
        path.write_text('#!/bin/sh\nprintf used >> "' + str(sentinel) + '"\nexit 97\n')
        path.chmod(0o700)
    env = dict(os.environ, XDG_STATE_HOME=str(root / "state"), PATH=str(traps) + os.pathsep + os.environ.get("PATH", ""))

    def run(*args, code=0, as_json=True):
        result = subprocess.run([binary, *map(str, args)], cwd=root, env=env,
                                capture_output=True, text=True, timeout=10)
        check(result.returncode == code, f"Unexpected exit {result.returncode} for {args}: {result.stdout} {result.stderr}")
        return json.loads(result.stdout) if as_json else result

    check(binary.read_bytes()[:4] == b"\x7fELF", "The Linux program must be ELF")
    caps = run("--capabilities")
    check(caps["native"] and not caps["wine"] and caps["scanner"] and caps["vmExecution"] and caps["vmBackend"] == "KVM" and isinstance(caps["vmAvailable"], bool), "Capabilities must describe the native subset")
    run("--help", as_json=False)
    run("--version", as_json=False)
    run("sandbox", code=2, as_json=False)
    run("hash", code=2, as_json=False)
    run("scan", "--unknown", code=2, as_json=False)

    empty_db = root / "empty-signatures.txt"
    empty_db.write_text("# built-in rule only\n")
    target = root / "conteúdo com espaços 🔎.bin"
    for length in (0, 1, 55, 56, 63, 64, 65, 65535, 65536, 65537, 4 * 1024 * 1024 + 127):
        data = (bytes(range(256)) * ((length + 255) // 256))[:length]
        target.write_bytes(data)
        result = run("hash", target, "--json")
        check(result["sha256"] == hashlib.sha256(data).hexdigest() and result["bytes"] == length,
              "SHA-256 must agree with the independent Python/OpenSSL oracle at every block boundary")
        check(result["path"] == str(target), "UTF-8 paths must round-trip")

    target.write_bytes(b"abc")
    scan = run("scan", target, "--signatures", empty_db, "--json")
    check(scan["filesScanned"] == 1 and not scan["threatsFound"] and not scan["errors"], "Ordinary file should have no matches")
    check(scan["results"][0]["sha256"] == hashlib.sha256(b"abc").hexdigest(), "Scan must hash the actual file")
    database = root / "custom.txt"
    database.write_text("sha256:" + hashlib.sha256(b"abc").hexdigest().upper() + ":Hash.Test\n")
    scan = run("scan", target, "--signatures", database, "--json", code=1)
    check(scan["threatsFound"] == 1 and scan["results"][0]["threat"] == "Hash.Test", "Uppercase hash rule must match")

    database.write_text("substr:native-marker:Content.Test\n")
    target.write_bytes(b"a" * 65530 + b"native-marker")
    scan = run("scan", target, "--signatures", database, "--json", code=1)
    check(scan["threatsFound"] == 1, "Substring spanning read buffers must match")
    target.write_bytes(b"prefix-" + b"EICAR-STANDARD-" + b"ANTIVIRUS-" + b"TEST-FILE" + b"-suffix")
    scan = run("scan", target, "--signatures", empty_db, "--json", code=1)
    check(scan["results"][0]["threat"] == "EICAR-Test-File", "Built-in harmless EICAR fragment must be detected")

    # Default database is relative to the executable/install, not to cwd.
    target.write_bytes(b"GUARDIANAV-DEMO-" + b"MALWARE-MARKER")
    scan = run("scan", target, "--json", code=1)
    check(scan["results"][0]["threat"] == "Demo.TestMarker", "Default bundled database was not found")

    for text in ("sha256:abc:Bad", "substr::Empty", "unknown:abc:Bad", "sha256:" + "z" * 64 + ":Bad", "substr:x:\x1bBad", "substr:x:Bad\x00"):
        database.write_text(text)
        run("scan", target, "--signatures", database, "--json", code=2, as_json=False)
    run("scan", target, "--signatures", root / "missing-db", code=2, as_json=False)
    missing = run("scan", root / "missing-file", "--signatures", empty_db, "--json", code=3)
    check(missing["errors"] == 1 and missing["filesScanned"] == 0, "Unreadable input must never count as scanned")
    run("hash", root / "missing-file", code=3, as_json=False)
    empty_dir = root / "empty-dir"
    empty_dir.mkdir()
    scan = run("scan", empty_dir, "--signatures", empty_db, "--json")
    check(not scan["filesScanned"] and scan["results"] == [], "Empty directory must produce a complete empty report")
    escaped = root / "escape\x1b[31mand\x7fdelete"
    escaped.write_text("ordinary bytes")
    output = run("scan", escaped, "--signatures", empty_db, as_json=False)
    check("\x1b" not in output.stdout and "\x7f" not in output.stdout, "Filenames must not inject terminal control bytes")
    invalid_path = os.fsencode(root) + b"/invalid-\xff-name"
    with open(invalid_path, "wb") as stream:
        stream.write(b"ordinary")
    output = subprocess.run([os.fsencode(binary), b"scan", invalid_path, b"--signatures", os.fsencode(empty_db), b"--json"],
                            env=env, capture_output=True, timeout=10)
    check(output.returncode == 0 and len(json.loads(output.stdout)["results"]) == 1, "Non-UTF-8 filename bytes must not invalidate JSON")

    target.write_bytes(b"x" * (4 * 1024 * 1024) + b"native-marker")
    database.write_text("substr:native-marker:BeyondLimit\n")
    scan = run("scan", target, "--signatures", database, "--json", code=1)
    check(not scan["partial"] and scan["threatsFound"] == 1,
          "Phase 14 must inspect content beyond the old 4 MiB prefix")
    database.write_text("sha256:" + hashlib.sha256(target.read_bytes()).hexdigest() + ":WholeFile\n")
    scan = run("scan", target, "--signatures", database, "--json", code=1)
    check(scan["threatsFound"] == 1 and not scan["partial"], "Full-file hash match remains conclusive")

    tree = root / "tree"
    tree.mkdir()
    (tree / "clean").write_text("clean")
    (tree / 'quotes"and\nlines').write_text("another clean file")
    (tree / "sub").mkdir()
    (tree / "sub" / "match").write_text("native-marker")
    database.write_text("substr:native-marker:Recursive\n")
    before = {p: p.read_bytes() for p in tree.rglob("*") if p.is_file()}
    scan = run("scan", tree, "--signatures", database, "--json", code=1)
    check(scan["filesScanned"] == 3 and scan["threatsFound"] == 1, "Recursive scanner must visit each real file")
    check(all(p.read_bytes() == data for p, data in before.items()), "Scanner must preserve every input byte")
    (tree / "loop").symlink_to(tree, target_is_directory=True)
    (tree / "file-link").symlink_to(target)
    os.mkfifo(tree / "pipe")
    scan = run("scan", tree, "--signatures", database, "--json", code=3)
    check(scan["filesScanned"] == 3 and scan["summary"]["inconclusive"] == 3, "Symlinks and FIFOs must not be followed or block")
    run("hash", tree / "pipe", code=3, as_json=False)
    run("hash", tree / "file-link", code=3, as_json=False)
    run("hash", tree, code=3, as_json=False)
    run("scan", tree / "file-link", "--signatures", empty_db, "--json", code=3)
    special_db = root / "pipe-db"
    os.mkfifo(special_db)
    run("scan", target, "--signatures", special_db, code=2, as_json=False)
    if os.geteuid() != 0:
        blocked = root / "blocked"
        blocked.write_text("private")
        blocked.chmod(0)
        try:
            scan = run("scan", blocked, "--signatures", empty_db, "--json", code=3)
            check(scan["errors"] == 1, "Permission failure must be visible")
        finally:
            blocked.chmod(0o600)

    unusual = root / "--dash-name"
    unusual.write_bytes(b"dash")
    result = run("hash", "--json", "--", unusual.name)
    check(result["sha256"] == hashlib.sha256(b"dash").hexdigest(), "End-of-options paths must be accepted")
    image = pathlib.Path(__file__).parent / "fixtures/native/guardian-kernel.gk"
    result = run("inspect-kernel", image, "--json")
    check(result["valid"] and not result["executed"] and result["base"] == 0x200000, "Image inspection must parse without execution")
    run("inspect-kernel", target, "--json", code=3, as_json=False)
    check(not sentinel.exists(), "A Windows executable or Wine was invoked")
    print(f"{checks} native Linux scanner/CLI checks PASS; no Wine; input files preserved")
