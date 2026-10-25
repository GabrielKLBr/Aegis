#include "PeRuntimeApi.h"
extern "C" __declspec(dllexport) uint32_t Win32PluginProbe(){return guardian::win::GetCurrentProcessId();}
extern "C" int Win32PluginEntry(void*,uint32_t,void*){return 1;}
const void* volatile Win32PluginRelocationAnchor=reinterpret_cast<const void*>(Win32PluginProbe);
