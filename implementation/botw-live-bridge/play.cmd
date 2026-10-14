@echo off
rem Starts a session to play: Cemu with the bridge, and Minecraft. Close Cemu to end it.
cd /d "%~dp0"
python -B native_route.py play --evidence gameplay
if errorlevel 1 pause
