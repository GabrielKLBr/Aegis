# 14.10C — native GDI design

Delimited Windows 10 x64 delivery in MiniNT. `--native-gdi` mode adds `gdi32.dll`, `user32.dll` and `win32u.dll` itself, v3 protocol and kernel with `GuardianNativeGdi/3/guest` capability. The auxiliary process uses actual Windows GDI to execute commands. The 12F, 14.10A and 14.10B modalities continue with their own contracts.

## Design and resources

The set implemented is in `guest/fixtures/NativeGdi32.def` and `NativeGdiUser.def`. Includes window and memory DC, BeginPaint/EndPaint, solid brushes, solid pens, allowed fonts, DIBs, selection/release, SaveDC/RestoreDC, rectangular regions, clipping, FillRect, Rectangle, MoveToEx/LineTo, TextOutW, DrawTextW, cores/mode background, metrics and text extension, BitBlt, StretchDIBits, GetPixel, GdiFlush and declared capabilities.

The calls operate on real HDC/HGDIOBJ**within the helper**. The guest receives opaque IDs linked to the session, process and thread. He does not receive HWND, HDC, pixel pointer, procedure or actual host address. Gateway does not offer a generic operation of calling functions, loading DLLs or opening devices.

Each custom window has a memory surface designed by GDI in the auxiliary. WM_PAINT and WM_PRINTCLIENT feature this surface by GDI; resizing preserves the existing content. The copy for HWND also occurs in the host. Text, lines and shapes are produced by Windows, without sending a window previously rastered by MiniNT.

Application bitmaps can cross the channel in limited blocks. The example transfers only a 16×16 bitmap to the StretchDIBits operation. The rest of the scene is produced by the GDI calls. There is no silent conversion to the 12F pixel presenter.

GetDC and BeginPaint accept only custom window from the thread and session itself; desktop DC, external window or native control is not provided. There is a window DC acquisition active by window. Memory DCs are independent. A BeginPaint acquisition has the EndPaint pair and a GetDC acquisition uses ReleaseDC. The incorrect pair returns error.

Objects selected in DCs or saved states cannot be deleted. Application Bitmaps cannot be selected simultaneously in two DCs. Restore returns selection and clipping, and DC deletion restores its initial objects. Stock objects are represented by own IDs.

## DIBs, text and DPI

The batch accepts 32-bit DIB BI_RGB, top-down, no file or palette section. CreateDIBSection returns a allocated buffer**in the guest**. Written on it are synchronized before use by GDI; GdiFlush copies back GDI changes to allow reading of the invited buffer. The program needs to use this synchronization point before reading pixels modified by GDI and before intermediate direct access with drawing. Compete writing without synchronization has no guarantee of consistency.

Sources created by the application accept the names Consoles, Arial and Segoe UI, height from −8 to −64, weight from 100 to 900 and quality without antialiasing. Declared stock sources are also available. No source file is loaded by the guest’s route. The face effectively resolved and metrics are returned by APIs. Modes and limits are explicit in the protocol validator; DrawText flags that modify the buffer are rejected.

The GDI view uses coordinates and logical capabilities of 96 DPI. The helper scales the presentation to the physical HWND. The report records the actual Windows host version and the target NT profile 10.0.19045. The pixels/metrics comparison is valid for the same machine and the registered configuration; it does not prove equivalence between Windows versions, font installations or monitor topologies.

The contracts follow, within this section, the documentation of [contexts and GDI](https://learn.microsoft.com/en-us/windows/win32/gdi/device-context-functions) states, [DIB sections and synchronization](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createdibsection) and [client area painting](https://learn.microsoft.com/en-us/windows/win32/gdi/drawing-in-the-client-area).

## Limits and supervision

- Up to 128 entries of objects, including stock, and 32 DCs.
- Up to eight window surfaces; each client can have up to 1280×720 logical pixels.
- Bitmap application up to 512×512; 24 MiB for surface pixels and bitmaps.
- Transfer in 128 pixel blocks and up to 8 MiB sent per session.
- Up to eight states saved by DC, texts up to 127 UTF-16 units and operations
  SRCCOPY for batch copy/scale.
- Inheritance of two endpoints, restricted token, low integrity, Job of one
  process and 64 private MiB, maintaining supervision of 14.10A/B.

The auxiliary profile is neither AppContainer nor prevents all file reading by process. The attack surface added by native APIs remains documented in [SECURITY.md](../SECURITY.md).

The kernel informs the end of threads and only it completes the session channel. DCs, objects and surfaces are released at the close. Falls, VM term or locked auxiliary close Job. The `native-gdi.json` file records authors, USER32/GDI operations, status, counters, remaining resources, bitmap bytes, NT versions and auxiliary process result.

## Acceptance and Use

The delivery criterion requires **152 Regression Release tests and 77 Acceptance Debug**, without ignored and with stable source code/binary. The five new tests cover reference Windows application, protocol, real GDI process, integration into MiniNT and implementation/empacotamento. rejections

The same application source SDK performs in MiniNT and native Windows. The 160×112 scene compares the hash of all pixels, metrics, extension, font face and rectangle calculated by DrawText. It also exercises BeginPaint/EndPaint, BitBlt for the window, resizing and explicit release of resources. The tests include capture of the auxiliary window itself, invalid and foreign IDs, authorship, selected/salvos objects, quotas, failures and counting of GDI before/later handles of 32 allocation cycles. Partial failure of CD creation and the total limit of bitmap bytes are also exercised.

The extracted package repeats the previous and new graphic scenarios in Release and Debug. The evidence, including errors found in development, follows the private build. Microsoft input DLLs, Flappy, ISOs, disks or personal states are not distributed.

```text
python -B tools/native_gdi_runtime.py --build PASTA-BUILD --config Release --reference PASTA-CRT --schema-reference SCHEMA-LEGAL --output pacote-gdi
PASTA-BUILD/Release/guardian-desktop.exe PASTA-BUILD/guest/Release/guardian-kernel-native-gdi.gk pacote-gdi/runtime.gpv pacote-gdi/manifest.json pacote-gdi/binding.gnt sessao-nova --native-gdi --seconds 25
```

Use new output directory. CMake Aggregated Target: `guardian_native_gdi_runtime`. The auxiliary should be next to Guardian-desktop; the manifest fixes its SHA256.

Universal GDI and USER32 are not declared. Palettes, printing/devices, external DCs, compressed formats, general ROPs, paths, transformations, arbitrary sources, full dialogues, clipboard, DirectX and WinRT do not belong to the cutout of this delivery. Acceptance of the original Flappy by this native bridge, additional controls and integration of dialogues are in the 14.10D.

**Stop at completion of 14.10C. 14.10D depends on new request.**
