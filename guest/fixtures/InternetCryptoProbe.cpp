#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
#include <ncrypt.h>
#include <cstdio>
#include <cstring>
#ifndef INTERNET_PERSIST_ONLY
#define INTERNET_PERSIST_ONLY 0
#endif
namespace {
UINT checks=0;
void Check(bool value,const char* name){++checks;if(!value){std::printf("Internet crypto FAIL: %s, error=%lu\n",name,GetLastError());ExitProcess(81);}}
void Hex(const char* text,BYTE* output,UINT bytes){for(UINT i=0;i<bytes;++i){const auto digit=[](char c){return c<='9'?c-'0':c-'A'+10;};output[i]=BYTE((digit(text[i*2])<<4)|digit(text[i*2+1]));}}
[[maybe_unused]] void Hashes(){BYTE digest[32]{},expected[32]{};Hex("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",expected,32);BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;Check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA256 provider");DWORD objectBytes=0,written=0;Check(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<BYTE*>(&objectBytes),4,&written,0)>=0&&written==4&&objectBytes>0&&objectBytes<=4096,"hash object size");auto* object=static_cast<BYTE*>(HeapAlloc(GetProcessHeap(),0,objectBytes));Check(object!=nullptr,"hash storage");Check(BCryptCreateHash(algorithm,&hash,object,objectBytes,nullptr,0,0)>=0,"hash create");Check(BCryptHashData(hash,reinterpret_cast<BYTE*>(const_cast<char*>("a")),1,0)>=0&&BCryptHashData(hash,reinterpret_cast<BYTE*>(const_cast<char*>("bc")),2,0)>=0,"hash incremental update");Check(BCryptFinishHash(hash,digest,32,0)>=0&&!std::memcmp(digest,expected,32),"SHA256 known answer");Check(BCryptDestroyHash(hash)>=0&&BCryptCloseAlgorithmProvider(algorithm,0)>=0,"hash cleanup");HeapFree(GetProcessHeap(),0,object);BYTE key[20];std::memset(key,0x0B,sizeof(key));Hex("B0344C61D8DB38535CA8AFCEAF0BF12B881DC200C9833DA726E9376C2E32CFF7",expected,32);Check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)>=0,"HMAC provider");Check(BCryptCreateHash(algorithm,&hash,nullptr,0,key,sizeof(key),0)>=0&&BCryptHashData(hash,reinterpret_cast<BYTE*>(const_cast<char*>("Hi There")),8,0)>=0&&BCryptFinishHash(hash,digest,32,0)>=0&&!std::memcmp(digest,expected,32),"RFC4231 HMAC");Check(BCryptDestroyHash(hash)>=0&&BCryptCloseAlgorithmProvider(algorithm,0)>=0,"HMAC cleanup");}
[[maybe_unused]] void Aes(){BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_KEY_HANDLE key=nullptr;BYTE secret[16]{},plain[16]{},cipher[16]{},output[16]{},nonce[12]{},tag[16]{},expected[16]{},expectedTag[16]{};Hex("0388DACE60B6A392F328C2B971B2FE78",expected,16);Hex("AB6E47D42CEC13BDF53A67B21257BDDF",expectedTag,16);Check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_AES_ALGORITHM,nullptr,0)>=0,"AES provider");Check(BCryptSetProperty(algorithm,BCRYPT_CHAINING_MODE,reinterpret_cast<BYTE*>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),sizeof(BCRYPT_CHAIN_MODE_GCM),0)>=0,"GCM mode");Check(BCryptGenerateSymmetricKey(algorithm,&key,nullptr,0,secret,16,0)>=0,"AES key");BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;BCRYPT_INIT_AUTH_MODE_INFO(auth);auth.pbNonce=nonce;auth.cbNonce=12;auth.pbTag=tag;auth.cbTag=16;ULONG written=0;Check(BCryptEncrypt(key,plain,16,&auth,nullptr,0,cipher,16,&written,0)>=0&&written==16&&!std::memcmp(cipher,expected,16)&&!std::memcmp(tag,expectedTag,16),"NIST AES128 GCM vector");Check(BCryptDecrypt(key,cipher,16,&auth,nullptr,0,output,16,&written,0)>=0&&!std::memcmp(output,plain,16),"GCM decrypt");tag[0]^=1;std::memset(output,0x55,16);Check(BCryptDecrypt(key,cipher,16,&auth,nullptr,0,output,16,&written,0)<0,"GCM bad tag rejected");
#ifndef INTERNET_REFERENCE
 for(const auto b:output)Check(b==0x55,"unauthenticated plaintext not published");
