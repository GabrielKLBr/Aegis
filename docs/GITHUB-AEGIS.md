# Aegis — sources in GitHub and Git automation

Aegis means **Advanced Endpoint Guardian for Inspection & Sandboxing**. The public repository is <https://github.com/GabrielKLBr/Aegis>. The project name in GitHub changes; executables and `guardian-*` contracts keep their names to preserve compatibility.

### 23/10/2026 approved separation

Public Aegis receives full sources, including runtime MiniNT and existing profiles, without replacing implementations with null returns. Historical builds are in **https://github.com/GabrielKLBr/Aegis-builds**, private, like branch files, without creating GitHub Releases. Windows Validations and Phase 14 are documented in their delivery records.

To export explicit selections of builds, preserving the originals:

```text
python tools/aegis_git.py prepare-builds --output PASTA_NOVA --entry fase14=PASTA_DA_ENTREGA
python tools/aegis_git.py --root PASTA_NOVA publish-builds --repo SUA_CONTA/Aegis-builds --create
```

You can repeat `--entry ROTULO=CAMINHO` for each existing directory or package. Export generates ZIPs with SHA-256 manifest from internal packages and files; refuses links, known credentials formats and disks/status personal VM. Packages above 90 MiB need to be separated into smaller selections. Upload checks the manifest and requires the destination to be private, including before push. Do not automatically transform a public repository in private. To update a later export, prepare another folder and integrate your packages/manifest to the private clone before command, omitting `--create`.

Builds are historical records: they are not recompiled to pretend that there has been new acceptance. Unpreserved binary-free phases should appear as missing in the inventory. No compiler caches/intermediate or OS images are included.

## Published content

Sources C/C++, assembly, Python, configuration, documentation, and images from the interface. No builds, releases, packages, snapshots, virtual disks, executables, DLLs, compiled libraries, or compiled kernels enter. Even old binary in `test/` is left out; its sources and inventories remain. Original local files and previous deliveries are not deleted.

`.gitignore` deletes local artifacts. The wizard uses an explicit list of fonts, refuses links/files disguised binaries and checks some known credentials formats without printing their values. This check is an additional barrier; it does not constitute complete detection of any secret. Before sending a branch, it also checks its previous commits not to publish a binary that has been added and later deleted. It does not automatically send tags, does not create releases, does not push and does not merge.

## Python Wizard

Requirements: **Python 3.10+**, Git on PATH and GitHub authentication from your account. Works on Windows and Linux; you do not need to install Python packages.

```text
python tools/aegis_git.py
```

The menu offers status/diff, repository selection and clone, repository creation, branch selection/creation, fast-forward update, source commit, push, PR consultation/checkout, draft PR creation, audit and sandbox test reviews generation.

To publish this folder in a new repository of your own account:

```text
python tools/aegis_git.py publish --repo SUA_CONTA/Aegis --create --public --message "Publica fontes do Aegis"
```

For later commits/push, omit `--create`; it does not silently reuse an existing repository. `--public` is required to create an audience; default creation is private. If `origin` points to another destination, the publication is stopped without replacing the remote. Local preparation and commit take place before remote creation; if the network fails, the commit remains recoverable.

There are also `audit`, `status`, `stage`, `push` and `create-repo` commands. `--root PASTA` before the command selects another folder. To change a repository, select its root. The menu does not change branch with pending local changes. The font selection is focused on the structure of this project; for another project, review the list in `tools/aegis_git.py` before using commit/publication.

Authentication looks for `GH_TOKEN`, `GITHUB_TOKEN`, GitHub CLI already authenticated or Git's credentials manager. **Authenticar** option accepts a hidden token kept only in memory of that session. The wizard does not record tokens in fonts, remote URL, command arguments or reports. Repositories/PRs need corresponding permissions in your account. The first commit also needs name/and-mail already configured in Git.

## Sandbox reviews and safety

Returning empty in all APIs would break program behavior and create observable inconsistencies. Protection relies on isolation, consistent synthetic metadata, permissions and preservation of real errors. No revision promises to make sandbox undetectable. Existing synthetic profiles, with seed and coherent identity, are in [PHASE-12E.md](PHASE-12E.md).

The generator creates ** new versions of regression scripts**, without modifying the kernel alone or manufacturing results. Implementation changes continue as commits/branches revisable, with results before and after.

```text
python tools/aegis_sandbox_revisions.py generate --seed 42
python tools/sandbox_revisions/sandbox_regression_v0001.py --manager CAMINHO/guardian-vm --kernel CAMINHO/guardian-kernel-analysis.gk --output DIRETORIO_NOVO
```

In Windows, use `guardian-vm.exe`. Without `--seed`, the generator chooses a new seed. Each generated script contains a template revision number, seed and hash; the same seed/revision/template reproduces the same bytes. Previous versions are not overwritten. Seed varies the order of tests, not the implementation or identity of the guest system. The scripts are fonts and can go to a PR; reports should be in `build-*` or out of the source tree.

The six scenarios use only their own applications: reference behavior, refusal, simulation, benign program, collection saturation and offline network. They check author/causality, real errors, unaltered content after refusal, simulation effects, classification, visible loss, export, and a private control file. The absence of the canary is verified in the reports; this does not prove the absence of any leak in all possible channels.

Failed returns error, closes with code different from zero and preserves `result.json` with diagnostic when the evidence directory was created. There are no empty answers to hide faults, host protection bypasses, external malware execution, or automatic rewriting of sensitive code.

## Recreate compiled test data on Linux

The clone contains only sources and documentation images. For tests that analyze PE files, build your own examples and create an external folder:

```text
python tools/build_guest_linux.py --llvm LLVM_NATIVO --config Release --output DIRETORIO_GUEST
python tools/prepare_source_fixtures.py --from-guest DIRETORIO_GUEST --output DIRETORIO_FIXTURES_NOVO
cmake -S . -B build-linux-source -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DGUARDIAN_NATIVE_FIXTURE_DIR=DIRETORIO_FIXTURES_NOVO
cmake --build build-linux-source --parallel 4
ctest --test-dir build-linux-source --output-on-failure
```

These commands do not execute PE in the host or use Wine. They are own recompiled entries for parser tests; they do not replace the frozen MSVC references cited in historical validations. For KVM tests, add `-DGUARDIAN_TEST_KVM=ON -DGUARDIAN_KVM_GUEST_DIR=DIRETORIO_GUEST` and also point out the directory of the drivers according to [LINUX-KVM.md](LINUX-KVM.md). Current validators accept `--native-fixtures DIRETORIO_FIXTURES_NOVO`. Windows references and legacy comparison kernels are not in GitHub; they must be produced/provided explicitly for these specific tests.

Windows continues using MSVC/WHP and existing build scripts. Historical examples `test/imgtest.dll`, `liba.dll` and `libb.dll` can be recompiled from the respective `.c`; the main kernel/PE images are produced by the guest build. Unknown binary cannot be downloaded to fill in these data.

## Validation of this publication

In 22/10/2026: 18 tests of Python tools, four parser tests on a copy containing only sources and the six scenarios of the generated revision passed on Linux/KVM. The tool tests use Git real location and simulated API, including refusal of binary/secrets, history with deleted build, remote destination, pre-prepared publication and non-delete failure.

This validates automation and the new fixtures option; it does not declare a complete new execution of the 153 phase 13 tests or native Windows acceptance of scripts. Previous acceptance and its limits remain in [PHASE-13.md](PHASE-13.md). No later phase was started.

API used: official documentation of [](https://docs.github.com/en/rest/repos/repos) and [pull requests](https://docs.github.com/en/rest/pulls/pulls).
