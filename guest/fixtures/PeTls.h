#pragma once
#include <stdint.h>
// Minimal compiler TLS metadata for owned, CRT-free fixtures. These are ordinary
// linked PE images, not hand-patched machine-code buffers.
extern "C" {
uint32_t _tls_index=0;
#pragma section(".tls",read,write)
#pragma section(".tls$ZZZ",read,write)
__declspec(allocate(".tls")) char _tls_start=0;
__declspec(allocate(".tls$ZZZ")) char _tls_end=0;
void TlsCallback(void*,uint32_t,void*);
#pragma section(".rdata$TLS",read)
__declspec(allocate(".rdata$TLS")) void (*const tlsCallbacks[])(void*,uint32_t,void*)={TlsCallback,nullptr};
struct TlsDirectory {const void* start;const void* end;const void* index;const void* callbacks;uint32_t zero,flags;};
__declspec(allocate(".rdata$TLS")) extern const TlsDirectory _tls_used={&_tls_start,&_tls_end,&_tls_index,tlsCallbacks,0,0};
}
