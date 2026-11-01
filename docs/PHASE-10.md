# Phase 10 — Virtual network and containment

Started in 18/10/2026 by explicit user request. Valid Linux implementation and acceptance; native Windows gate pending. Windows/MSVC/WHP remains the product; Linux/KVM is compiled and executed natively without Wine or QEMU. Native Windows acceptance remains separate.

## Final Linux Acceptance

**Implement completed for the profile described; approved Linux matrix. Pending native Windows/MSVC/WHP acceptance. Phase 11 was not started.**

Final evidence: `build-phase10/validation-20261018-180022-c09af2d0/validation.json`. Status: `passed` / `awaiting_native_windows_acceptance`. All sources were reconstructed and their hashes remained equal from start to finish. No Wine, QEMU, ignored tests or reuse of binaries from previous rounds.

| Configuration | CTests | Guests of network real | Captures conferidas |
| --- | --- | --- | --- |
| Debug | 27 approved | 16 boots/resumes + 8 for manager | 18 catches |
| Release | 27 approved | 16 boots/resumes + 8 for manager | 18 catches |
| Sanitize | 27 approved | 16 boots/resumes + 8 for manager | 18 catches |

Total acceptance:

- 81 plays CTest, 132 Regression Boots of the 11 Images of Previous Steps
  and four fatal controls/IST correctly rejected as success.
- 72 boots/resumes network, with IPv4/IPv6, CPL3, DNS, laboratory of four
  guests, isolation, cloning, snapshots policy and durable history. There are also nine pauses checked in network checkpoints.
- 345 device/policy checks and 20,000 entries malformed by
  configuration; 39 independent serialization/relay checks per configuration.
- Preserved phase 9: 81 boots/resumes matrix of the full console,
  27 pauses, 24 manager bootes and 221 snapshot checks per configuration. Includes 589 volume/52 startup checks failed prefixes and 64 interrupted creation/cloning transactions per configuration.
- Windows Preparation: 12 modules compiled for COFF objects by LLVM ELF;
  there was no Windows execution. The native gate requires 47 CTests in Debug/Release, including 153 handler comparisons and 59 unwind with Microsoft APIs.

The functional limits remain explicit: UDP on the own device and APIs itself in CPL3, without ABI Winsock/TCP; real external relay not exercised; snapshots do not reverse the external world; physical execution protection KVM remains distinct from that provided by WHP.

Linux package delivered: `dist/guardianav-phase10-linux-x86_64-20261018.tar.gz`. Extraction and execution outside the project folder were approved; result registered in the package section below.

## Profile implemented

Point to point IP layer device, MTU 1280, UDP IPv4/IPv6 with simulated checksums, DNS and echo, isolated laboratory between VMs of the same catalog and UDP output by explicit relay. No network is standard. TCP, Ethernet, routing, fragmentation, IPv6, multicast extensions, broadcast and automatic discovery will not be announced as supported. Unsupported requests will be refused.

VM policy limits addresses, ports, pairs, queues, bytes and operations. No lab traffic uses the physical interface of the host. Proxy requires fixed public address of the relay and explicit list of public/ports destinations; DNS names come from the policy, without consulting the host solver. PCAP capture and registration of decisions include refusals. Snapshots link policy and device status; effects on pairs/relay already performed cannot be undone.

Previous sources in `snapshots/codex-before-phase10-20261018`. Final results at the beginning of this document; development history preserved below.

## Progress before full acceptance

Linux pretests: 238 initial policy/parser checks, 20,000 malformed entries, 16 boots/resumes KVM and eight bootes by approved manager. Independent binary vectors and 39 relay checks have been added. The full matrix Debug/Release/Sanitize and the regression of previous phases will be executed after the font revision.

Failures encountered and corrected during development: package assembly with entry/exit in the same buffer; need for stack probe in the guest; qualification of namespace in the command line executable; reuse of the same capture directory in the tests. Development logs remain in `build-phase10/development-logs`. Partial approval is not final acceptance.

