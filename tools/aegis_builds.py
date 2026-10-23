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
        raise ag.Error("Build ausente: " + str(path))
    for parent in (path, *path.parents):
        if parent.is_symlink() or getattr(parent, "is_junction", lambda: False)():
            raise ag.Error("Links não são aceitos nas builds")
    candidates = [path]
    if path.is_dir():
        candidates = []
        for base, dirs, names in os.walk(path, followlinks=False):
            for name in dirs:
                child = Path(base) / name
                if child.is_symlink() or getattr(child, "is_junction", lambda: False)():
                    raise ag.Error("Link dentro da build")
            candidates.extend(Path(base) / name for name in names)
    for file in sorted(candidates):
        if file.is_symlink() or not file.is_file():
            raise ag.Error("Arquivo irregular na build")
        if (file.name.startswith(".env") or file.suffix.lower() in
                {".pem", ".key", ".vhd", ".vhdx", ".qcow", ".qcow2", ".iso", ".dmp"}
                or file.name.lower().startswith("credentials")):
            raise ag.Error("Arquivo privado/estado de VM não aceito: " + file.name)
        # Overlap keeps a token split between reads detectable.
        tail = b""
        with file.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                if ag.SECRET.search(tail + block):
                    raise ag.Error("Possível credencial na build: " + file.name)
                tail = block[-4096:]
        yield file


def prepare(output, entries):
    output = output.absolute()
    if output.exists():
        raise ag.Error("Use uma pasta de exportação nova")
    if not entries:
        raise ag.Error("Selecione pelo menos uma build")
    plan = []
    labels = set()
    for entry in entries:
        label, separator, value = entry.partition("=")
        if not separator or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", label) or label in labels:
            raise ag.Error("Entrada deve ser ROTULO=CAMINHO, sem rótulos repetidos")
        labels.add(label)
        source = Path(value).absolute()
        if output == source or source in output.parents:
            raise ag.Error("Exportação não pode ficar dentro da build")
        files = list(checked_files(source))
        if not files:
            raise ag.Error("Build vazia: " + label)
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
                    raise ag.Error("Build alterada durante a exportação")
                members[name] = {"sha256": sha, "bytes": file.stat().st_size}
        with zipfile.ZipFile(target) as archive:
            if archive.testzip() is not None:
                raise ag.Error("Arquivo de build corrompido")
            for name, record in members.items():
                if hashlib.sha256(archive.read(name)).hexdigest() != record["sha256"]:
                    raise ag.Error("Conteúdo exportado diverge do original")
        if target.stat().st_size > LIMIT:
            raise ag.Error("Pacote excede 90 MiB; separe a seleção em entradas menores")
        artifacts.append({"path": target.relative_to(output).as_posix(), "sha256": digest(target),
                          "bytes": target.stat().st_size, "members": members})
    (output / MANIFEST).write_text(json.dumps({"schema": 1, "privateOnly": True,
        "artifacts": artifacts}, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (output / "README.md").write_text(
        "# Aegis — builds históricas\n\nRepositório privado de artefatos, sem GitHub Releases.\n"
        "Fontes completos: https://github.com/GabrielKLBr/Aegis\n\n"
        "Cada ZIP preserva a estrutura da seleção original. O manifesto registra SHA-256 "
        "de cada pacote e arquivo interno. Extraia em uma pasta nova para usar.\n\n"
        "São builds preservadas, inclusive configurações históricas de desenvolvimento; "
        "a presença aqui não declara uma nova validação nem compatibilidade entre fases. "
        "Caches de compilação, discos pessoais e credenciais não fazem parte das entregas.\n",
        encoding="utf-8")
    return {"output": str(output), "packages": len(artifacts),
            "bytes": sum(a["bytes"] for a in artifacts)}


def verify(root):
    manifest = json.loads((root / MANIFEST).read_text(encoding="utf-8"))
    if manifest.get("schema") != 1 or manifest.get("privateOnly") is not True:
        raise ag.Error("Manifesto privado inválido")
    expected = {MANIFEST, "README.md"}
    for artifact in manifest["artifacts"]:
        name = artifact["path"]
        if not re.fullmatch(r"phases/[A-Za-z0-9][A-Za-z0-9_.-]*\.zip", name) or name in expected:
            raise ag.Error("Caminho de pacote inválido")
        expected.add(name)
        file = root / name
        if file.is_symlink() or file.stat().st_size > LIMIT or digest(file) != artifact["sha256"]:
            raise ag.Error("Pacote diverge do manifesto")
    actual = {p.relative_to(root).as_posix() for p in root.rglob("*")
              if p.is_file() and ".git" not in p.relative_to(root).parts}
    if actual != expected:
        raise ag.Error("Exportação contém arquivos não inventariados")
    return expected


def publish(root, repo, create=False, api=None):
    repo = ag.validate_repo(repo)
    expected = verify(root)
    if not (root / ".git").exists():
        ag.git(root, "init", "-b", "main")
    ag.require_repository_root(root)
    remotes = ag.git(root, "remote").splitlines()
    if "origin" in remotes and ag.github_repo(root).lower() != repo.lower():
        raise ag.Error("Destino remoto diferente do solicitado")
    tracked = set(filter(None, ag.git(root, "ls-files", "-z").split("\0")))
    if tracked - expected:
        raise ag.Error("Índice contém arquivo fora do manifesto")
    ag.git(root, "add", "--", *sorted(expected))
    if ag.git(root, "diff", "--cached", "--name-only"):
        ag.git(root, "commit", "-m", "Preserva builds históricas por fase com manifesto SHA-256")
    # Historical trees must contain only inventoried build exports too.
    for commit in ag.git(root, "rev-list", "HEAD").splitlines():
        historical = json.loads(ag.git(root, "show", f"{commit}:{MANIFEST}"))
        allowed = {MANIFEST, "README.md", *[x["path"] for x in historical["artifacts"]]}
        names = set(filter(None, ag.git(root, "ls-tree", "-rz", "--name-only", commit).split("\0")))
        if names != allowed:
            raise ag.Error("Histórico contém arquivo fora do manifesto")
    api = api or ag.GitHub()
    if create:
        owner, name = repo.split("/")
        if api.request("GET", "/user")["login"].lower() != owner.lower():
            raise ag.Error("Conta autenticada diferente do destino")
        row = api.create_repo(name, public=False)
    else:
        row = api.request("GET", "/repos/" + repo)
    if row.get("private") is not True or row.get("full_name", "").lower() != repo.lower():
        raise ag.Error("Builds só podem ser enviadas ao repositório privado solicitado")
    if "origin" not in remotes:
        ag.git(root, "remote", "add", "origin", "https://github.com/" + repo + ".git")
    # Recheck visibility directly before sending. Never change repository visibility.
    if api.request("GET", "/repos/" + repo).get("private") is not True:
        raise ag.Error("Destino deixou de ser privado")
    branch = ag.git(root, "symbolic-ref", "--short", "HEAD")
    ag.git(root, "push", "--set-upstream", "origin", branch, env=api.git_env())
    return {"url": row["html_url"], "private": True, "packages": len(expected) - 2,
            "commit": ag.git(root, "rev-parse", "HEAD")}
