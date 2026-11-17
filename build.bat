@echo off
setlocal
where cmake >nul 2>nul
if errorlevel 1 (
  echo CMake was not found. Install CMake and add it to PATH.
  pause
  exit /b 1
)
cmake -S . -B build -A x64
if errorlevel 1 goto :error
cmake --build build --config Release
if errorlevel 1 goto :error
echo Build complete: build\Release\PlaylistDL.exe
pause
exit /b 0
:error
echo Build failed. See the error messages above.
pause
exit /b 1
