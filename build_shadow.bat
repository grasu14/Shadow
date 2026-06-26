@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo Failed to initialize VC environment
    exit /b 1
)
echo VC environment initialized
where cl
where nmake
cd /d "C:\Users\ddeni\Desktop\Shadow"
rmdir /s /q build 2>nul
mkdir build
"C:\Program Files\CMake\bin\cmake.exe" -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug
if errorlevel 1 (
    echo CMake configure failed
    exit /b 1
)
"C:\Program Files\CMake\bin\cmake.exe" --build build
