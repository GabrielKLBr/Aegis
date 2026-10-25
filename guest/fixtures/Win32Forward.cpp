// KERNEL32's owned export table forwards to KERNELBASE; no host code is linked.
extern "C" const int GuardianWin32ForwarderAnchor=144;
const void* volatile GuardianWin32RelocationAnchor=&GuardianWin32ForwarderAnchor;
