MoonTRCK source export
Generated: 2026-11-03 18:28:08
From:      C:\Users\Jordan Solans\Documents\GitHub\pockettracker - Copie (2)\pockettracker - Copie

Build the PC version:
    cmake --build ".\shell\build-pc" -j
    .\shell\build-pc\Debug\pockettracker-sdl.exe

Build the handheld version (Docker, aarch64):
    powershell -ExecutionPolicy Bypass -File .\shell\build-moontrck.ps1
    artifact: .\build\portmaster-moontrck\moontrck.zip

The granular editor is MOONSET:
    native/ui/modules/moonset.h / .cpp
    native/ui/dispatch/screens/moonset.cpp
    native/granular/granular-voice.h
