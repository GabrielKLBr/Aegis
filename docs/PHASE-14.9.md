# Phase 14.9 — Network, HTTP, TLS and encryption in MiniNT

The 14.9 adds a bounded batch to **Windows 10 x64**, with 111 exports in ten own DLLs: WS2_32, MSWSOCK, DNSAPI, IPLPAPI, WINHTTP, WININET, BCRYPT, NCRYPT, CRYPT32 and SECUR32. They forward the contracts implemented to `guardianinet.dll` within the guest. Acceptance customers use normal SDK Windows imports, including the Winsock ordinals selected by SDK. There is no full implementation of these libraries or acceptance of arbitrary Windows applications. Other NT profiles still need their own matrices.

## Transport and policy

`guardian-kernel-internet.gk` retains the services of 14.8 and adds a layer 4 transport device. The host maintains limited TCP/UDP sockets; the guest implements Winsock, HTTP, encryption and TLS contracts. This delivery does not contain a complete TCP/IP stack within the guest kernel.

The A9 input only accepts executable code from `guardianinet.dll`. The kernel copies messages, checks limits and assigns PID and sequence. The protocol has 2,184 bytes, payload of 2,048 bytes, 16 sockets and eight destinations per session. It does not carry host pointers, callbacks or handles. Identifiers are monotonic and belong to the process; repetition, socket of another PID and invalid messages are refused. Process output and closing collect sockets, including after an exhausted quota.

| Mode | Behavior |
| --- | --- |
| Offline, default | DNS and connections are denied. Entropy remains available. |
| Simulated | Synthetic addresses in 198.18.0.0/24, echo TCP/UDP, fixed HTTP or silence. No native socket is opened. |
| Laboratory | Only destinations and ports explicitly declared on 127.0.0.1. Used by tests with local pairs. |
| Authorized | Fixed public IPv4 destinations and declared ports; additional verification excludes local host addresses and networks. There is no open discovery or forwarding. |

DNS exclusively uses policy names/address; does not call the host solver. Names are tiny ASCII, up to 63 characters. There is no external DNS, automatic proxy, IPv6, listening sockets, RAW or general access to the local network. The authorized mode is validated for restrictions, but the acceptance matrix does not depend on public servers or performs tests on the Internet.

The default limit is 32,768 operations and 2 MiB plus sending, receiving and entropy. The policy admits 16–65,536 operations and 4 KiB–16 MiB. Excess fails; does not automatically expand the budget. `internet.json` records operations, results, bytes, open native sockets and still active resources. Events also enter the network domain of telemetry. The policy is a canonical binary file of 800 bytes; `tools/internet_runtime.py policy` validates and converts JSON.

## Contracts of the guest

| Family | Lot implemented |
| --- | --- |
| WS2_32 / MSWSOCK | Connected IPv4 TCP and UDP clients, numerical names and fixed mappings, upload/reception, non-blocking, WSAPoll, timeout, SO_ERROR, complete shutdown, I/O overlapped with event and guest worker, closing cancellation and TransmitFile of guest file. |
| DNSAPI / IPHLPAPI | A/W queries of A records, release compatible with SDK import DnsFree and a virtual adapter, without inventory of personal interfaces. |
| WINHTTP | GET/POST/HEAD HTTP/1.1, status/headers, body by Content-Length or simple chucked, reading/writing in parts, asynchronous callbacks in invited threads, timeout, cancellation and explicit Basic on TLS. |
| WININET | meeting/connection/request synchronous wrappers, query, read and URL opening for the same limited transport. |
| BCRYPT | SHA-1/256/384/512, HMAC, AES CBC of whole blocks and GCM, ECDSA P-256, import/export limited and signature verification. GCM validates authentication before publishing plaintext. |
| NCRYPT | Software Provider own, P-256 keys named in virtual HKCU, open/sign/check/delete and export policy. Private export is denied by default. |
| CRYPT32 | DER context with metadata, copy/refcount, memory stores, virtual ROOT, leaf/root direct chain and base/SSL policies with name, signature, time and server usage. |
| SECUR32 | Self-exit Schannel credentials, handshake by tokens, context attributes, remote certificate and EncryptMessage/DecryptMessage of TLS records. |

The exposed adapter represents the virtual transport device. Its local addresses/ports does not identify the physical socket used by the host. Overlapped accepts a WSABUF of up to 2,048 bytes and event; completion routers, IOPP, WSAEventSelect and complete semantics of all flags are not in this batch. UDP preserves datagrams and tells truncation; there are up to eight datagrams pending by simulated socket. The simulated TCP echo has 64 KiB queue.

HTTP limits the body to 64 KiB, headers to 8 KiB and 24 live objects. There is no HTTP/2, WebSocket, automatic redirect, compression, cache, cookies, PAC/WPAD, integrated authentication, trailers or chunked extensions. Unsupported requests fail. `RECEIVE_RESPONSE_TIMEOUT` treats headers and `RECEIVE_TIMEOUT` receives; the native comparison covers both limits. Asynchronous WinINet is not implemented. Basic credentials are provided by the guest application and are not obtained from the user or host storage.

## Encryption, Certificates and TLS

Algorithms run in CPL3 with BearSSL, MIT fonts without modifications, fixed in `7bea48e5e850ab4cafbe68d3765cdaba13a86d6f`. The license and hashes of the 309 files are in `guest/third_party/bearssl`. The configuration uses portable software without automatic selection of RNG/time from the system by BearSSL. The entropy channel uses the host generator; it does not export its keys or credentials. The watch comes from the time service already existing in the guest.

