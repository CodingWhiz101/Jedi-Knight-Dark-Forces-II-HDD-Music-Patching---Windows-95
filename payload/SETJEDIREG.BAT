@echo off
set GAMEPATH=
if "%1"=="" goto usage
if "%OS%"=="Windows_NT" goto argnt
set GAMEPATH=%1
goto havepath
:argnt
set GAMEPATH=%~1
:havepath
if not exist "%GAMEPATH%\JK.EXE" goto badpath
set INFFILE=%GAMEPATH%\~jkreg.inf
if exist "%INFFILE%" del "%INFFILE%"
echo [Version]> "%INFFILE%"
echo Signature="$CHICAGO$">> "%INFFILE%"
echo Provider=JediKnightHDD>> "%INFFILE%"
echo.>> "%INFFILE%"
echo [DefaultInstall]>> "%INFFILE%"
echo AddReg=JK.Reg>> "%INFFILE%"
echo.>> "%INFFILE%"
echo [JK.Reg]>> "%INFFILE%"
echo HKLM,"Software\LucasArts Entertainment Company\JediKnight">> "%INFFILE%"
echo HKLM,"Software\LucasArts Entertainment Company\JediKnight\v1.0">> "%INFFILE%"
echo HKLM,"Software\LucasArts Entertainment Company\JediKnight\v1.0","Executable",,"%GAMEPATH%\JK.EXE">> "%INFFILE%"
echo HKLM,"Software\LucasArts Entertainment Company\JediKnight\v1.0","Install Path",,"%GAMEPATH%">> "%INFFILE%"
echo HKLM,"Software\LucasArts Entertainment Company\JediKnight\v1.0","Source Path",,"%GAMEPATH%">> "%INFFILE%"
echo HKLM,"Software\LucasArts Entertainment Company\JediKnight\v1.0","Source Dir",,"%GAMEPATH%">> "%INFFILE%"
echo HKLM,"Software\LucasArts Entertainment Company\JediKnight\v1.0","Sound File",,"%GAMEPATH%\Resource\Res1hi.gob">> "%INFFILE%"
echo HKLM,"Software\LucasArts Entertainment Company\JediKnight\v1.0","CDPath",,"C:">> "%INFFILE%"
echo HKLM,"Software\LucasArts Entertainment Company\JediKnight\v1.0","InstallType",0x00010001,9>> "%INFFILE%"
echo HKLM,"Software\LucasArts Entertainment Company\JediKnight\v1.0","bHighResGraphicsInstall",0x00010001,1>> "%INFFILE%"
echo HKLM,"Software\LucasArts Entertainment Company\JediKnight\v1.0","bLowRes",0x00010001,0>> "%INFFILE%"
rundll32.exe setupapi.dll,InstallHinfSection DefaultInstall 132 %INFFILE%
if errorlevel 1 goto inffail
if exist "%INFFILE%" del "%INFFILE%"
echo Jedi Knight registry setup completed for %GAMEPATH%.
goto end
:usage
echo Usage: SETJEDIREG.BAT D:\GAMES\DARKF2
echo Use a short path without spaces or a trailing backslash.
goto end
:badpath
echo ERROR: JK.EXE was not found at %GAMEPATH%.
goto end
:inffail
echo ERROR: Registry setup failed. Temporary INF retained at %INFFILE%.
:end
set GAMEPATH=
set INFFILE=
pause
