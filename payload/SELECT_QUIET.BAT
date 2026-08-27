@echo off
if not exist JK.EXE goto wrongdir
if not exist wincd_pcm.dll goto missing
copy /y wincd_pcm.dll wincd.dll >nul
if errorlevel 1 goto failed
echo Quiet PCM/CDDA DLL selected.
goto end
:wrongdir
echo ERROR: Run this file from the Jedi Knight directory.
goto end
:missing
echo ERROR: wincd_pcm.dll is missing. No file was changed.
goto end
:failed
echo ERROR: Could not replace wincd.dll.
:end
pause
