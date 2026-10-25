#include <windows.h>
#include "guardian/Exception.h"
#include <cstdio>
// SDK constants are independent of the owned handler and catch the target /
// collided-unwind flag mix-up that destroyed live objects with original CRTs.
static_assert(guardian::exceptions::TargetUnwind==EXCEPTION_TARGET_UNWIND);
static_assert(guardian::exceptions::Unwinding==EXCEPTION_UNWINDING);
static_assert(sizeof(guardian::exceptions::Context)==sizeof(CONTEXT));
static_assert(offsetof(guardian::exceptions::Context,rip)==offsetof(CONTEXT,Rip));
static_assert(offsetof(guardian::exceptions::Dispatcher,targetIp)==offsetof(DISPATCHER_CONTEXT,TargetIp));
int main(){CONTEXT context{};RtlCaptureContext(&context);if(!(context.ContextFlags&CONTEXT_CONTROL)||!context.Rip||!context.Rsp)return 1;std::puts("CRT SDK exception/context contracts PASS");return 0;}