#endif
 Check(BCryptDestroyKey(key)>=0,"AES cleanup");Check(BCryptSetProperty(algorithm,BCRYPT_CHAINING_MODE,reinterpret_cast<BYTE*>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),sizeof(BCRYPT_CHAIN_MODE_CBC),0)>=0,"CBC mode");BYTE iv[16]{};for(UINT i=0;i<16;++i)iv[i]=BYTE(i);Hex("2B7E151628AED2A6ABF7158809CF4F3C",secret,16);Hex("6BC1BEE22E409F96E93D7E117393172A",plain,16);Hex("7649ABAC8119B246CEE98E9B12E9197D",expected,16);Check(BCryptGenerateSymmetricKey(algorithm,&key,nullptr,0,secret,16,0)>=0&&BCryptEncrypt(key,plain,16,nullptr,iv,16,cipher,16,&written,0)>=0&&!std::memcmp(cipher,expected,16),"NIST AES128 CBC vector");Check(BCryptDestroyKey(key)>=0&&BCryptCloseAlgorithmProvider(algorithm,0)>=0,"CBC cleanup");}
[[maybe_unused]] void Signatures(){BYTE blob[104]{};BCRYPT_ECCKEY_BLOB header{BCRYPT_ECDSA_PRIVATE_P256_MAGIC,32};std::memcpy(blob,&header,8);Hex("60FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB6",blob+8,32);Hex("7903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299",blob+40,32);Hex("C9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721",blob+72,32);BYTE hash[32]{},expected[64]{},signature[64]{};Hex("AF2BDBE1AA9B6EC1E2ADE1D694F41FC71A831D0268E9891562113D8A62ADD1BF",hash,32);Hex("EFD48B2AACB6A8FD1140DD9CD45E81D69D2C877B56AAF991C34D0EA84EAF3716F7CB1C942D657C41D436C7A1B6E29F65F3E900DBB9AFF4064DC4AB2F843ACDA8",expected,64);BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_KEY_HANDLE key=nullptr;Check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)>=0&&BCryptImportKeyPair(algorithm,nullptr,BCRYPT_ECCPRIVATE_BLOB,&key,blob,sizeof(blob),0)>=0,"RFC6979 private key import");Check(BCryptVerifySignature(key,nullptr,hash,32,expected,64,0)>=0,"RFC6979 public verification");ULONG written=0;Check(BCryptSignHash(key,nullptr,hash,32,signature,64,&written,0)>=0&&written==64&&BCryptVerifySignature(key,nullptr,hash,32,signature,64,0)>=0,"ECDSA sign and verify");
#ifndef INTERNET_REFERENCE
 Check(!std::memcmp(signature,expected,64),"RFC6979 deterministic signing");
