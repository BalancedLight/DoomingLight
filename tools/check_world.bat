@echo off
setlocal EnableExtensions
cd /d "%~dp0.."
call tools\setup_msvc.bat || exit /b 1
if not exist build\checks mkdir build\checks || exit /b 1
cl /nologo /W4 /WX /TC /Isrc /Fo"build\checks\\" /Fe"build\checks\check_world.exe" tools\check_world.c src\doom_core.c src\doom_world.c src\doom_world_runtime.c src\doom_world_sprites.c || exit /b 1
build\checks\check_world.exe || exit /b 1
echo Doom world checks passed.
