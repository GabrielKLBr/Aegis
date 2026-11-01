# 14.10E — GDI enlarged in host windows

Delimited delivery for x64 applications in the profile **NT 10.0.19045**. **--expanded-gdi** mode adds GDI operations to the native bridge. Windows draws text, fonts, curves, regions and bitmaps in the session windows; application, DLLs and callbacks remain in the MiniNT. Images of the application may cross the limited contract. A rastered window previously in the guest does not count as acceptance this way.

## Protocol and contracts

Kernel guardian-kernel-expanded-gdi.gk, capacity GuardianExpandedGdi/5/guest, protocol v5 and auxiliary guardian-expanded-gdi-host.exe. The manifest links kernel, volume, profile, mode and SHA-256 The A–D variants remain separate. Command (972 bytes), Reply (896) and Packet (3260) preserve the previous size; magic/version distinguish the new variant. There are no native host hands, procedures or hands on the guest.

The [versioned matrix](../config/gdi-api-matrix.json) contains **411 entries**: 358 with restricted implementation, 31 pending and 22 rejected by policy. Inputs include internal variants and aliases in different DLLs; this does not mean 358 universal APIs or independent implementations. The matrix records ANSI/Unicode, forwarding, dependencies and limits. The inventory confers real exports and gaps, but does not replace the execution of the tests. The report records separately the status of the bridge and the return of the native API.

MessageBeep remains exported by compatibility, returns explicit failure and records diagnosis; audio is pending from 14.10G. TranslateMessage in guest receives input that the helper has already translated; character synthesis from posted keys is not declared. Pending APIs without export, such as wsprintfA/W, are rejected in inventory before execution.

## Zoom implemented

- Curves: Ellipse, RoundRect, Arc, Chord, Pie, Polyline, Polygon, PolyBezier,
  To and PolyPolygon variants; pens with hachured styles and brushes.
- Regions: elipses/rectangles rounded, combination, displacement, limits,
  intersection with point/rectangle, combined cutting, exclusion and displacement.
- Paths: beginning/end/cancellation, closure, filling, contour,
  conversion into region, cut-off, flattening and enlargement; limited reading.
- Status: SaveDC/RestoreDC inherited preserve the native states; modes of
  mapping, origins/extensions, LP/DP conversion, related transformations, arc direction, ROP2, stretch modes and DC object colors.
- Raster: BitBlt/StretchBlt/PatBlt with ROPs enumeradas, SetPixel, DIBs BI_RGB
  24/32 bits in both directions, application memory synchronization with native bitmap and StretchDIBits SRCOPY.
- Text: A variants in CP1252 and W in UTF-16 validated, alignment, spacing,
  justification, ExtTextOut with clipping/opacity/progresses, glyph indexes, cumulative extensions, ABC widths, metrics and font descriptors.
- Catalog additional: Tahoma, Verdana, Times New Roman, Courier New, Calibri,
  Cambria and Georgia, in addition to previous families. Requests remain in the catalog; resolved face, metrics and glyphs are those of Windows host.
- Default cursors are applied only to session window clients;
  cursor files and global cursor display changes are not displayed.

GetObject translates descriptors; bmBits points to guest memory when applicable. Native bitmap pointers and padding bytes are not copied. Active and saved selections remain protected from exclusion.

## Compatibility measure of creation and painting

Independent examples revealed gaps beyond GDI exports:

- CW_USEDEFAULT in main windows uses the guest's logical policy:
  position 120,100 and size 640×480. It does not represent the entire position/cascade policy of a Windows installation.
- SW_SHOWDEFAULT uses the guest pattern, SW_SHOWNORMAL.
- Allowable combinations of CS_HREDRAW, CS_VREDRAW and CS_DBLCLKS correspond
  the pre-registered native classes in the auxiliary.
- InvalidateRect and UpdateWindow have different paths.
  in an asynchronous way; the second presents the already invalid region. A painting delivered is pending until EndPaint, preserving subsequent invalidations without creating extra cycles when the program invalidates before BeginPaint. If the host resizes during a painting callback, classes with CS_HREDRAW/CS_VREDRAW receive new invalidation after delivery of WM_SIZE to the guest. This avoids preserving calculated drawing with the old size.
- Consecutive native changes in position and size keep the last
  WM_WINDOWPOSCHANGED, WM_MOVE and WM_SIZE from each window while waiting for the guest. This avoids exhausting the 256 event queue by dragging a window during a long calculation. explicitly posted messages and replies do not enter this combination. A 400-factor test without consuming events and confers final geometry and cleaning.

