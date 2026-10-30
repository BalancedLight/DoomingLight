@echo off
setlocal EnableExtensions
cd /d "%~dp0.."
call tools\setup_msvc.bat || exit /b 1
if not exist build\checks mkdir build\checks || exit /b 1
cl /nologo /W4 /WX /TC /Isrc /Fo"build\checks\\" /Fe"build\checks\check_hit_geometry.exe" tools\check_hit_geometry.c || exit /b 1
build\checks\check_hit_geometry.exe || exit /b 1
