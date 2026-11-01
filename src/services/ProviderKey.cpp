#include "Services.h"
// Provider keys stay in the current user's Windows Credential Manager.
#include <wincred.h>
#include <algorithm>
namespace av::services {
std::string ReputationKey(const wchar_t* target) {
    PCREDENTIALW credential=nullptr;
    if(!CredReadW(target,CRED_TYPE_GENERIC,0,&credential)) {
        if(GetLastError()==ERROR_NOT_FOUND)return {};
        WinError("Read protected provider credential");
    }
    std::string key;
    try {
        Check(credential->CredentialBlobSize<=512,"Provider credential exceeds limit");
        key.assign(reinterpret_cast<const char*>(credential->CredentialBlob),credential->CredentialBlobSize);
    } catch(...) {CredFree(credential);throw;}
    SecureZeroMemory(credential->CredentialBlob,credential->CredentialBlobSize);CredFree(credential);
    return key;
}
void SaveReputationKey(std::string& key,const wchar_t* target) {
    Check(!key.empty()&&key.size()<=512&&std::all_of(key.begin(),key.end(),[](unsigned char c){return c>=33&&c<=126;}),"Use a nonempty printable provider key (maximum 512 bytes)");
    CREDENTIALW credential{};credential.Type=CRED_TYPE_GENERIC;credential.TargetName=const_cast<wchar_t*>(target);
    credential.CredentialBlobSize=DWORD(key.size());credential.CredentialBlob=reinterpret_cast<BYTE*>(key.data());
    credential.Persist=CRED_PERSIST_LOCAL_MACHINE;credential.UserName=const_cast<wchar_t*>(L"MalwareBazaar");
    const auto ok=CredWriteW(&credential,0);const auto error=GetLastError();SecureZeroMemory(key.data(),key.size());key.clear();
    if(!ok)WinError("Save protected provider credential",error);
}
void RemoveReputationKey(const wchar_t* target) {
    if(!CredDeleteW(target,CRED_TYPE_GENERIC,0)&&GetLastError()!=ERROR_NOT_FOUND)WinError("Remove protected provider credential");
}
} // namespace av::services
