// liba.c - a leaf DLL exporting plain functions (loaded by our WinLoader).
__declspec(dllexport) int AddOne(int x) { return x + 1; }
__declspec(dllexport) int Magic(void)   { return 0x7E57; }
