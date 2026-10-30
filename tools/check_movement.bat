@echo off
setlocal EnableExtensions
cd /d "%~dp0.."
call tools\setup_msvc.bat || exit /b 1
if not exist build\checks mkdir build\checks || exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /Isrc /Isdk /Fo"build\checks\\" /Fe"build\checks\check_movement.exe" tools\check_movement.cpp src\doom_movement.cpp || exit /b 1
build\checks\check_movement.exe || exit /b 1
echo Doom movement profile checks passed.
