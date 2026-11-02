#include "Distribution.h"
#include <ncrypt.h>
#include <iostream>
using namespace aegis::distribution;
namespace {
struct Object{NCRYPT_HANDLE h=0;~Object(){if(h)NCryptFreeObject(h);}};
std::string Blob(NCRYPT_KEY_HANDLE key){DWORD n=0;Require(NCryptExportKey(key,0,BCRYPT_ECCPUBLIC_BLOB,nullptr,nullptr,0,&n,0)==ERROR_SUCCESS&&n==72,"Query public publisher key");std::vector<uint8_t> bytes(n);Require(NCryptExportKey(key,0,BCRYPT_ECCPUBLIC_BLOB,nullptr,bytes.data(),n,&n,0)==ERROR_SUCCESS,"Export public publisher key");return Hex(bytes);}
}
int wmain(int argc,wchar_t** argv){try{
    Require(argc>=2,"Use --create-key PUBLIC_HEADER or --sign FOLDER KIND SEQUENCE VERSION CONFIGURATION");Object provider,key;
    Require(NCryptOpenStorageProvider(&provider.h,MS_KEY_STORAGE_PROVIDER,0)==ERROR_SUCCESS,"Open Windows key storage");
    if(std::wstring(argv[1])==L"--create-key"){
        Require(argc==3,"Public header path required");auto status=NCryptOpenKey(provider.h,&key.h,PublisherKeyName,0,NCRYPT_SILENT_FLAG);
        if(status==NTE_BAD_KEYSET){Require(NCryptCreatePersistedKey(provider.h,&key.h,NCRYPT_ECDSA_P256_ALGORITHM,PublisherKeyName,0,0)==ERROR_SUCCESS,"Create publisher key");DWORD noExport=0,usage=NCRYPT_ALLOW_SIGNING_FLAG;Require(NCryptSetProperty(key.h,NCRYPT_EXPORT_POLICY_PROPERTY,reinterpret_cast<PBYTE>(&noExport),sizeof(noExport),0)==ERROR_SUCCESS&&NCryptSetProperty(key.h,NCRYPT_KEY_USAGE_PROPERTY,reinterpret_cast<PBYTE>(&usage),sizeof(usage),0)==ERROR_SUCCESS&&NCryptFinalizeKey(key.h,0)==ERROR_SUCCESS,"Finalize non-exportable signing key");}
        else Require(status==ERROR_SUCCESS,"Open publisher key without interaction");
        const auto blob=Blob(key.h);const fs::path header=fs::absolute(argv[2]);AtomicWrite(header,"#pragma once\n// Public verification key only; the non-exportable private key is in Windows CNG.\nnamespace aegis::distribution { inline constexpr const char* PublisherPublicKey = \""+blob+"\"; }\n");std::cout<<Json({{"publicKey",blob},{"privateKeyExported",false}}).dump()<<'\n';return 0;
    }
    Require(std::wstring(argv[1])==L"--sign"&&argc==7,"Use --sign FOLDER KIND SEQUENCE VERSION CONFIGURATION");
    Require(NCryptOpenKey(provider.h,&key.h,PublisherKeyName,0,NCRYPT_SILENT_FLAG)==ERROR_SUCCESS,"Publisher key not provisioned for this user");Require(Blob(key.h)==PublicKey(),"Publisher key does not match the compiled trust anchor");
    const auto root=fs::absolute(argv[2]);size_t consumed=0;const auto sequence=std::stoull(argv[4],&consumed);Require(consumed==wcslen(argv[4]),"Invalid release sequence");const auto manifest=MakeManifest(root,Utf8(argv[3]),sequence,Utf8(argv[5]),Utf8(argv[6]));const auto text=manifest.dump()+"\n";auto digest=Unhex(Hash({reinterpret_cast<const uint8_t*>(text.data()),text.size()}));DWORD n=0;
    Require(NCryptSignHash(key.h,nullptr,digest.data(),DWORD(digest.size()),nullptr,0,&n,NCRYPT_SILENT_FLAG)==ERROR_SUCCESS&&n==64,"Query publisher signature");std::vector<uint8_t> signature(n);Require(NCryptSignHash(key.h,nullptr,digest.data(),DWORD(digest.size()),signature.data(),n,&n,NCRYPT_SILENT_FLAG)==ERROR_SUCCESS,"Sign release manifest");AtomicWrite(root/ManifestName,text);AtomicWrite(root/SignatureName,Hex(signature)+"\n");auto verified=Verify(root,Utf8(argv[3]));std::cout<<Json({{"signed",true},{"identity",verified.identity},{"files",verified.manifest.at("files").size()}}).dump()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<Json({{"error",e.what()}}).dump()<<'\n';return 1;}}
