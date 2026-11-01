"""Private, manifest-verified phase build publication for aegis_git.py."""
from pathlib import Path
import hashlib
import json
import os
import re
import zipfile

import aegis_git as ag

LIMIT = 90 * 1024 * 1024
MANIFEST = "builds-manifest.json"


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def checked_files(path):
    """No links, credentials or user VM state, even in a private destination."""
    path = path.absolute()
    if not path.exists():
        raise ag.Error("Absent build: " + str(path))
    for parent in (path, *path.parents):
        if parent.is_symlink() or getattr(parent, "is_junction", lambda: False)():
            raise ag.Error("Links are not accepted in builds")
    candidates = [path]
    if path.is_dir():
        candidates = []
        for base, dirs, names in os.walk(path, followlinks=False):
            for name in dirs:
                child = Path(base) / name
                if child.is_symlink() or getattr(child, "is_junction", lambda: False)():
                    raise ag.Error("Link inside build")
            candidates.extend(Path(base) / name for name in names)
    for file in sorted(candidates):
        if file.is_symlink() or not file.is_file():
            raise ag.Error("Irregular build file")
        if (file.name.startswith(".env") or file.suffix.lower() in
                {".pem", ".key", ".vhd", ".vhdx", ".qcow", ".qcow2", ".iso", ".dmp"}
                or file.name.lower().startswith("credentials")):
            raise ag.Error("Private/VM-state file not accepted: " + file.name)
        # Overlap keeps a token split between reads detectable.
        tail = b""
        with file.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                if ag.SECRET.search(tail + block):
                    raise ag.Error("Possible credential on build: " + file.name)
                tail = block[-4096:]
        yield file


def prepare(output, entries):
    output = output.absolute()
    if output.exists():
        raise ag.Error("Use a new export folder")
    if not entries:
        raise ag.Error("Select at least one build")
    plan = []
    labels = set()
    for entry in entries:
        label, separator, value = entry.partition("=")
        if not separator or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", label) or label in labels:
            raise ag.Error("Input shall be LABEL=CAME, without repeated labels")
        labels.add(label)
        source = Path(value).absolute()
        if output == source or source in output.parents:
            raise ag.Error("Export cannot stay inside the build")
        files = list(checked_files(source))
        if not files:
            raise ag.Error("Empty build: " + label)
        plan.append((label, source, files))
    output.mkdir(parents=True)
    artifacts = []
    for label, source, files in plan:
        target = output / "phases" / (label + ".zip")
        target.parent.mkdir(exist_ok=True)
        members = {}
        with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for file in files:
                name = file.relative_to(source).as_posix() if source.is_dir() else file.name
                sha = digest(file)
                archive.write(file, name)
                # Catch changes during copying; do not publish a mixed build.
                if digest(file) != sha:
                    raise ag.Error("Build changed during export")
                members[name] = {"sha256": sha, "bytes": file.stat().st_size}
        with zipfile.ZipFile(target) as archive:
            if archive.testzip() is not None:
                raise ag.Error("Corrupt build file")
            for name, record in members.items():
                if hashlib.sha256(archive.read(name)).hexdigest() != record["sha256"]:
                    raise ag.Error("Exported content differs from the original")
        if target.stat().st_size > LIMIT:
            raise ag.Error("Package exceeds 90 MiB; separate selection into smaller entries")
        artifacts.append({"path": target.relative_to(output).as_posix(), "sha256": digest(target),
                          "bytes": target.stat().st_size, "members": members})
    (output / MANIFEST).write_text(json.dumps({"schema": 1, "privateOnly": True,
        "artifacts": artifacts}, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (output / "README.md").write_text(
        "# Aegis — historical builds\n\nPrivate repository of phase build artifacts, without GitHub Releases.\n"
        "Full Sources: https://github.com/GabrielKLBr/Aegis\n\n"
        "Each ZIP preserves the structure of the original selection. The manifest records SHA-256 "
        "from each package and internal file. Extract into a new folder for use.\n\n"
        "They are preserved builds, including historical development configurations; "
        "the presence here does not declare a new validation or compatibility between phases. "
        "Compilation caches, personal disks and credentials are not part of the deliveries.\n",
        encoding="utf-8")
    return {"output": str(output), "packages": len(artifacts),
            "bytes": sum(a["bytes"] for a in artifacts)}


def verify(root):
    manifest = json.loads((root / MANIFEST).read_text(encoding="utf-8"))
    if manifest.get("schema") != 1 or manifest.get("privateOnly") is not True:
        raise ag.Error("Invalid private manifest")
    expected = {MANIFEST, "README.md"}
    for artifact in manifest["artifacts"]:
        name = artifact["path"]
        if not re.fullmatch(r"phases/[A-Za-z0-9][A-Za-z0-9_.-]*\.zip", name) or name in expected:
            raise ag.Error("Invalid package path")
        expected.add(name)
        file = root / name
        if file.is_symlink() or file.stat().st_size > LIMIT or digest(file) != artifact["sha256"]:
            raise ag.Error("Package differs from manifesto")
    actual = {p.relative_to(root).as_posix() for p in root.rglob("*")
              if p.is_file() and ".git" not in p.relative_to(root).parts}
    if actual != expected:
        raise ag.Error("Export contains unscheduled files")
    return expected


def publish(root, repo, create=False, api=None):
    repo = ag.validate_repo(repo)
    expected = verify(root)
    if not (root / ".git").exists():
        ag.git(root, "init", "-b", "main")
    ag.require_repository_root(root)
    remotes = ag.git(root, "remote").splitlines()
    if "origin" in remotes and ag.github_repo(root).lower() != repo.lower():
        raise ag.Error("Remote destination other than requested")
    tracked = set(filter(None, ag.git(root, "ls-files", "-z").split("\0")))
    if tracked - expected:
        raise ag.Error("Contents contains file outside the manifest")
    ag.git(root, "add", "--", *sorted(expected))
    if ag.git(root, "diff", "--cached", "--name-only"):
        ag.git(root, "commit", "-m", "Historical phase builds preservation with SHA-256 manifesto")
    # Historical trees must contain only inventoried build exports too.
    for commit in ag.git(root, "rev-list", "HEAD").splitlines():
        historical = json.loads(ag.git(root, "show", f"{commit}:{MANIFEST}"))
        allowed = {MANIFEST, "README.md", *[x["path"] for x in historical["artifacts"]]}
        names = set(filter(None, ag.git(root, "ls-tree", "-rz", "--name-only", commit).split("\0")))
        if names != allowed:
            raise ag.Error("History contains file outside the manifest")
    api = api or ag.GitHub()
    if create:
        owner, name = repo.split("/")
        if api.request("GET", "/user")["login"].lower() != owner.lower():
            raise ag.Error("Account authenticated other than destination")
        row = api.create_repo(name, public=False)
    else:
        row = api.request("GET", "/repos/" + repo)
    if row.get("private") is not True or row.get("full_name", "").lower() != repo.lower():
        raise ag.Error("Builds can only be sent to the requested private repository")
    if "origin" not in remotes:
        ag.git(root, "remote", "add", "origin", "https://github.com/" + repo + ".git")
    # Recheck visibility directly before sending. Never change repository visibility.
    if api.request("GET", "/repos/" + repo).get("private") is not True:
        raise ag.Error("Destiny is no longer private.")
    branch = ag.git(root, "symbolic-ref", "--short", "HEAD")
    ag.git(root, "push", "--set-upstream", "origin", branch, env=api.git_env())
    return {"url": row["html_url"], "private": True, "packages": len(expected) - 2,
            "commit": ag.git(root, "rev-parse", "HEAD")}