## Device and battery usage

The new image is `guardian-kernel-network.gk`, `network` contract. Includes previous CPU/CPL3 probes and a kernel network driver. The CPL3 program uses syscalls 0x90/0x91, with a copy limited to its data page; kernel addresses, overflow and buffers above MTU are rejected before the device. They are test profile-specific APIs without ABI Winsock. Phase 8 PE consoles retain their current contract and do not advertise unimplemented sockets.

The connection loads IP packages directly with MTU 1280 and queue of eight frames. Parser requires exact sizes, IPv4 checksum and non-zero UDP checksum in both families. It does not accept IPv4 options, fragmentation, IPv6, ICMP extensions, TCP, Ethernet, multicast, broadcast, IP-in-IP, GRE, Teredo or automatic discovery. IPv4 UDP without checksum does not belong to the profile either. There is no TAP/TUN, bridge, NAT from the host or DNS service from the system. No Windows program runs by the Linux host to compile or test the guest.

Serialization references: [UDP/RFC 768](https://www.rfc-editor.org/rfc/rfc768), [IPv6/RFC 8200](https://www.rfc-editor.org/rfc/rfc8200) and [DNS/RFC 1035](https://www.rfc-editor.org/rfc/rfc1035). The profile implemented is more restricted than the complete protocols.

## Profiles by VM

| Profile | Traffic permitted |
| --- | --- |
| `offline` (default) | No shipment or receipt; observable refusal |
| `simulated` | Local DNS at 253/UDP 53 node and echo at 254/UDP 7 node; both families |
| `lab` | DNS of declared pairs and UDP only to knots/ports permitted, with reciprocal authorization |
| `proxy` | Static policy DNS and datagrams to explicit public/ports addresses, exclusively by the configured public relay |

Node N receives IPv4 `198.18.0.N` and IPv6 `fd42:6761::N`, with 1 ≤ N ≤ 254. The origin is checked in the host. Simulated DNS publishes `echo.guardian.test`; laboratory uses `node-N.guardian.test`. Questions A/AAAA limited to a question, without compression in the query; names outside the policy receive NXDOMAIN. There is no query to the DNS of the LAN or external fallback.

In the laboratory, VMs exchange frames for limited queues in the catalog, with locks, validated common files and atomic publication. Different lab catalogs and names form distinct networks. Only active members receive shipments; duplicate node is refused. The new session clears the queue of the previous incarnation. There is no physical network connection in this path. UDP does not promise delivery or order; full queue, missing member and storage failure return error.

Example Linux, after compiling host and images:

```bash
./build-linux/Release/guardian-vm --root "$PWD/laboratorio" create demo \
  build-guest-linux/Release/guardian-kernel-network.gk network --disposable
./build-linux/Release/guardian-vm --root "$PWD/laboratorio" network demo simulated
./build-linux/Release/guardian-vm --root "$PWD/laboratorio" start demo
```

For two pairs, create `alpha` and `beta` with the same contract and configure:

```bash
guardian-vm --root laboratorio network alpha lab --lab teste --node 1 --peer 2 --port 40000
guardian-vm --root laboratorio network beta lab --lab teste --node 2 --peer 1 --port 40000
guardian-vm --root laboratorio start alpha --paused
guardian-vm --root laboratorio start beta --paused
guardian-vm --root laboratorio resume alpha
guardian-vm --root laboratorio resume beta
```

The guest example swaps bytes in both families, DNS query and ends; it is not a permanent server. `guardian-vm` commands are the same in Windows, using `guardian-vm.exe`. Network setup requires VM stop. Clones maintain policy; two clones with the same node in the same laboratory cannot start simultaneously. Changing the node of a clone requires new boot; an old snapshot is not accepted under another policy.

## Optional external relay

The supported output is UDP by a proper relay, not a HTTP/SOCKS. proxy Nothing is started automatically. `--relay` requires IPv4 numerical audience, `--relay-port` requires explicit port and each `--allow` declares `nome,família,endereço,porta`. IPv6 uses eight explicit hexadecimal groups in the configuration, without names or zones. Configuration form examples:

```text
guardian-vm --root laboratorio network demo proxy --relay IP_PUBLICO_DO_RELAY --relay-port 9000 --allow exemplo.test,4,IP_PUBLICO_DO_DESTINO,7
```

The above markers must be replaced by lab addresses. The operator can use `tools/guardian_udp_relay.py` on an external machine, with `--bind`, `--port`, `--client` and `--allow` explicit. Relay also checksum, family and destination; uses UDP socket connected to the allowed destination. Do not run it in the analyzer host to simulate external output.

The host only opens a UDP socket for fixed relay. Re-check addresses and subnets of local interfaces before each shipment. Loopback, private addresses, link-local, metadata, CGNAT, documentation, multicast, broadcast, IPv4-mapped IPv6, NAT64 and transition prefixes are out of public policy. The filter is conservative: it also refuses all `169/8`, `192/8`, `2001::/23`, `2002::/16`, `3ffe::/16` and `3fff::/16`. It does not accept resolvers or URLs that can change the address after validation.

The relay envelope contains magic/version, 128-bit random nonce, IP size and package. The response must come from the connected socket, repeat the nonce and match exactly the family, addresses and inverted ports. Limits: 100 ms reception timeout on the host and 75 ms for the upstream reference relay query. The protocol does not offer TLS, cryptographic authentication of the relay or confidentiality; it is intended for the explicitly authorized laboratory. Validation uses a controlled relay and local native sockets to check the protocol without test traffic sent to the internet. A real user operated public relay has not been exercised on this machine.

## Snapshots, capture and limits

Status saved: canonical policy, sequence, counters, reception queue, guest report, decisions and capture. Restoration requires the same policy; callbacks and host sockets are not serialized. Effects already performed in pairs or in relay cannot be undone by restoring VM. A new laboratory worker uses new identity of participation; restored candidates in the same worker share the existing endpoint.

Each meeting/candidate of the manager saves its files in `<catálogo>/<VM>/network-history/<geração>/`: `policy.gnp`, `network.pcap` and `network.json`. Restore creates another record and preserves the previous ones. Delete VM removes its files, including this history. PCAP uses LINKTYPE_RAW (101), logic clock in microseconds and MTU 1280; does not represent a real time. JSON contains hash FNV of PCAP/policy to detect incomplete pairs after publishing failure, in addition to counts and decisions. Publication of the two files is individually atomic; does not constitute a transaction between files. Capture records the attempt before allowing external effect.

Result codes: 0 accepted, 1 empty queue, 2 denied, 3 malformed, 4 unsupported protocol, 5 full queue, 6 quota, 7 unavailable. Events indicate direction (0 exit/1 input), size, family and port; refusals also appear. `acceptedTx` counts accepted attempts in the policy, including if the transport fails later; the later error is another event. Wait without package does not generate a false traffic event. Capture failure interrupts execution.

Standards: 128 packages and 128 KiB per session; configurable maximums 256 packages and 256 KiB, adding upload and reception. Up to eight rules/pairs/ports, eight frames in queue, 1024 operations/events, 512 KiB of PCAP, 16 networks per catalog and 64 session records by VM. Exhausted quota returns error and does not extend policy. The manager's four VMs quotas residents/256 MiB continue to be valid.

## Resume acceptance

`python3 tools/validate_phase10.py` on Linux rebuilds all artifacts and performs Debug, Release, ASan/UBSan, real guests and regressions. On Windows, `python tools/validate_phase10.py` uses CMake/MSVC/WHP and requires real Microsoft oracles. Cross compilation is just preparation and does not replace this gate.

Capture of a new restored/clonada session can contain the prefix saved in snapshot. This prefix is historical; restore does not relay these packages. Host generation directories and reports identify each execution.

Full acceptance mark: Debug passed the 27 reconstructed CTests without faults/skip, including CPL3, snapshots, recovery, four laboratory guests and manager profiles (355.28 s on this machine). Evidence in `build-phase10/validation-20261018-172452-d770ed5f`. Boot regression and Release/Sanitize settings were still running while saving this milestone.

The first complete acceptance stopped in the `Debug-complete-16-0` regression, with `0xC7F4` autotest status (check 244 of the example of dynamic DLLs), without timeout. The 27 CTests and 60 previous bootes of that execution passed. The error reappeared in one of 12 bootes isolated with the same image. The original evidence was preserved as failure; it is not counted as approved acceptance. Ongoing diagnosis to separate TLS/references and return from Yield.

Confirmed diagnosis: by reducing the probe to a single worker, `SwitchToThread` zero return produced `0xC7FE`, distinguishing it from TLS and `FreeLibrary` errors. The old test unduly required non-zero return; the [Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-switchtothread) document allows zero when there is no other thread ready. The correction keeps the two competing workers, the eight cycles per worker, and all TLS/references/download. checks were added `LoneYieldProof`, which requires observing this legitimate return while the father waits. Acceptance will be remade with the corrected source; diagnostic probes will be separated from the package.

With the correction, the new `build-phase10/validation-20261018-174152-6a21f7f6` execution approved the 27 Debug CTests (363.69 s), the 66 Debug regression boots and the two fatal/IST. Release and Sanitize controls continued to run. Phase 11 remains unstarted.


The relay review reproduced a failure of reception under asynchronous signal (`build-phase10/eintr-before-fix`, check 337). The KVM timer uses signals to wake the executable; UDP reception now uses non-blocking socket and an absolute 100 ms time frame, resuming the wait after interruptions. The positive test receives delayed response after a signal; the negative test injects signals for 200 ms and requires error close to the 100 ms deadline, without restarting it. The preparation passed with 345 network checks and 20,000 malformed entries. Windows: the 12 updated modules compiled into COFF objects without execution.

The `validation-20261018-174152-6a21f7f6` round passed the 27 CTests of Debug and Release and the 66 boots Debug; it was interrupted at a border without resident workers to add that correction. It is intermediate evidence, not final acceptance. The final round of acceptance is `build-phase10/validation-20261018-180022-c09af2d0` and its approved results are at the beginning of this document.

## Linux package delivered and verified

File: `dist/guardianav-phase10-linux-x86_64-20261018.tar.gz` (about 1.5 MiB, 252 files). SHA-256: `c78332e6eb46c76ac794ddf21a7ce6dc790b6750642549b51dee276273531d6c`. The `.tar.gz.sha256` file next door contains the same sum for conference.

`build-phase10/validation-20261018-180022-c09af2d0/package-validation.json` registered `passed`. The package was extracted in a temporary folder and executed with another work directory. All hashes and three ELF executables were checked. The scanner ran natively; the manager's tests completed eight network bootes and two additional console bootes with persistence. The volume sequence survived the reboot and the base remained intact. Sentinels did not record Wine/QEMU. Logs and package reports are next to acceptance.

The distributed file stores the documentation captured before the final verification of the package itself; the final hash and the result of this verification are in this external record. The sources remain identical to the accepted sources.

Recoverable revision: `changes.patch`, `changes.json`, `documentation-sha256.json` and `windows-cross-precheck.json` in the acceptance folder. The previous 329 files remain intact in `snapshots/codex-before-phase10-20261018`; there are 41 text files added or modified with reversable differences. Previous rounds/probes remain preserved and identified as development, failure or interruption.

**Stop at this stage:** Linux implementation and acceptance completed for the documented UDP profile. Native Windows acceptance remains pending; run `python tools/validate_phase10.py` on Windows/MSVC/WHP for that gate. Phase 11 has not been started.

## Continuation authorized after this delivery

In a new request from 18/10/2026, the user authorized to start phase 11. The state and evidence of this step are in [PHASE-11.md](PHASE-11.md); the previous mentions to “not started” describe the historical closure of phase 10. The Windows gate of phase 10 remains pending.
