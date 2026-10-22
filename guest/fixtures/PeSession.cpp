// Owned benign application: durable progress before a real timer wait.
#define GUARDIAN_COMPLETE_ENTRY GuardianEarlierSessionProof
#include "PeCompleteConsole.cpp"
#undef GUARDIAN_COMPLETE_ENTRY
namespace {
void SessionText(const char* text) {
    uint32_t n=0,done=0;while(text[n])++n;
    WriteFile(GetStdHandle(uint32_t(-11)),text,n,&done,nullptr);
}
void SessionProgress(const char* prefix,uint32_t value) {
    char text[64]{},digits[10]{};uint32_t count=0,n=0;
    while(prefix[count]) {text[count]=prefix[count];++count;}
    do {digits[n++]=char('0'+value%10);value/=10;}while(value);
    while(n) text[count++]=digits[--n];text[count++]='\n';SessionText(text);
}
}
extern "C" uint32_t GuardianEntry() {
    const auto proof=GuardianEarlierSessionProof();if(proof)return proof;
    const auto invalid=reinterpret_cast<Handle>(UINT64_MAX);
    const auto file=CreateFileA("C:\\session.dat",0xC0000000,3,nullptr,4,0,nullptr);
    REQUIRE(file!=invalid,260);
    const bool existing=GetLastError()==183;
    uint32_t prior=0,bytes=0;
    if(existing) REQUIRE(ReadFile(file,&prior,4,&bytes,nullptr) && bytes==4 && prior<250,261);
    uint32_t progress=prior+1;
    REQUIRE(SetFilePointerEx(file,0,nullptr,0) && WriteFile(file,&progress,4,&bytes,nullptr) && bytes==4 && FlushFileBuffers(file),262);
    SessionProgress(prior?"12D RECOVER ":"12D WAIT ",prior);
    const auto before=GetTickCount64();Sleep(1000);
    REQUIRE(GetTickCount64()-before>=1000,263);
    progress=prior+2;
    REQUIRE(SetFilePointerEx(file,0,nullptr,0) && WriteFile(file,&progress,4,&bytes,nullptr) && bytes==4 && FlushFileBuffers(file) && CloseHandle(file),264);
    SessionProgress("12D DONE ",progress);
    return 0;
}
