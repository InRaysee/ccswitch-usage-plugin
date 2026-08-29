@echo off
setlocal

REM ============================================================
REM  CCSwitchUsage - TrafficMonitor plugin build script
REM  Requires: MinGW-w64 (gcc/g++), curl
REM  Output:   bin\CCSwitchUsage.dll
REM ============================================================

set ROOT=%~dp0..
set SQLITE_DIR=%ROOT%\third_party\sqlite
set SQLITE_YEAR=2026
set SQLITE_VER=3530400

where gcc >nul 2>nul
if errorlevel 1 (
    echo [ERROR] gcc not found. Install MinGW-w64 and add it to PATH.
    exit /b 1
)
where g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++ not found. Install MinGW-w64 and add it to PATH.
    exit /b 1
)

REM ---- Download SQLite amalgamation if missing ----
if not exist "%SQLITE_DIR%\sqlite3.c" (
    echo [INFO] Downloading SQLite amalgamation...
    where curl >nul 2>nul
    if errorlevel 1 (
        echo [ERROR] curl not found. Please manually download sqlite-amalgamation and
        echo         extract sqlite3.c / sqlite3.h to %SQLITE_DIR%
        exit /b 1
    )
    mkdir "%SQLITE_DIR%" 2>nul
    pushd "%SQLITE_DIR%"
    curl -L -o sqlite-amalgamation.zip "https://www.sqlite.org/%SQLITE_YEAR%/sqlite-amalgamation-%SQLITE_VER%.zip"
    if errorlevel 1 (
        echo [ERROR] Download failed.
        popd
        exit /b 1
    )
    powershell -NoProfile -Command "Expand-Archive -Path sqlite-amalgamation.zip -DestinationPath . -Force"
    move /Y "sqlite-amalgamation-%SQLITE_VER%\*" . >nul
    rmdir /S /Q "sqlite-amalgamation-%SQLITE_VER%" 2>nul
    del sqlite-amalgamation.zip
    popd
)

REM ---- Build ----
echo [INFO] Compiling sqlite3.o...
gcc -O2 -c "%SQLITE_DIR%\sqlite3.c" -o "%SQLITE_DIR%\sqlite3.o"
if errorlevel 1 (
    echo [ERROR] sqlite3 compile failed.
    exit /b 1
)

echo [INFO] Linking CCSwitchUsage.dll...
if not exist "%ROOT%\bin" mkdir "%ROOT%\bin"
g++ -shared -static -O2 -I"%SQLITE_DIR%" -o "%ROOT%\bin\CCSwitchUsage.dll" "%ROOT%\src\CCSwitchUsage.cpp" "%SQLITE_DIR%\sqlite3.o" -lgdi32 -lshell32
if errorlevel 1 (
    echo [ERROR] Link failed.
    exit /b 1
)

echo [OK] Built: %ROOT%\bin\CCSwitchUsage.dll
endlocal
