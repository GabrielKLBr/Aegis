# Security

Aegis is a project under development. Compatibility, coverage, and Windows acceptance limits are documented in `docs/PHASE-13.md` and the roadmap. Isolation and telemetry do not guarantee undetectability or complete protection against every malicious file.

The optional modes from [14.10A](docs/PHASE-14.10A.md), [14.10B](docs/PHASE-14.10B.md), [14.10C](docs/PHASE-14.10C.md), [14.10D](docs/PHASE-14.10D.md), and [14.10E](docs/PHASE-14.10E.md) execute limited sets of GUI operations through Windows APIs in a helper with a restricted token, low integrity, and a supervised Job. Application code remains in MiniNT. This helper adds attack surface on the host; its profile is not AppContainer and does not block all reads of user files. The phase document lists operations, quotas, IPC, and failure tests. Execution of operations outside that contract remains a reportable vulnerability.

For a vulnerability that allows guest escape, access to private data, or unauthorized operations on the host, use [GitHub private vulnerability reporting](https://github.com/GabrielKLBr/Aegis/security/advisories/new). Include the affected revision, platform, and a minimal harmless reproduction. Do not publish credentials, personal data, or malicious samples in public issues.

Security tests must preserve errors and incomplete coverage. Artificially empty responses must not be used to make a failure appear successful. Changes to the core require review and validation; the script generator produces versioned regression tests and does not automatically change these boundaries.

In 14.10D, file selectors use only the guest namespace; there is no host Shell browser. Fonts/icons and control messages use bounded contracts, private IDs, retention of selected objects, and quotas. Geometry policy is virtualized and does not establish complete USER32 equivalence.

In 14.10E, curves, paths, regions, transforms, glyphs, and descriptors use bounded contracts and object ownership. Host descriptor pointers/padding are not serialized. Cursors affect only session clients. The larger set of GDI operations requires the same isolation, quota, partial-failure, and cleanup checks; it does not guarantee the absence of escape vulnerabilities.

In [14.10F](docs/PHASE-14.10F.md), the helper also executes Direct2D, DirectWrite, built-in WIC codecs, and D3D11 WARP through bounded commands. It does not receive guest pointers, shader/driver modules, host paths, or COM interfaces. Images/textures have quotas, and API errors are distinguished from transport errors. These components increase attack surface; compatibility tests do not establish containment suitable for running real malware.

14.10G adds PCM/WAV playback and a limited WinMM/WASAPI subset in `--audio` mode, with copied buffers and default output mediated by a restricted helper. Capture and compressed media remain unavailable. [Contracts and limitations](docs/PHASE-14.10G.md).
