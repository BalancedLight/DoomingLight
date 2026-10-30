@echo off
setlocal EnableExtensions
cd /d "%~dp0"
call tools\setup_msvc.bat || exit /b 1
set "DOOM_CONFIG=%~1"
if not defined DOOM_CONFIG set "DOOM_CONFIG=Release"
if /I not "%DOOM_CONFIG%"=="Release" if /I not "%DOOM_CONFIG%"=="Debug" exit /b 1
set "DOOM_OUT=build\%DOOM_CONFIG%"
set "DOOM_CRT=/MT"
if /I "%DOOM_CONFIG%"=="Debug" set "DOOM_CRT=/MTd"
set "DOOM_LAB_FLAGS="
set "DOOM_LAB_SOURCES="
if /I "%~2"=="Lab" (
    if /I not "%DOOM_CONFIG%"=="Debug" (
        echo ERROR: The lab variant requires Debug.
        exit /b 1
    )
    set "DOOM_OUT=build\Lab"
    set "DOOM_LAB_FLAGS=/DDOOM_MODE_LAB /DDOOM_LAB_HOSTILITY"
    set "DOOM_LAB_SOURCES=src\doom_death_presentation.cpp"
) else if not "%~2"=="" (
    echo ERROR: Unknown build variant.
    exit /b 1
)
if not exist "%DOOM_OUT%" mkdir "%DOOM_OUT%" || exit /b 1
set "DOOM_FLAGS=/nologo %DOOM_CRT% /W4 /WX /GS /guard:cf /I sdk /I src /I third_party\opl /I third_party\minhook\include /DWIN32_LEAN_AND_MEAN /DNOMINMAX %DOOM_LAB_FLAGS%"
if /I "%DOOM_CONFIG%"=="Release" (set "DOOM_FLAGS=%DOOM_FLAGS% /O2 /DNDEBUG") else (set "DOOM_FLAGS=%DOOM_FLAGS% /Od /Zi")
cl %DOOM_FLAGS% /std:c11 /TC /c /Fo"%DOOM_OUT%\\" src\doom_core.c src\doom_world.c src\doom_world_runtime.c src\doom_world_sprites.c src\doom_pickup_feedback.c src\doom_messages.c src\doom_vehicle_feedback.c src\doom_wad.c src\doom_hud.c src\doom_music.c src\doom_midi.c src\doom_synth.c src\doom_sf2.c || exit /b 1
cl /nologo %DOOM_CRT% /W3 /O2 /TC /c /Fo"%DOOM_OUT%\doom_tsf.obj" src\doom_tsf.c || exit /b 1
cl /nologo %DOOM_CRT% /W3 /O2 /TC /c /Fo"%DOOM_OUT%\opl3.obj" third_party\opl\opl3.c || exit /b 1
cl /nologo %DOOM_CRT% /W3 /WX- /GS /guard:cf /I third_party\minhook\include /DWIN32_LEAN_AND_MEAN /TC /c /Fo"%DOOM_OUT%\\" third_party\minhook\src\buffer.c third_party\minhook\src\hook.c third_party\minhook\src\trampoline.c third_party\minhook\src\hde\hde64.c || exit /b 1
cl %DOOM_FLAGS% /std:c++17 /EHsc /LD /Fo"%DOOM_OUT%\\" /Fe"%DOOM_OUT%\DoomMode.dll" src\mod.cpp src\doom_engine.cpp src\doom_menu_guard.cpp src\doom_combat.cpp src\doom_deaths.cpp src\doom_hostility.cpp src\doom_movement.cpp src\doom_viewmodel.cpp src\doom_fpp_body.cpp src\doom_damage_direction.cpp src\doom_player_voice.cpp src\doom_interaction_presentation.cpp src\doom_world_renderer.cpp src\doom_world_lights.cpp src\doom_world_display.cpp src\doom_hud_host.cpp src\doom_audio.cpp src\wad_discovery.cpp %DOOM_LAB_SOURCES% "%DOOM_OUT%\doom_core.obj" "%DOOM_OUT%\doom_world.obj" "%DOOM_OUT%\doom_world_runtime.obj" "%DOOM_OUT%\doom_world_sprites.obj" "%DOOM_OUT%\doom_pickup_feedback.obj" "%DOOM_OUT%\doom_messages.obj" "%DOOM_OUT%\doom_vehicle_feedback.obj" "%DOOM_OUT%\doom_wad.obj" "%DOOM_OUT%\doom_hud.obj" "%DOOM_OUT%\doom_music.obj" "%DOOM_OUT%\doom_midi.obj" "%DOOM_OUT%\doom_synth.obj" "%DOOM_OUT%\doom_sf2.obj" "%DOOM_OUT%\doom_tsf.obj" "%DOOM_OUT%\opl3.obj" "%DOOM_OUT%\buffer.obj" "%DOOM_OUT%\hook.obj" "%DOOM_OUT%\trampoline.obj" "%DOOM_OUT%\hde64.obj" /link /machine:x64 /dynamicbase /nxcompat /highentropyva /guard:cf /PDBALTPATH:DoomMode.pdb kernel32.lib user32.lib advapi32.lib bcrypt.lib xaudio2.lib ole32.lib || exit /b 1
if /I "%DOOM_CONFIG%"=="Release" python tools\stage_package.py --module "%DOOM_OUT%\DoomMode.dll" || exit /b 1
