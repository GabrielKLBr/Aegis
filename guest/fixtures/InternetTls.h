#pragma once
#include "InternetBase.h"
#include <wincrypt.h>
namespace netapp {
struct TrustAnchor {br_x509_trust_anchor value{};BYTE dn[512]{},key[1024]{};};
struct TlsState;
struct X509Capture {const br_x509_class* vtable=nullptr;TlsState* owner=nullptr;UINT certificate=0;};
struct TlsState {
    br_ssl_client_context client{};br_x509_minimal_context validator{};
    TrustAnchor anchors[4]{};br_x509_trust_anchor trust[4]{};
    X509Capture capture{};BYTE buffer[BR_SSL_BUFSIZE_BIDI]{},peer[8192]{};
    UINT peerBytes=0;bool overflow=false;
};
UINT TrustAnchors(TrustAnchor*,UINT);
bool ServerCertificate(const BYTE*,UINT);
bool TlsInitialize(TlsState&,const char*);
DWORD TlsPump(TlsState&,UINT,DWORD,const volatile LONG* cancelled,bool handshake);
DWORD TlsWrite(TlsState&,UINT,const void*,UINT,UINT&,DWORD,const volatile LONG*);
DWORD TlsRead(TlsState&,UINT,void*,UINT,UINT&,DWORD,const volatile LONG*);
}
