# 14.10F — Direct2D, DirectWrite, WIC and WARP textures

Delimited delivery for **Windows 10 x64, NT 10.0.19045**, in **--software-graphics** mode. The application, its DLLs and callbacks run on MiniNT. Windows host draws with Direct2D by software and DirectWrite, decodes images with WIC and processes textures with D3D11 WARP in a restricted helper. There is no promise of complete DirectX or universal compatibility.

## Contract and isolation

Kernel guardian-kernel-software-graphics.gk, capacity GuardianSoftwareGraphics/6/guest, protocol v6 and auxiliary guardian-software-graphics-host.exe. The manifest links profile, kernel, volume and SHA-256 The A–And variants preserve their protocols. Command/Reply/Packet Keep 972/896/3260 bytes. Object IDs include session and are verified by process, thread, type and kinship. Pointers, COM interfaces, native callbacks and handles do not cross the bridge. The guest COM interfaces follow the SDK types and ABI.

The helper keeps token restricted, low integrity, two inherited handles, one process and the interface constraints of the previous phases. Job limits memory to 256 MiB and closes the helper if the supervisor leaves. Commands have a ten-second deadline; normal shutdown has five seconds. The Job limit includes internal allocations, which are not fully represented by pixel accounting.

The attack surface includes Windows Direct2D, DirectWrite, WIC, and WARP components. The helper is not AppContainer and this token does not prevent all personal file reading. Tests do not prove sufficient escape or containment to perform real malware safely.

## Resources implemented

- **Direct2D:** single thread factory, render HWND target or DC of
  own window; cleaning, brush solid/colour/opacity, lines, rectangles, ellipses, transformation in order, clipping, text and bitmaps WIC. Deferred errors appear in EndDraw. BindDC covers the entire area of the authorized DC, 96 DPI. Do not accept desktop DC or arbitrary memory. Resize follows the limited actual client. A painting keeps your DC up to EndPaint when the host resizes during callback.
- **DirectWrite:** SHARED/ISOLATED factories with their own objects; format,
  family/weight/style/stretch, basic line-alignment and breakage. The helper uses isolated factory and native sources of the allowed catalog. Text up to 127 UTF-16, family/locale units up to 31 and source from 1 to 96. Layouts, custom fonts and typographical analysis remain pending.
- **WIC:** Activation of known factories within the guest, streams
  PNG/BMP/JPEG of a frame, size/format/resolution, conversion BGRA/PBGRA and CopyPixels limited with rectangle, stride and padding. Origins of the converter: 24bpp BGR/RGB, 32bpp BGRA/PBGRA/RGBA and 8bpp indexed. Frames survive the release of decoder/stream. No scaler, encoders, GIF/TIFF or multiple frames. The auxiliary loads System32 windowscodecs.dll and directly obtains the listed internal factories; it does not activate extensions by registration.
- **D3D11:**WARP with real level feature 9_1; zeroed textures BGRA8/RGBA8,
  a mip/array, without MSAA, RTV, cleaning, copying, staging of reading/Map/Unmap, Flush, kery EVENT and completion query. Binding of a RTV, viewport and ClearState. No vertices buffers, shaders, Draw, 3D pipeline or startup by D3D11_SUBRESOURCE_DATA. The 9_1 level identifies the chosen native device; it does not declare the entire D3D contract of this level.
- **DXGI:**factory/interface base, software logical adapter, association
  the own window and logical swapchain of a buffer in window. GetBuffer provides guest texture; Present copies the pixels limited to the native window. No flip, synchronization with monitor, fullscreen, sharing, host outputs or GPU passthrough. ResizeBuffers refuses direct/indirect references to buffer. GPU priority admits only zero pattern.

D3D9 returns null/D3DERR_NOTAVAILABLE. D3D12/OpenGL remain unreturned; missing imports are rejected before boot. CreateDXGIFactory1/2 can only provide the base interface; new IIDs are refused. Missing HRESULT methods return E_NOTIMPL; missing void operations register diagnosis and fail in the EndDraw/status of the device. Neutral refusal to scale does not count as implementation.

## Quotas, coverage and records

Up to 64 software resources per session, 128 COM wrappers in guest, 16 MiB of accounted data, 1 MiB by coded image, pictures/textures up to 512×512 and client up to 1280×720 to 96 DPI. Inherited GDI quotas are maintained. Unknown fields are rejected, formats/flags, invalid text, NaN/infinite and non-canonical chunks. Numbers are validated by entire IEEE-754 sorting, without depending on the guest's FP mode.

software-graphics.json separates transport status and apiHresult and registers resources, bytes and effective use: mask 1 Direct2D, 2 DirectWrite, 4 WIC, 8 This doesn't mean full coverage. analysis.json keeps the telemetry inherited; there is no new malware classifier at this stage.

The [COM](../config/software-graphics-api-matrix.json) matrix contains 465 slots of 21 interfaces, including repeated inherited methods and refusals. It is not a percentage of compatibility. Actual exports and execution are separate evidence. HeapSetInformation remains refused in the previous runtime; the independent sample ignores this failure as its original code.

## Validation and use

SoftwareGraphicsScene.cpp uses standard SDK headers/imports. The same EXE performs natively in invited DLL-free folder and MiniNT. Full pixels of resize before/later client, sharp text, hashes of PNG/BMP/JPEG pixels and WARP texture are compared. Queries, COM identity, swapchain references and release are also exercised after closing the window.

The Microsoft Direct2DHelloWorld sample is compiled without changing the sources fixed in [corpus](../config/software-graphics-corpus.json); the same EXE is natively compared and in the guest. External Fonts/EXEs are not redistributed. This result is not extrapolated to all programs.

Negative tests cover parameters, thread, foreign resources, quotas, invalid image, hardware, missing levels, shaders/Draw/scaler, new interfaces, D3D9, deferred errors, and cleaning. Tools check imports static/delay/forwarders/ordinais, API sets, cycles, modes and auxiliary identity. Inherited regression and ZIP tests are required in both settings, with hashes checked after execution.

Create volume with tools/software_graphics_runtime.py using --build, --config Release, --reference CRT-LEGAL, --schema-reference SCHEMA-LEGAL, --output BUNDLE-NEW and three --asset pointing to image.png/image.bmp/image.jpg of BUILD/software-assets/Release. Execute BUILD/Release/guardian-desktop.exe with kernel BUILD/guest/Release/guardian-kernel-software-graphics.gk, BUNDLE-NOVO/runtime.gpv, manifest.json, binding.gnt, SESSAO-NOVA and --software-graphics --seconds 30.

Sources go to Aegis and builds only to the private Aegis-builds. **14.10G** remains planned; stop after delivery F.
