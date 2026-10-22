@echo off
REM Build GuardianAV with the Microsoft Visual C++ compiler (cl.exe).
REM Run this from a "x64 Native Tools Command Prompt for VS".
setlocal

where cl >nul 2>nul
if errorlevel 1 (
    echo [ERROR] 'cl' not found.
    echo         Open "x64 Native Tools Command Prompt for VS 2022" and run build.bat there,
    echo         or use build-mingw.bat if you have MinGW-w64 / g++.
    exit /b 1
)

if not exist build mkdir build

cl /nologo /std:c++17 /EHsc /W4 /O2 /DUNICODE /D_UNICODE ^
   src\Util.cpp src\Sha256.cpp src\SignatureDatabase.cpp src\Scanner.cpp ^
   src\Quarantine.cpp src\RealTimeMonitor.cpp src\WscIntegration.cpp src\main.cpp ^
   /Fe:build\av.exe /Fo:build\ ^
   /link bcrypt.lib ole32.lib oleaut32.lib wbemuuid.lib advapi32.lib shell32.lib

if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

copy /Y signatures.txt build\signatures.txt >nul
echo.
echo [OK] Built build\av.exe
echo      Try:  build\av.exe eicar build\test.txt  ^&^&  build\av.exe scan build\test.txt
