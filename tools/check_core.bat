@echo off
setlocal EnableExtensions
cd /d "%~dp0.."
call tools\setup_msvc.bat || exit /b 1
if not exist build\checks mkdir build\checks || exit /b 1
cl /nologo /W4 /WX /TC /Isrc /Fo"build\checks\\" /Fe"build\checks\check_core.exe" tools\check_core.c src\doom_core.c || exit /b 1
build\checks\check_core.exe || exit /b 1
cl /nologo /W4 /WX /std:c11 /TC /Isrc /Fo"build\checks\\" /Fe"build\checks\check_music_tracks.exe" tools\check_music_tracks.c src\doom_music.c || exit /b 1
build\checks\check_music_tracks.exe || exit /b 1
echo Doom core checks passed.