TLS is **1.2**, with ECDHE-ECDSA or ECDHE-RSA, AES-128-GCM and SHA-256. Handshake, key derivation, validation and records run within MiniNT. Host does not end TLS. There is no TLS 1.3, renegotiation, client certificate, NTLM/Kerberos or general SSPI. Flags to ignore validation or use personal credentials are refused. CBC does not implement standard PKCS#7; RSA is not a public CNG algorithm of this batch. SHA-1 exists for compatibility; it does not change the TLS selection.

Up to 32 certified contexts, eight stores are admitted in memory with eight entries each and 16 live chains. Persistent ROOT contains ** one** CA DER anchor of up to 512 bytes; a second root or duplication beyond this failure quota. Memory stores deal with NEW, USE_EXISTING, REPLACE_EXISTING and ALWAYS, with comparison of issuer and serial. CERT_INFO designs version, serial, algorithms, issuer, subject, validity, public key and up to 16 extensions. Parser is limited to DER of 8 KiB and serial of 32 bytes; OIDs have a maximum of 95 characters.

Validation covers the direct chain leaf/root of the corpus, server name, signature, validity and EKU serverAuth. There is no AIA discovery, arbitrary intermediaries, additional stores or arbitrary usage policies. Parameters outside of this contract are refused. Request for revocation returns unknown state and policy failure; no verified revocation is declared.

Named keys accept up to 31 tiny/digits/hyphen characters and are saved as blobs in the 14.6. virtual registry. This 64 KiB disk is not an encrypted safe. Insulation is from the namespace and session; anyone with the exported state can inspect it. No key, ROOT store or host credential is mounted.

## Persistence, catalog and execution

GPV2 remains limited to 32 files, 24 modules per process, 8 MiB per file, and 64 MiB per volume. `.cfg` and `.der` become allowed data in `C:\App` for GPV2; GPV1 preserves its previous list. Dependencies are included by scope and network imports are checked before boot. Microsoft CRT and schema continue to be legal local entries and do not accompany build.

`--state` keeps the virtual disk between sessions, including key and test root. Capture and restore of RAM snapshots are refused for this transport: sockets and external peer status are not reproduced by RAM. The execution remains limited to 1–30 seconds per session in CLI.

```powershell
python -B tools/internet_runtime.py bundle --build PASTA-BUILD --config Release --reference PASTA-CRT --schema-reference SCHEMA-LEGAL --output runtime-internet
.\PASTA-BUILD\Release\guardian-desktop.exe PASTA-BUILD/guest/Release/guardian-kernel-internet.gk runtime-internet/runtime.gpv runtime-internet/manifest.json runtime-internet/binding.gnt sessao
```

The default application scans offline encryption. To declare destinations, use a JSON with `mode`, `endpoints` and optional quotas; each endpoint has `name`, `address`, `port`, `transport` (`tcp`/`udp`) and optional simulated `service`:

```json
{"mode":"simulated","endpoints":[{"name":"localhost","address":"198.18.0.254","port":7,"transport":"tcp"}]}
```

Convert with `python -B tools/internet_runtime.py policy politica.json politica.bin` and add `--network-policy politica.bin` to the execution. Omit this argument selects offline.

## Acceptance and reproduction

There are nine new tests: `internet_core`, `internet_runtime_tools`, `internet_crypto_reference` and `internet_{crypto,sockets,http,tls,sspi,reference}_integration`. Integration tests start **20 MiniNT sessions by configuration**, plus four native customers against their own loopback pairs. The native CNG reference uses only ephemeral keys. TLS pairs use intentionally public certificates and synthetic keys in `test/fixtures/internet/certificates.json`. They are never installed on Windows or used as production credentials.

The matrix covers known vectors SHA/HMAC/AES and RFC 6979, invalid tag/signature, re-opened key in another session, TCP/UDP, DNS allowed/denied, simulation without native sockets, HTTP chunked/POST, asynchronous partial reading, cancellation, timeout and Basic about TLS. TLS negotiates with real pairs ECDSA/RSA; rejects expired certificate, wrong name, changed signature, EKU only client and missing root. SSPI tests handshake, partial registration, plaintext and adulterated record. The persistent virtual store is used in a second session.

`tools/validate_phase14_9.py` requires **133 tests in the Release** regression and **58 in the Debug** acceptance, without ignoring, preserving hashes of sources, binaries and references before/later. Regressions include the previous phases and the original Flappy. The package requires stable evidence and runs again in both configurations after checking all files and ZIP. Counts are criteria required by the validator; the result of the execution is in the private build `validation.json`. Linux has not been revalidated at this stage.

Delivery ends at 14.9. A **14.10 and 12F are not started**; complete Windows compatibility, window-based native GUI and the following batches remain open.

References: [BearSSL](https://www.bearssl.org/), [CNG](https://learn.microsoft.com/windows/win32/api/bcrypt/), [Schannel](https://learn.microsoft.com/windows/win32/secauthn/initializesecuritycontext--schannel), [WinHTTP](https://learn.microsoft.com/windows/win32/winhttp/option-flags), [certificate stores](https://learn.microsoft.com/windows/desktop/api/wincrypt/nf-wincrypt-certaddcertificatecontexttostore), [chains](https://learn.microsoft.com/windows/win32/api/wincrypt/nf-wincrypt-certgetcertificatechain) and [RFC 6979](https://www.rfc-editor.org/rfc/rfc6979.html)].