#endif
 signature[0]^=1;Check(BCryptVerifySignature(key,nullptr,hash,32,signature,64,0)<0,"bad ECDSA signature rejected");Check(BCryptDestroyKey(key)>=0,"signature key cleanup");Check(BCryptGenerateKeyPair(algorithm,&key,256,0)>=0&&BCryptFinalizeKeyPair(key,0)>=0,"random P256 key generation");BYTE publicBlob[72]{};Check(BCryptExportKey(key,nullptr,BCRYPT_ECCPUBLIC_BLOB,publicBlob,sizeof(publicBlob),&written,0)>=0&&written==72,"P256 public export");Check(BCryptDestroyKey(key)>=0&&BCryptCloseAlgorithmProvider(algorithm,0)>=0,"P256 cleanup");BYTE first[32]{},second[32]{};Check(BCryptGenRandom(nullptr,first,32,BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0&&BCryptGenRandom(nullptr,second,32,BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0&&std::memcmp(first,second,32),"independent random outputs");}
void Persisted(){NCRYPT_PROV_HANDLE provider=0;NCRYPT_KEY_HANDLE key=0;Check(NCryptOpenStorageProvider(&provider,MS_KEY_STORAGE_PROVIDER,0)==ERROR_SUCCESS,"virtual KSP provider");BYTE hash[32]{},signature[64]{};Hex("AF2BDBE1AA9B6EC1E2ADE1D694F41FC71A831D0268E9891562113D8A62ADD1BF",hash,32);[[maybe_unused]] DWORD written=0;
#if INTERNET_PERSIST_ONLY
 Check(NCryptOpenKey(provider,&key,L"phase149-key",0,0)==ERROR_SUCCESS,"persisted key reopened");DWORD read=0;const auto file=CreateFileW(L"C:\\App\\phase149-sign.bin",GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);Check(file!=INVALID_HANDLE_VALUE&&ReadFile(file,signature,64,&read,nullptr)&&read==64&&CloseHandle(file),"persistent signature reopened");Check(NCryptVerifySignature(key,nullptr,hash,32,signature,64,0)==ERROR_SUCCESS,"signature survives session");Check(NCryptDeleteKey(key,0)==ERROR_SUCCESS,"persisted key delete");
#else
#ifdef INTERNET_REFERENCE
 const wchar_t* name=nullptr;const DWORD createFlags=0;
#else
 const wchar_t* name=L"phase149-key";const DWORD createFlags=NCRYPT_OVERWRITE_KEY_FLAG;
#endif
 Check(NCryptCreatePersistedKey(provider,&key,NCRYPT_ECDSA_P256_ALGORITHM,name,0,createFlags)==ERROR_SUCCESS&&NCryptFinalizeKey(key,0)==ERROR_SUCCESS,"KSP P256 key finalized");Check(NCryptSignHash(key,nullptr,hash,32,signature,64,&written,0)==ERROR_SUCCESS&&written==64&&NCryptVerifySignature(key,nullptr,hash,32,signature,64,0)==ERROR_SUCCESS,"KSP signing contract");BYTE publicBlob[72]{};Check(NCryptExportKey(key,0,BCRYPT_ECCPUBLIC_BLOB,nullptr,publicBlob,72,&written,0)==ERROR_SUCCESS&&written==72,"KSP public export");
#ifndef INTERNET_REFERENCE
 BYTE privateBlob[104]{};Check(NCryptExportKey(key,0,BCRYPT_ECCPRIVATE_BLOB,nullptr,privateBlob,104,&written,0)!=ERROR_SUCCESS,"private export denied by default");const auto file=CreateFileW(L"C:\\App\\phase149-sign.bin",GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);DWORD done=0;Check(file!=INVALID_HANDLE_VALUE&&WriteFile(file,signature,64,&done,nullptr)&&done==64&&CloseHandle(file),"signature persisted in guest files");
#endif
 Check(NCryptFreeObject(key)==ERROR_SUCCESS,"KSP key close");
#endif
 Check(NCryptFreeObject(provider)==ERROR_SUCCESS,"KSP provider close");}
}
int main(){
#if !INTERNET_PERSIST_ONLY
 Hashes();Aes();Signatures();
#endif
 Persisted();std::printf("Internet crypto known answers, authenticated failures and persistence PASS checks=%u\n",checks);return 0;}
