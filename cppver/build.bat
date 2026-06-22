@echo off
chcp 65001 > nul

echo === RomajiTxted C++ Build ===

if not exist build mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo.
    echo MSVC not found. Trying MinGW...
    cd ..
    if not exist build-mingw mkdir build-mingw
    cd build-mingw
    cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 (
        echo CMake configure failed.
        pause
        exit /b 1
    )
    cmake --build . --config Release
) else (
    cmake --build . --config Release
)

if errorlevel 1 (
    echo Build FAILED.
    pause
    exit /b 1
)

echo.
echo Build SUCCESS.
pause
