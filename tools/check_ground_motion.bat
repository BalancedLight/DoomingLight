@echo off
setlocal EnableExtensions
cd /d "%~dp0.."
call tools\setup_msvc.bat || exit /b 1
if not exist build\checks mkdir build\checks || exit /b 1
cl /nologo /W4 /WX /TC /std:c11 /Isrc /Fo"build\checks\\" /Fe"build\checks\check_ground_motion.exe" tools\check_ground_motion.c src\doom_ground_motion.c || exit /b 1
build\checks\check_ground_motion.exe || exit /b 1
echo Doom ground-motion checks passed.