The 12F mode regression also corrected the DPI script: a scale change uses the current native geometry, even before the guest confirms a resizing. The test forces two DPI exchanges without confirmation and confers the final logical size in a framed window. The client area uses the requested scale; the edges use the actual DPI of the HWND, which does not change when the test sends a synthetic DPI exchange. This prevents adding frame pixels to the client area for each transition. The integrated program requires a client 480×300 in 240,160 and logical DPI 96; the incidental count of two WM_SIZE messages was replaced by this verifiable state and by receiving WM_SIZE. The native history records each DPI transition at the time it happens, in addition to the frames. One goes to 144 and back to 96 before the next frame does not disappear from the evidence by combination of frames. The test forces this sequence without intermediate confirmation of the guest.

The negative locking case uses three seconds to include cold boot and requires timedOut, guest failure and first synchronized image, as well as recovery of all windows.

The 14.10D guest-bound-rectangles policy remains. The logical desktop is 1280×720 to 96 DPI. Host version, target version and differences are recorded separately; no equivalence with all NT profiles, fonts, themes and DPI settings is announced.

## Limits and refusals

32 windows/surfaces, 128 GDI entries, 32 DCs, 24 MiB of pixels and 8 MiB sent per session, in addition to the menus/icons/timers limits of 14.10D. Bitmaps have up to 512×512; points and rectangles use limited coordinates. Polys have up to 32 points per call and path up to 128 points; GetPath returns up to 32. Regions have up to 64 KiB of data and geometry limits. Transformations must be finite, limited and non-natural. Sources have limited dimensions and styles; usual texts have up to 127 units and ExtTextOut up to 64.

Accepted ROPs are listed in the contract. Negative sizes and mirroring bitmaps, tablets/paletizados formats, path or memory fonts, desktop/other capture, printing and driver exhausts are out of contract. Text within an open path is refused. AlphaBlend, metafiles, GetFontData, unrestricted enumeration of sources and other gaps are identified in the matrix.

The helper keeps restricted token, low integrity, Job with a process and 64 MiB, two inherited endpoints, sequence, authorship of objects and deadlines. Paths, regions and buffers expand the attack surface GDI; a list of names does not guarantee absence of escape. The helper is not AppContainer and its profile does not prevent all file reading. See [SECURITY.md](../SECURITY.md).

## Corpus and evidence

The SDK scene itself compares six pages of pixels and metrics between Windows and MiniNT: shapes, regions, transformations, paths, raster 24/32 bits and text. Includes input, resize, finite painting, glyphs, descriptors and cleaning.

The external corpus adds Bezier and Clover, assigned to Charles Petzold, compiled without changing the sources with the Windows SDK, in ANSI and Unicode. The same EXE runs in the native reference and MiniNT; it is not connected against the own import libraries. Customer pixels are compared in two sizes and mouse changes in Bezier. WhatSize is a negative case: wsprintfA/W remain pending, without replacing formatting by empty success.

[Origin and hashes of the examples](../config/gdi-corpus.json) fix a public mirror. [Author's website](https://www.charlespetzold.com/books/) provides the fonts; ZIP download returned HTTP 465 in this execution, so no byte comparison is declared with this ZIP. The sources and binaries of these examples are not redistributed. Compilation receipts link fonts and EXEs used. The original Flappy remains in MiniNT; it is not executed in the host.

** Final criteria:** 167 Regression Release tests, 92 Acceptance Debug, without ignored and with stable source code/binary; more ZIP testing extracted in both configurations and verification of files after execution. The package preserves evidence of development failure. The Loopback laboratory reserves TCP and UDP in independent candidates, as tracks reserved by Windows may differ between protocols. Approved Corpus is a measured cutout, not general GDI compatibility.

## Usage

Target CMake: guardian_expanded_gdi_runtime. Provide legal CRT/schema and, to reproduce the external corpus, fonts corresponding to configured hashes:

    cmake -S . -B PASTA_BUILD -DGUARDIAN_GUI_COMPATIBILITY_ONLY=ON -DGUARDIAN_EXPANDED_GDI_CORPUS=PASTA_FONTES
    cmake --build PASTA_BUILD --config Release --target guardian_expanded_gdi_runtime

Add the CRT, schema and Flappy reference options from previous phases.

    python -B tools/expanded_gdi_runtime.py --build PASTA_BUILD --config Release --reference PASTA_CRT --schema-reference SCHEMA --output PACOTE_NOVO
    PASTA_BUILD/Release/guardian-desktop.exe PASTA_BUILD/guest/Release/guardian-kernel-expanded-gdi.gk PACOTE_NOVO/runtime.gpv PACOTE_NOVO/manifest.json PACOTE_NOVO/binding.gnt SESSAO_NOVA --expanded-gdi --seconds 30

The gallery guardian-expanded-gdi-scene.exe is the default of this creator. CLI maintains the time limit of 1–30 seconds; closing the window allows normal closure. For Flappy, provide --application and the assets settings.txt/icon.ico. Outputs must be new. The package test also requires --corpus-binaries pointing to the external folder that contains Release/Debug And your receipts.

**Stop at 14.10E. DirectX/Direct2D/DirectWrite/codecs continue at 14.10F, depending on new request.**

