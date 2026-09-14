@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
  echo CMake was not found. Install Visual Studio 2022 with Desktop development with C++ and CMake tools.
  exit /b 1
)

cmake -S . -B build -A x64
if errorlevel 1 exit /b 1
cmake --build build --config Release --parallel
if errorlevel 1 exit /b 1

if not exist bin mkdir bin
copy /Y "build\Release\XRR_Layer_Studio_CPP_v2.5.1.exe" "bin\XRR_Layer_Studio_CPP_v2.5.1.exe" >nul
echo.
echo Built: bin\XRR_Layer_Studio_CPP_v2.5.1.exe
endlocal
