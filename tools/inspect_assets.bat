@echo off
setlocal EnableExtensions
cd /d "%~dp0.."
call tools\setup_msvc.bat || exit /b 1
if not exist build\inspect mkdir build\inspect || exit /b 1
cl /nologo /MT /W4 /WX /std:c11 /TC /I src /Fo"build\inspect\\" /Febuild\inspect\inspect_assets.exe tools\inspect_assets.c src\doom_core.c src\doom_wad.c src\doom_hud.c || exit /b 1
build\inspect\inspect_assets.exe %*
