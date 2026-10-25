#include <stdint.h>
extern "C" int DesktopGatewayEntry(void*,uint32_t,void*){return 1;}
extern "C" const int DesktopGatewayAnchor=147;
const void* volatile DesktopGatewayRelocation=&DesktopGatewayAnchor;
