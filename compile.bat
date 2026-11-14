@echo off
setlocal enabledelayedexpansion

:: Ensure working directory is the project root where compile.bat lives
cd /d "%~dp0"

:: Add MSYS2 UCRT64 and Visual Studio CMake to PATH if not already present
if exist "D:\msys64\ucrt64\bin" (
    set "PATH=D:\msys64\ucrt64\bin;%PATH%"
)
if exist "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin" (
    set "PATH=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
)

:: Parse arguments:
::   compile.bat           -> build debug
::   compile.bat release   -> build release
::   compile.bat run       -> build debug and launch vcut.exe
::   compile.bat test      -> build debug and run tests
::   compile.bat clean     -> delete build folder and rebuild
set "PRESET=debug"
set "DO_RUN=0"
set "DO_TEST=0"
set "DO_CLEAN=0"

for %%A in (%*) do (
    if /i "%%~A"=="release" set "PRESET=release"
    if /i "%%~A"=="debug"   set "PRESET=debug"
    if /i "%%~A"=="run"     set "DO_RUN=1"
    if /i "%%~A"=="test"    set "DO_TEST=1"
    if /i "%%~A"=="clean"   set "DO_CLEAN=1"
)

if "%DO_CLEAN%"=="1" (
    echo [VCut] Cleaning build\%PRESET%...
    if exist "build\%PRESET%" rmdir /s /q "build\%PRESET%"
)

if not exist "build\%PRESET%\build.ninja" (
    echo [VCut] Configuring CMake preset: %PRESET%...
    cmake --preset %PRESET%
    if errorlevel 1 (
        echo [VCut] CMake configuration failed!
        exit /b 1
    )
)

echo [VCut] Building (%PRESET%)...
cmake --build --preset %PRESET%
if errorlevel 1 (
    echo [VCut] Build failed! Make sure vcut.exe is closed before compiling.
    exit /b 1
)

echo [VCut] Build succeeded: build\%PRESET%\vcut.exe

if "%DO_TEST%"=="1" (
    echo [VCut] Running tests...
    ctest --test-dir "build/%PRESET%" --output-on-failure
    if errorlevel 1 exit /b 1
)

if "%DO_RUN%"=="1" (
    echo [VCut] Launching build\%PRESET%\vcut.exe...
    start "" "build\%PRESET%\vcut.exe"
)

exit /b 0
