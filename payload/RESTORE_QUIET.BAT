@echo off
if not exist wincd_pcm.dll goto missing
copy /y wincd_pcm.dll wincd.dll >nul
if errorlevel 1 goto failed
echo Approved quiet PCM/CDDA DLL restored.
goto end
:missing
echo ERROR: wincd_pcm.dll is missing. No file was changed.
goto end
:failed
echo ERROR: Could not restore wincd.dll.
:end
pause
