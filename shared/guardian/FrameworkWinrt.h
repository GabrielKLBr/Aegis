#pragma once
#include <inspectable.h>
#include <winstring.h>
namespace guardian::frameworks {
// Native ABI of the three selected IReference<T> specializations. GUIDs follow
// the WinRT parameterized-interface algorithm; Int32/Boolean also match SDK IDL.
struct __declspec(uuid("548cefbd-bc8a-5fa0-8df2-957440fc8bf4")) __declspec(novtable) ReferenceInt32: IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Value(INT32*)=0;
};
struct __declspec(uuid("3c00fd60-2950-5939-a21a-2d12c5a01b8a")) __declspec(novtable) ReferenceBoolean: IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Value(boolean*)=0;
};
struct __declspec(uuid("fd416dfb-2a07-52eb-aae3-dfce14116c05")) __declspec(novtable) ReferenceString: IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Value(HSTRING*)=0;
};
}
