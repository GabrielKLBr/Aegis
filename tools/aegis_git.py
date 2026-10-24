#!/usr/bin/env python3
"""Aegis: interactive Git/GitHub assistant; Python 3.10+, Git, no pip packages."""
from __future__ import annotations

import argparse
import base64
import getpass
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
DESCRIPTION = "Advanced Endpoint Guardian for Inspection & Sandboxing"
SOURCE_DIRS = {"src", "shared", "guest", "test", "cmake", "tools", "docs", "config", ".github"}
ROOT_FILES = {".gitignore", ".gitattributes", "AGENTS.md", "CMakeLists.txt", "README.md", "SECURITY.md",
              "LICENSE", "LICENSE.md", "signatures.txt", "build.bat", "build-mingw.bat",
              "Build-Kernel.ps1", "Build-Linux.sh"}
SOURCE_EXTENSIONS = {".c", ".cpp", ".h", ".hpp", ".inl", ".inc", ".asm", ".s", ".py",
                     ".md", ".txt", ".json", ".cmake", ".def", ".inf", ".rc", ".manifest",
                     ".ps1", ".sh", ".bat", ".yml", ".yaml", ".svg", ".png"}
SECRET = re.compile(rb"(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|"
                    rb"AKIA[A-Z0-9]{16}|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----)")


class Error(RuntimeError):
    pass


