@echo off
REM Build GuardianAV with MinGW-w64 (g++). Requires g++ on PATH.
setlocal

where g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] 'g++' not found. Install MinGW-w64 (e.g. via MSYS2) or use build.bat with MSVC.
    exit /b 1
)

if not exist build mkdir build

g++ -std=c++17 -O2 -municode -DUNICODE -D_UNICODE ^
    src\Util.cpp src\Sha256.cpp src\SignatureDatabase.cpp src\Scanner.cpp ^
    src\Quarantine.cpp src\RealTimeMonitor.cpp src\WscIntegration.cpp src\main.cpp ^
    -o build\av.exe ^
    -lbcrypt -lole32 -loleaut32 -lwbemuuid -ladvapi32 -lshell32 ^
    -static-libgcc -static-libstdc++

if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

copy /Y signatures.txt build\signatures.txt >nul
echo.
echo [OK] Built build\av.exe
echo      Try:  build\av.exe eicar build\test.txt  ^&^&  build\av.exe scan build\test.txt
