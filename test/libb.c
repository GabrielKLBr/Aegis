// libb.c - a DLL that IMPORTS from liba.dll and exports a function using it.
// Proves our WinLoader resolves cross-module imports and patches the IAT: the
// call to AddOne goes through libb's import table, which we fix up at load.
__declspec(dllimport) int AddOne(int x);

__declspec(dllexport) int Compute(int x) { return AddOne(x) * 2 + 8; }
