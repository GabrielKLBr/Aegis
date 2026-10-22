// imgtest.c - a tiny relocatable DLL used to prove SEC_IMAGE image-section
// layout (headers + sections by RVA) AND that DIR64 base relocations fire.
//
// g_ptr holds the ABSOLUTE address of g_marker, which the linker emits as a
// DIR64 base relocation. If the image-section loader applied relocations, then
// after mapping *g_ptr == 0xC0FFEE5. If it did NOT, g_ptr still points at the
// DLL's preferred ImageBase (outside the mapped view) and the value is wrong.
//
// Probe() loads g_ptr (RIP-relative -> base-independent) then dereferences it,
// so calling Probe() from the guest exercises: executable code placed at the
// right RVA, correct section layout, and relocation fixups -- all at once.
//
// Build (MinGW, no CRT, no entry):
//   gcc -shared -nostdlib -Wl,--entry=0 -o imgtest.dll imgtest.c

typedef long long i64;

static i64  g_marker = 0xC0FFEE5LL;
static i64* g_ptr    = &g_marker;     // absolute address -> DIR64 relocation

__declspec(dllexport) i64 Probe(void) { return *g_ptr; }
