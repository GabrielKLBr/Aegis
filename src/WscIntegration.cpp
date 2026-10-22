// WscIntegration.cpp
#include "WscIntegration.h"

#include <wbemidl.h>
#include <comdef.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "wbemuuid.lib")

namespace av {

// Decodes the productState bitmask exposed by AntiVirusProduct.
// Layout (per byte): [unused][provider][security state][signature state]
static std::wstring DecodeProductState(uint32_t state) {
    uint32_t security  = (state >> 8) & 0xFF;  // 0x10 = on, 0x00 = off
    uint32_t signature = (state >> 0) & 0xFF;  // 0x00 = up to date, 0x10 = outdated

    std::wstring s = L"enabled=";
    s += (security == 0x10 || security == 0x11) ? L"YES" : L"NO";
    s += L", definitions=";
    s += (signature == 0x00) ? L"up-to-date" : L"OUT-OF-DATE";
    return s;
}

bool ListSecurityProducts() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool needUninit = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE) { needUninit = false; hr = S_OK; }
    if (FAILED(hr)) {
        PrintLine(L"COM init failed.", Color::Red);
        return false;
    }

    // Process-wide security; ignore "already called" result.
    CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
                         RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
                         nullptr, EOAC_NONE, nullptr);

    IWbemLocator*  locator = nullptr;
    IWbemServices* services = nullptr;
    IEnumWbemClassObject* enumerator = nullptr;
    bool result = false;

    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IWbemLocator, (LPVOID*)&locator);
    if (FAILED(hr)) { PrintLine(L"Cannot create WbemLocator.", Color::Red); goto cleanup; }

    hr = locator->ConnectServer(_bstr_t(L"ROOT\\SecurityCenter2"),
                                nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
    if (FAILED(hr)) {
        PrintLine(L"Cannot connect to ROOT\\SecurityCenter2 (need a desktop SKU + rights).", Color::Red);
        goto cleanup;
    }

    CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                      RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    hr = services->ExecQuery(_bstr_t(L"WQL"),
                             _bstr_t(L"SELECT displayName, productState, pathToSignedProductExe "
                                     L"FROM AntiVirusProduct"),
                             WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                             nullptr, &enumerator);
    if (FAILED(hr)) { PrintLine(L"AntiVirusProduct query failed.", Color::Red); goto cleanup; }

    PrintLine(L"Antivirus products registered with Windows Security Center:", Color::Cyan);
    PrintLine(L"(this is the exact list the Windows Security panel reads)", Color::Gray);
    PrintLine(L"", Color::Default);

    {
        int count = 0;
        for (;;) {
            IWbemClassObject* obj = nullptr;
            ULONG returned = 0;
            if (enumerator->Next(WBEM_INFINITE, 1, &obj, &returned) != S_OK || returned == 0)
                break;

            VARIANT vName; VariantInit(&vName);
            VARIANT vState; VariantInit(&vState);
            VARIANT vPath;  VariantInit(&vPath);

            obj->Get(L"displayName", 0, &vName, nullptr, nullptr);
            obj->Get(L"productState", 0, &vState, nullptr, nullptr);
            obj->Get(L"pathToSignedProductExe", 0, &vPath, nullptr, nullptr);

            std::wstring name = (vName.vt == VT_BSTR && vName.bstrVal) ? vName.bstrVal : L"(unknown)";
            uint32_t state = (vState.vt == VT_I4 || vState.vt == VT_UI4) ? (uint32_t)vState.lVal : 0;

            wchar_t hexState[16];
            wsprintfW(hexState, L"0x%06X", state);

            PrintLine(L"  * " + name, Color::Green);
            PrintLine(L"      state " + std::wstring(hexState) + L"  (" + DecodeProductState(state) + L")",
                      Color::Gray);
            if (vPath.vt == VT_BSTR && vPath.bstrVal)
                PrintLine(L"      exe   " + std::wstring(vPath.bstrVal), Color::Gray);

            VariantClear(&vName);
            VariantClear(&vState);
            VariantClear(&vPath);
            obj->Release();
            ++count;
        }
        if (count == 0)
            PrintLine(L"  (no products registered)", Color::Yellow);
        result = true;
    }

cleanup:
    if (enumerator) enumerator->Release();
    if (services)   services->Release();
    if (locator)    locator->Release();
    if (needUninit) CoUninitialize();
    return result;
}

void AttemptRegister(const std::wstring& displayName) {
    PrintLine(L"Attempting Windows Security Center registration for:", Color::Cyan);
    PrintLine(L"  " + displayName, Color::Green);
    PrintLine(L"", Color::Default);

    if (!IsElevated())
        PrintLine(L"Note: not running elevated. Registration always needs Administrator.", Color::Yellow);

    // The registration entry point lives in wscapi.dll. We resolve it
    // dynamically so the program still builds/links on any SDK, and so we can
    // report precisely what happens.
    HMODULE wsc = LoadLibraryW(L"wscapi.dll");
    if (!wsc) {
        PrintLine(L"wscapi.dll not available on this system.", Color::Red);
        return;
    }

    // HRESULT WscRegisterSecurityProduct(PCWSTR path, DWORD flags, ...);
    // This export is reserved for Microsoft Virus Initiative members; calling it
    // from an unsigned, non-PPL process returns an access-denied / not-allowed
    // HRESULT. We surface that outcome instead of pretending it worked.
    typedef HRESULT (WINAPI *PFN_Register)(PCWSTR);
    auto pRegister = reinterpret_cast<PFN_Register>(
        GetProcAddress(wsc, "WscRegisterSecurityProduct"));

    if (!pRegister) {
        PrintLine(L"Registration export is not callable on this build of Windows.", Color::Yellow);
    } else {
        HRESULT hr = pRegister(displayName.c_str());
        wchar_t buf[32];
        wsprintfW(buf, L"0x%08X", (unsigned)hr);
        if (SUCCEEDED(hr)) {
            PrintLine(L"Registration reported success (HRESULT " + std::wstring(buf) + L").", Color::Green);
        } else {
            PrintLine(L"Registration rejected by Windows (HRESULT " + std::wstring(buf) + L").", Color::Red);
        }
    }

    FreeLibrary(wsc);

    PrintLine(L"", Color::Default);
    PrintLine(L"To actually appear in the Windows Security panel you must:", Color::Cyan);
    PrintLine(L"  1. Join the Microsoft Virus Initiative (MVI).", Color::Gray);
    PrintLine(L"  2. Ship an ELAM driver so your service runs as a Protected", Color::Gray);
    PrintLine(L"     Process Light (anti-malware-light).", Color::Gray);
    PrintLine(L"  3. Sign all binaries with the MVI-issued certificate.", Color::Gray);
    PrintLine(L"Only then will Windows let the product register and let Defender", Color::Gray);
    PrintLine(L"hand over real-time protection. This is by design.", Color::Gray);
}

} // namespace av
