# 14.11 — Limited CIL, WinRT contracts and portable installation

Initial delivery delimited for Windows x64/WHP, by the lab executable `guardian-desktop.exe --frameworks`. The `guardian-kernel-frameworks.gk` own kernel now contains the `GuardianFramework/2/cil-forms` (`GuardianFramework/1/cil-console` contract in the previous two deliveries). This delivery does not complete the broad compatibility with the frameworks provided for in the roadmap. The integration of these new contracts to scanner/service continues in the 14.13.

## Implemented

- **Console CIL:**`mscoree.dll` itself interprets an IL subset in
  PE32+ x64, ILONLY, with reference to `mscorlib` v4. Static and non-virtual instance methods, constructors, instance/static fields, type booters, 32-bit integer operations, comparisons, jumps, calls/recursion limited, UTF-16 and `Console.Write/WriteLine` strings on accepted signatures. `Main(string[])` receives only an empty list; additional arguments are diagnosed. It does not load Microsoft CLR or executes managed code on host.
- **WinRT site to guest:**HSTRING and string references, startup by
  thread/apartment and `Windows.Foundation.PropertyValue` activation. Int32, Boolean and String implement `IPropertyValue` and its `IReference<T>` interfaces; class names have been compared with Windows. Other creations and arrays return `E_NOTIMPL`; unknown classes return `REGDB_E_CLASSNOTREG`. No WinRT class is enabled in the host. This set of contracts does not announce support for the full WinRT framework or UWP applications.
- **`version.dll`:**RT_VERSION reading in guest PE64 files, fixed
  information, strings and translation via W queries. The variant A covers ASCII paths and binary queries; A text queries are refused. Multiple language features and resolution/mesclagem MIU are not implemented.
- **Portable installation:**Payload copy of the package to the saveable namespace,
  HKCU record, start of EXE installed, persistent status, resume and removal. Tests use a client compiled against SDK, without private imports in the application. It is not a MSI, MSIX, AppX or SetupAPI installer.

Installed large files are immutable references to bytes of package bound by SHA-256. The bookmark saves canonical path, size and checksum; the kernel only solves it against the files of the validated package. Bookmark checksum does not replace the cryptographic link of volume. Copies up to 512 bytes use common storage. Writing in an installed reference is refused; copying, rereading, querying size and removing are supported in the cropping. The destination remains subject to the ACLs and the existing VFS boundaries. A long-named package reads the ACL of the parent directory as well; this does not extend the file name limit of the saveable storage.

## Limits and profiles

The interpreter accepts files from up to 1 MiB, methadata of up to 256 KiB, 128 methods, bodies of up to 4096 bytes, 32 positions of stack/local, eight arguments, eight frames, 100 A thousand instructions and 64 KiB output. Excess of quotas, arithmetic error and invalid output interrupt the scenario and leave the analysis inconclusive. UTF-8 console is validated incrementally, including truncated sequences, surrogates, excessively long encodings and prohibited terminal controls.

EH, P/Invoke, I8, generics, JIT, GC, and complete BCL are not implemented. The static validator is not a complete CLI verifier: types/stack, budget, and other requirements are also verified in execution. An accepted inspection does not mean ensuring that the program will work.

The external console was exercised in the NT profiles 6.1.7601, 10.0.19045 and 10.0.22631. Profile 6.1 uses API-set v2 aliases itself and delimited for package adapters; it is not a native Windows 7 schema nor a full compliance claim. WinRT is tested in profile 10.0.19045; in 6.1 returns `E_NOTIMPL` before activation. These results do not automatically extend the graphics/of audio matrix to all profiles.

**Full 14.11 pendants:** Real CLR/CoreCLR, .NET Modern Framework/.NET, WinForms, WPF, WinUI 3, Windows SDK App and third-party frameworks beyond the CIL cutout described. `clr.dll`, `coreclr.dll`, `msi.dll` and `setupapi.dll` are not provided by this delivery. Imports/packages recognized without support are diagnosed; unknown dependencies also pass by import validation. It is not stated that most Windows programs work.

## Verification and reproduction

