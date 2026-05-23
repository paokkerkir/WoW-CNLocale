@echo off
setlocal
cd /d "%~dp0"

if not exist build mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" -A Win32
if %errorlevel% neq 0 (
    echo CMake configuration failed.
    pause & exit /b 1
)

cmake --build . --config Release
if %errorlevel% neq 0 (
    echo Build failed.
    pause & exit /b 1
)

echo.
echo Done. Output: build\bin\Release\CNLocale.dll
pause
