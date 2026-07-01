@echo off
cmake --preset vs2022-windows -DCOPY_OUTPUT=OFF
if errorlevel 1 exit /b %errorlevel%
cmake --build build --config Release