def command(args, cwd=None, env=None, input_text=None):
    p = subprocess.run(list(map(str, args)), cwd=cwd, env=env, input=input_text,
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    if p.returncode:
        # Do not include command arguments: credentials never belong in diagnostics.
        raise Error(p.stderr.strip() or p.stdout.strip() or "Falha ao executar a ferramenta")
    return p.stdout.strip()


def git(root, *args, env=None, input_text=None):
    return command(["git", "-C", str(root), *args], env=env, input_text=input_text)


def source_files(root):
    """Allow source/assets explicitly; never traverse build or local state directories."""
    root = root.resolve()
    files = []
    for name in sorted(ROOT_FILES):
        if (root / name).is_file():
            files.append(root / name)
    for directory in sorted(SOURCE_DIRS):
        for parent, dirs, names in os.walk(root / directory, followlinks=False):
            dirs[:] = sorted(d for d in dirs if not d.startswith(("build", "."))
                             and d not in {"__pycache__", "dist", "out", "releases", "snapshots"}
                             and not (Path(parent) / d).is_symlink())
            for name in sorted(names):
                p = Path(parent) / name
                if p.suffix.lower() in SOURCE_EXTENSIONS and not name.startswith(".env"):
                    files.append(p)
    for p in files:
        if p.is_symlink() or not p.resolve().is_relative_to(root):
            raise Error("Link de fonte recusado: " + str(p.relative_to(root)))
        data = p.read_bytes()
        if data[:2] == b"MZ" or data[:4] in (b"\x7fELF", b"PK\x03\x04", b"!<ar"):
            raise Error("Binário disfarçado de fonte: " + str(p.relative_to(root)))
        if p.suffix.lower() != ".png":
            try:
                data.decode("utf-8-sig")
            except UnicodeDecodeError as e:
                raise Error("Arquivo não textual: " + str(p.relative_to(root))) from e
            if b"\0" in data or SECRET.search(data):
                raise Error("Possível segredo ou binário em: " + str(p.relative_to(root)))
        elif not data.startswith(b"\x89PNG\r\n\x1a\n"):
            raise Error("Imagem PNG inválida: " + str(p.relative_to(root)))
    return sorted(files)


def audit(root):
    files = source_files(root)
    return {"files": len(files), "buildsIncluded": False, "binariesIncluded": False,
            "sources": {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in files}}


def validate_repo(name):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", name) or ".." in name:
        raise Error("Use o formato dono/repositorio")
    return name


def github_repo(root):
    url = git(root, "remote", "get-url", "origin")
    match = re.fullmatch(r"(?:https://github\.com/|git@github\.com:)([^/]+/[^/]+?)(?:\.git)?", url)
    if not match:
        raise Error("origin deve apontar para github.com, sem credenciais na URL")
    return validate_repo(match[1])


def require_repository_root(root):
    if Path(git(root, "rev-parse", "--show-toplevel")).resolve() != root.resolve():
        raise Error("Selecione a raiz do repositório, não uma subpasta")


def token_from_system():
    for name in ("GH_TOKEN", "GITHUB_TOKEN"):
        if os.environ.get(name):
            return os.environ[name]
    if shutil.which("gh"):
        p = subprocess.run(["gh", "auth", "token", "--hostname", "github.com"],
                           capture_output=True, text=True)
        if p.returncode == 0 and p.stdout.strip():
            return p.stdout.strip()
    env = {**os.environ, "GIT_TERMINAL_PROMPT": "0"}
    p = subprocess.run(["git", "credential", "fill"], input="protocol=https\nhost=github.com\n\n",
                       env=env, capture_output=True, text=True, timeout=20)
    values = dict(line.split("=", 1) for line in p.stdout.splitlines() if "=" in line)
    return values.get("password", "")


class NoRedirect(urllib.request.HTTPRedirectHandler):
    # Never forward Authorization to a different host through a redirect.
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class GitHub:
    def __init__(self, token=None):
        self.token = token if token is not None else token_from_system()
        if not self.token:
            raise Error("Autentique com gh auth login ou defina GH_TOKEN nesta sessão. "
                        "No menu, a opção de autenticação também aceita token em memória.")
        self.opener = urllib.request.build_opener(NoRedirect)

    def request(self, method, path, data=None):
        if not path.startswith("/") or path.startswith("//"):
            raise Error("Caminho de API inválido")
        payload = None if data is None else json.dumps(data).encode("utf-8")
        req = urllib.request.Request("https://api.github.com" + path, data=payload, method=method,
                                     headers={"Authorization": "Bearer " + self.token,
                                              "Accept": "application/vnd.github+json",
                                              "Content-Type": "application/json",
                                              "X-GitHub-Api-Version": "2026-03-10",
                                              "User-Agent": "Aegis-source-assistant"})
        try:
            with self.opener.open(req, timeout=45) as response:
                body = response.read()
                return json.loads(body) if body else None
        except urllib.error.HTTPError as e:
            try:
                message = json.loads(e.read()).get("message", "Falha da API")
            except (ValueError, UnicodeError):
                message = "Falha da API"
            raise Error(f"GitHub HTTP {e.code}: {message}") from None

    def pages(self, path):
        rows = []
        for page in range(1, 101):
            sep = "&" if "?" in path else "?"
            part = self.request("GET", f"{path}{sep}per_page=100&page={page}")
            rows.extend(part)
            if len(part) < 100:
                return rows
        raise Error("Limite de paginação atingido; restrinja a consulta")

    def git_env(self):
        env = {**os.environ, "GIT_TERMINAL_PROMPT": "0"}
        count = int(env.get("GIT_CONFIG_COUNT", "0"))
        auth = base64.b64encode(("x-access-token:" + self.token).encode()).decode()
        env.update({"GIT_CONFIG_COUNT": str(count + 1),
                    f"GIT_CONFIG_KEY_{count}": "http.https://github.com/.extraheader",
                    f"GIT_CONFIG_VALUE_{count}": "AUTHORIZATION: basic " + auth})
        return env

    def create_repo(self, name, public=False):
        if not re.fullmatch(r"[A-Za-z0-9_.-]+", name) or name.startswith("-"):
            raise Error("Nome de repositório inválido")
        return self.request("POST", "/user/repos", {"name": name, "description": DESCRIPTION,
                            "private": not public, "auto_init": False, "has_wiki": False})


def stage_sources(root):
    require_repository_root(root)
    files = source_files(root)
    allowed = {p.relative_to(root).as_posix() for p in files}
    # Existing tracked binary/build files must be removed explicitly before publication.
    tracked = set(git(root, "ls-files", "-z").split("\0")) - {""}
    forbidden = sorted(n for n in tracked if n not in allowed and (root / n).exists())
    if forbidden:
        raise Error("Há arquivos rastreados fora dos fontes; revise com git rm --cached: "
                    + ", ".join(forbidden[:8]))
    for start in range(0, len(files), 80):
        git(root, "add", "--", *[str(p.relative_to(root)) for p in files[start:start + 80]])
    removed = [n for n in tracked if not (root / n).exists()]
    if removed:
        git(root, "add", "-u", "--", *removed)
    return len(files)


def check_publish(root):
    require_repository_root(root)
    allowed = {p.relative_to(root).as_posix() for p in source_files(root)}
    # Audit every reachable commit on this branch so deleted builds cannot hide in history.
    commits = git(root, "rev-list", "HEAD").splitlines()
    for commit in commits:
        names = git(root, "ls-tree", "-rz", "--name-only", commit).split("\0")
        for name in filter(None, names):
            parts = Path(name).parts
            permitted = (len(parts) == 1 and name in ROOT_FILES) or (
                parts[0] in SOURCE_DIRS and Path(name).suffix.lower() in SOURCE_EXTENSIONS
                and not any(p.startswith("build") or p in {"__pycache__", "dist", "out"}
                            for p in parts[1:-1]))
            if not permitted:
                raise Error("Histórico contém arquivo excluído da publicação: " + name)
            data = subprocess.run(["git", "-C", str(root), "show", f"{commit}:{name}"],
                                  check=True, capture_output=True).stdout
            if (data[:2] == b"MZ" or data[:4] in (b"\x7fELF", b"PK\x03\x04", b"!<ar")
                    or (Path(name).suffix != ".png" and (b"\0" in data or SECRET.search(data)))):
                raise Error("Histórico contém binário/possível segredo: " + name)
    tracked = set(filter(None, git(root, "ls-files", "-z").split("\0")))
    if tracked - allowed:
        raise Error("Índice contém arquivos fora dos fontes")


def push(root, api):
    github_repo(root)
    check_publish(root)
    branch = git(root, "symbolic-ref", "--short", "HEAD")
    git(root, "push", "--set-upstream", "origin", branch, env=api.git_env())


def publish(root, repo, create=False, public=False, message="Publica os fontes do Aegis", api=None):
    """Prepare and review locally before creating a remote or sending anything."""
    repo = validate_repo(repo)
    files = source_files(root)
    if not files:
        raise Error("Nenhum fonte encontrado")
    p = subprocess.run(["git", "-C", str(root), "rev-parse", "--show-toplevel"],
                       capture_output=True, text=True)
    if p.returncode:
        git(root, "init", "-b", "main")
        git(root, "config", "core.filemode", "false")
    require_repository_root(root)
    remotes = git(root, "remote").splitlines()
    if "origin" in remotes and github_repo(root).lower() != repo.lower():
        raise Error("origin aponta para outro repositório; selecione a pasta correta")
    stage_sources(root)
    changed = git(root, "diff", "--cached", "--name-only")
    if changed:
        print(git(root, "diff", "--cached", "--stat"), flush=True)
        git(root, "commit", "-m", message)
    check_publish(root)
    api = api or GitHub()
    if create:
        account, name = repo.split("/")
        if api.request("GET", "/user")["login"].lower() != account.lower():
            raise Error("A criação automática utiliza a conta autenticada, não outra conta/organização")
        row = api.create_repo(name, public)
        if row["full_name"].lower() != repo.lower():
            raise Error("Repositório criado não corresponde ao destino")
    else:
        row = api.request("GET", "/repos/" + repo)
    if "origin" not in remotes:
        git(root, "remote", "add", "origin", "https://github.com/" + repo + ".git")
    push(root, api)
    return {"repository": repo, "url": row["html_url"], "public": not row["private"],
            "branch": git(root, "symbolic-ref", "--short", "HEAD"),
            "commit": git(root, "rev-parse", "HEAD"), "sourceFiles": len(files)}


def choose(rows, label):
    if not rows:
        raise Error("Nenhuma opção disponível")
    for i, row in enumerate(rows, 1):
        print(f"{i}. {label(row)}")
    try:
        n = int(input("Número (0 cancela): "))
    except ValueError:
        raise Error("Selecione um número") from None
    if n == 0:
        return None
    if not 1 <= n <= len(rows):
        raise Error("Opção fora da lista")
    return rows[n - 1]


def menu(root):
    api = None
    while True:
        print(f"\nAegis — {root}\n1 Status e diferenças\n2 Selecionar repositório/clonar"
              "\n3 Criar repositório\n4 Selecionar/criar branch\n5 Atualizar (fast-forward)"
              "\n6 Revisar e fazer commit dos fontes\n7 Enviar branch\n8 Listar/abrir PR"
              "\n9 Criar PR em rascunho\n10 Gerar revisão de testes do sandbox"
              "\n11 Auditar fontes\n12 Autenticar\n13 Publicar esta pasta em outro repositório\n0 Sair")
        option = input("Opção: ").strip()
        try:
            if option == "0":
                return
            if option == "12":
                api = GitHub(getpass.getpass("Token GitHub (somente nesta sessão): ").strip())
                print("Conta:", api.request("GET", "/user")["login"])
            elif option == "13":
                repo = input("Destino dono/repositorio: ").strip()
                create = input("Criar esse repositório agora? [s/N]: ").lower() == "s"
                public = create and input("Visibilidade [privado/publico]: ").lower() == "publico"
                message = input("Mensagem do commit: ").strip() or "Atualiza fontes do Aegis"
                print(json.dumps(publish(root, repo, create, public, message, api=api), indent=2))
            elif option == "1":
                print(git(root, "status", "--short", "--branch"))
                print(git(root, "diff", "--stat"))
                print(git(root, "diff", "--cached", "--stat"))
            elif option in {"2", "3", "5", "7", "8", "9"}:
                api = api or GitHub()
                if option == "2":
                    rows = api.pages("/user/repos?sort=updated&affiliation=owner,collaborator,organization_member")
                    row = choose(rows, lambda r: r["full_name"])
                    if row:
                        target = Path(input("Pasta local existente ou nova para clone: ")).expanduser().resolve()
                        if not target.exists():
                            command(["git", "clone", "--", row["clone_url"], target], env=api.git_env())
                        if github_repo(target).lower() != row["full_name"].lower():
                            raise Error("A pasta aponta para outro repositório")
                        root = target
                elif option == "3":
                    name = input("Nome do novo repositório: ").strip()
                    public = input("Visibilidade [privado/publico]: ").strip().lower() == "publico"
                    print(api.create_repo(name, public)["html_url"])
                    print("Selecione-o na opção 2 para clonar. Fontes locais não foram movidos.")
                elif option == "5":
                    github_repo(root)
                    print(git(root, "pull", "--ff-only", env=api.git_env()))
                elif option == "7":
                    push(root, api)
                    print("Branch enviada.")
                elif option == "8":
                    repo = github_repo(root)
                    row = choose(api.pages(f"/repos/{repo}/pulls?state=open"),
                                 lambda r: f"#{r['number']} {r['title']}")
                    if row:
                        print(row["html_url"])
                        if input("Obter essa PR em uma branch local? [s/N]: ").lower() == "s":
                            if git(root, "status", "--porcelain"):
                                raise Error("Salve as mudanças locais antes de trocar de branch")
                            branch = "pr/" + str(row["number"])
                            git(root, "fetch", "origin", f"pull/{row['number']}/head:{branch}", env=api.git_env())
                            git(root, "switch", branch)
                elif option == "9":
                    repo = github_repo(root)
                    info = api.request("GET", f"/repos/{repo}")
                    base = input(f"Branch de destino [{info['default_branch']}]: ").strip() or info["default_branch"]
                    head = git(root, "symbolic-ref", "--short", "HEAD")
                    title = input("Título da PR: ").strip()
                    body = input("Descrição da mudança: ").strip()
                    if not title or head == base:
                        raise Error("Use título e branch de trabalho diferente do destino")
                    push(root, api)
                    row = api.request("POST", f"/repos/{repo}/pulls", {
                        "title": title, "body": body, "head": head, "base": base, "draft": True})
                    print(row["html_url"])
            elif option == "4":
                rows = git(root, "for-each-ref", "--format=%(refname:short)", "refs/heads", "refs/remotes/origin").splitlines()
                branch = choose(["[Criar branch]", *[r for r in rows if not r.endswith("/HEAD")]], str)
                if branch:
                    if git(root, "status", "--porcelain"):
                        raise Error("Salve as mudanças locais antes de trocar de branch")
                    if branch == "[Criar branch]":
                        name = input("Nome: ").strip()
                        git(root, "check-ref-format", "--branch", name)
                        print(git(root, "switch", "-c", name))
                    elif branch.startswith("origin/"):
                        print(git(root, "switch", "--track", branch))
                    else:
                        print(git(root, "switch", branch))
            elif option == "6":
                print(f"{stage_sources(root)} fontes selecionados.")
                print(git(root, "diff", "--cached", "--stat"))
                print(git(root, "diff", "--cached"))
                message = input("Mensagem do commit (vazio cancela): ").strip()
                if not message:
                    print("Commit cancelado; seleção preservada para revisão.")
                else:
                    print(git(root, "commit", "-m", message))
            elif option == "10":
                path = root / "tools/aegis_sandbox_revisions.py"
                print(command([sys.executable, path, "generate", "--output", root / "tools/sandbox_revisions"]))
            elif option == "11":
                proof = audit(root)
                print(json.dumps({k: v for k, v in proof.items() if k != "sources"}, indent=2))
            else:
                print("Opção desconhecida.")
        except (Error, OSError, ValueError, subprocess.SubprocessError) as e:
            print("Erro:", e)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    commands = parser.add_subparsers(dest="action")
    commands.add_parser("audit")
    commands.add_parser("status")
    commands.add_parser("stage")
    create = commands.add_parser("create-repo")
    create.add_argument("name")
    create.add_argument("--public", action="store_true")
    commands.add_parser("push")
    builds = commands.add_parser("prepare-builds", help="Exportar builds para uma pasta nova")
    builds.add_argument("--output", type=Path, required=True)
    builds.add_argument("--entry", action="append", required=True, help="ROTULO=CAMINHO")
    builds_push = commands.add_parser("publish-builds", help="Enviar exportação somente a destino privado")
    builds_push.add_argument("--repo", required=True)
    builds_push.add_argument("--create", action="store_true")
    publish_parser = commands.add_parser("publish", help="Auditar, selecionar fontes, commit e push")
    publish_parser.add_argument("--repo", required=True)
    publish_parser.add_argument("--create", action="store_true")
    publish_parser.add_argument("--public", action="store_true")
    publish_parser.add_argument("--message", default="Publica os fontes do Aegis")
    args = parser.parse_args()
    root = args.root.expanduser().resolve()
    if args.action == "prepare-builds":
        from aegis_builds import prepare
        print(json.dumps(prepare(args.output, args.entry), indent=2))
    elif args.action == "publish-builds":
        from aegis_builds import publish as publish_builds
        print(json.dumps(publish_builds(root, args.repo, args.create), indent=2))
    elif args.action == "audit":
        print(json.dumps(audit(root), indent=2, ensure_ascii=False))
    elif args.action == "status":
        print(git(root, "status", "--short", "--branch"))
    elif args.action == "stage":
        print(json.dumps({"sourcesStaged": stage_sources(root)}))
    elif args.action == "create-repo":
        row = GitHub().create_repo(args.name, args.public)
        print(json.dumps({"repository": row["full_name"], "url": row["html_url"], "private": row["private"]}))
    elif args.action == "push":
        push(root, GitHub())
        print("Branch enviada.")
    elif args.action == "publish":
        print(json.dumps(publish(root, args.repo, args.create, args.public, args.message), indent=2))
    else:
        menu(root)


if __name__ == "__main__":
    try:
        main()
    except (Error, OSError, ValueError, subprocess.SubprocessError) as error:
        print("Erro:", error, file=sys.stderr)
        sys.exit(1)
    except (KeyboardInterrupt, EOFError):
        print("\nEncerrado.")