`tools/validate_frameworks.py` runs only 14.11 and the dependencies used by it. Verifies console/Unicode/arithmetic, an external program of the Mono with source/license fixed on `test/frameworks/mono/ORIGIN.json`, profiles, WinRT, version features, copy limits, installation/resume/removal, CIL failures, framework refusals and link mode. SDK's WinRT and Version clients are natively compared in a clean directory, with no guest's DLLs. copy/installation and EXES CIL customers are executed only in MiniNT.

Parser test uses AddressSanitizer on host: 20 a thousand deterministic mutations, all the truncations of fixture and UTF-checks.8. Mutants are never executed. Accepting a mutation for inspection does not mean that it was executed or proof compliance/absence There is no claim of impossibility of escape or undetectability in this delivery.

Set up `GUARDIAN_BUILD_FRAMEWORKS=ON` and, to instrument only the parser test, `GUARDIAN_FRAMEWORKS_ASAN=ON`. Compile `guardian_framework_runtime`, `guardian-desktop` and `guardian-framework-tests` for Release/Debug. Run:

```text
python tools/validate_frameworks.py --build BUILD --config Release --output NOVO --reference CRT --schema-reference SCHEMA --asan-runtime ASAN_BIN
```

MSVC/SDK, WHP, the .NET Framework C# x64 compiler installed and legal local references of CRT/API-set. The package contains only its own DLLs; Microsoft references and ASan runtime are not redistributed. Repeat with Debug. `tools/framework_runtime.py` offers `inspect`, volume preparation and `--portable install|resume|uninstall`. `tools/package_phase14_11.py` requires approved receipts and current hashes before packing. Do not run installation/copy customers directly on the host Windows.

Acceptance Windows/MSVC/WHP on 29/10/2026: **38/38 Release and 38/38 Debug**, with stable source code and binaries during each round. In each setting, the parser passed 20 thousand mutations and 3584 truncations with AddressSanitizer. The external program installed, started, printed its result, resumed from persisted state and was removed in the profile 10.0.19045. The other two profiles cover the console scenario; they do not represent the whole installation/GUI Receipts and hashes accompany private build. Validation is limited to 14.11 and their used dependencies.

## Continuation — Managed Object Base

The following batch implements objects of own classes that derive directly from `System.Object`, non-virtual instance methods, constructors, fields, and `readonly`. The interpreter checks the concrete type by assigning references to places, arguments, returns, and typed fields. A field of another class or a `readonly` script outside the constructor is refused during execution. Static booters run once by type/Case; boot failure interrupts the scenario. There is no custom inheritance or virtual layout in this clip.

Unidimensional arrays of Int32, String and Object, length, reading/writing with limit checking, boxing/unboxing of Int32/Boolean with reference identity and selected String operations are also available: concatenation of two to four strings, length, ordinal equality and `IsNullOrEmpty`. The output of non-override objects uses the type name; arrays use the names of the types of elements. Unlisted string APIs remain unsupported.

Additional limits per process: 32 types, 64 fields, 32 allocations of objects including boxing, 16 arrays with up to 256 elements each and 64 dynamic strings with up to 512 UTF-16 units each. Allocations remain until the process is closed; this **is not a garbage collector**. Excess quota is inconclusive, including if objects are no longer referenced by the program.

`Managed.cs` is a self-inflicted and harmless control, executed in the host CLR only to get a reference output, then compared to MiniNT. Invalid IL external inputs and fixtures are never executed in the host. ASan exercises console and object seeds separately. Negative IL tests remove the native bootstrap generated by ILASM: they check the interpreter rules without announcing support for the native CLR bootstrap. The package includes the `.il` fonts needed to reproduce these cases.

**State after object base:**The wide phase remains open. This delivery did not yet execute complete WinForms/WPF/WinUI or CLR/CoreCLR. The conclusion requires setting target applications by framework, making them run their scenarios on the guest and checking installation, closure and behavior on the announced NT profiles.

