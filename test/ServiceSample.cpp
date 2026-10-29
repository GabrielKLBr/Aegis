// Owned, CRT-free WHP fixture. The sample is never executed on the host.
#include <intrin.h>
extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long);
extern "C" void ServiceSampleEntry() {
#ifdef AEGIS_SERVICE_WAIT_SAMPLE
    for (;;) { __nop(); }
#else
    ExitProcess(0);
#endif
}
