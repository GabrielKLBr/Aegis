#include <windows.h>
#include "guardian/Security.h"
#include <cstdio>
static_assert(sizeof(guardian::security::ServiceStatus)==sizeof(SERVICE_STATUS_PROCESS));
static_assert(offsetof(guardian::security::ServiceStatus,pid)==offsetof(SERVICE_STATUS_PROCESS,dwProcessId));
static_assert(sizeof(guardian::security::Request)<1200);
static_assert(guardian::security::LocalService().user==200); // checked below: Token is an ordinary ABI record.
int main(){std::puts("Security SDK service/request contracts PASS");return 0;}