Validation of this continuation in 29/10/2026: **56/56 Release and 56/56 Debug**, with stable hashes from sources and binaries. Includes exact comparison of the output of the managed control with the native CLR, installation/resume/removal of this control, null reference, array limits and allocations, initializer failure and three invalid IL programs (readally written, field of another class and incompatible typed site). The previous matrix of 38 cases is preserved as historical; this magnification is not a complete regression of the other phases.

## Continuation — Selected WinForms with Windows windows

First target chosen: **.NET Framework 4 x64** contracts in the profile **NT 10.0.19045**, using the existing USER32/GDI interpreter and bridge. You do not load `System.Windows.Forms.dll` or the Microsoft CLR: accepted metadata references are resolved for explicitly allowed own implementations. `mscorlib`, `System` and `System.Windows.Forms` have AssemblyRef major 4; interface types are also linked to the correct name of assembly.

The independent example is **Rosetta Code, Window Creation, C#**, no changes, fixed by the source blob and SHA-256 in `config/framework-forms-corpus.json`. Your scenario creates a Form, defines title, records Disposed with a delegate, displays the window, enters `Application.Run()` and calls `Application.Exit()` when closing it. `tools/framework_forms_corpus.py` downloads and checks that source in a local folder. The example and compiled EXE are not redistributed by the package; no managed external input is executed in the host.

Contracts from this lot:

- `Form`, `Button`, `Label`, `TextBox` and `Control.ControlCollection.Add`;
- Text, Left, Top, Width, Height, Visible and IsDisposed on accepted subscriptions;
- Show/Hide, Close/Dispose, Application.Run(Form), Run(), Exit() and DoEvents();
- EventHandler with its own static or class instance method,
  `ldftn` as opaque metadata token, Button.Click and Component.Disposed;
- registration/removal of events with target/method and snapshot identity
  list of handlers during the issue; callbacks remain in the invited thread;
- custom broker windows and native BUTTON/STATIC/EDIT controls, with
  text/source code provided by Windows. Host HWNDs do not reach the interpreter.

The input test enumerates only the window of the junior assistant under test, confirms its native classes and consults the EDIT source. Sends BM_CLICK only to the button of this process. Closes use WM_CLOSE in the identified window; there is no mouse movement or typing on the desktop. The tests check callbacks, Unicode text, remove handlers, discarded objects, release of resources, and closure of the auxiliary. Callback errors and quotas remain inconclusive; now they also go through the normal closure of the process and graphical channel before registering the failure.

Each event has up to eight handlers; interface texts have up to 255 UTF-16 units, no built-in NUL or out-of-contract controls. Dimensions are limited to 1024 and positions to 8192. The loop accepts up to 4096 messages and 15 seconds per Run, within the deadline of the launcher. There is no GC, auto layout, designer, Form heritage, custom virtual dispatch, managed timers, drawing/structs System.Drawing, additional dialogues and controls, generic deleters, EH or native CLR interoperability. Calls/signatures out of the list are refused; there is no fallback to run the application in the host. Graphic support is not announced in the NT 6.1/10.0.22631 profiles for these results.

Batch reproduction without repeating previous batteries:

```text
python tools/validate_framework_forms.py --build BUILD --config Release --output NOVO --reference CRT --schema-reference SCHEMA --asan-runtime ASAN_BIN --corpus CACHE_LOCAL
```

Repeat in Debug. `tools/package_phase14_11.py --forms` requires receipts from this batch and current hashes. Parser receives the interface seed with aninhados/delegates metadata types under AddressSanitizer; mutants are not executed. Installation/resume/removal are exercised in the guest both by own control and by independent example. Receipts accompany the private build.

This batch extends to 14.11; **the wide phase remains open**. The next blocks still need to implement runtime/BCL and the graphical contracts required by larger applications and other frameworks before announcing compatibility.

Acceptance of this lot on 29/10/2026: **20/20 Release and 20/20 Debug**, with stable source code and binaries. In each configuration, parser passed 20 thousand mutations and 5120 truncations under AddressSanitizer. The external example completed installation, graphical execution, resume and removal in the guest. The previous two sets (38 and 56 cases) remain historical records, without repetition in this round.
