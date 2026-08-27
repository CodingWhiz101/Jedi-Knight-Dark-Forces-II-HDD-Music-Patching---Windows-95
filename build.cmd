@echo off
setlocal
set ROOT=%~dp0
set OBJ=%ROOT%build
set OUT=%OBJ%\out

if not exist C:\WATCOM\owsetenv.bat goto no_watcom
call C:\WATCOM\owsetenv.bat
if errorlevel 1 goto failed
if not exist "%OBJ%" mkdir "%OBJ%"
if not exist "%OUT%" mkdir "%OUT%"

set CFLAGS=-q -bt=nt -bm -5s -os -s -I="%ROOT%src"
wcc386 %CFLAGS% -dPCMCD_DEBUG=1 -fo="%OBJ%\runtime_debug.obj" "%ROOT%src\runtime.c"
if errorlevel 1 goto failed
wcc386 %CFLAGS% -dPCMCD_DEBUG=1 -fo="%OBJ%\ds_debug.obj" "%ROOT%src\ds_backend.c"
if errorlevel 1 goto failed
wcc386 %CFLAGS% -fo="%OBJ%\runtime_quiet.obj" "%ROOT%src\runtime.c"
if errorlevel 1 goto failed
wcc386 %CFLAGS% -fo="%OBJ%\ds_quiet.obj" "%ROOT%src\ds_backend.c"
if errorlevel 1 goto failed
wcc386 %CFLAGS% -fo="%OBJ%\disc_test.obj" "%ROOT%tools\disc_test.c"
if errorlevel 1 goto failed

wlink system nt_dll name %OUT%\wincd_pcm_debug.dll file %OBJ%\runtime_debug.obj,%OBJ%\ds_debug.obj option osversion=4.0 option eliminate option map=%OBJ%\wincd_pcm_debug.map @%ROOT%src\exports.lnk
if errorlevel 1 goto failed
wlink system nt_dll name %OUT%\wincd_pcm.dll file %OBJ%\runtime_quiet.obj,%OBJ%\ds_quiet.obj option osversion=4.0 option eliminate option map=%OBJ%\wincd_pcm.map @%ROOT%src\exports.lnk
if errorlevel 1 goto failed
wlink system nt name %OUT%\DISC_TEST.EXE file %OBJ%\disc_test.obj option osversion=4.0 option eliminate option map=%OBJ%\disc_test.map
if errorlevel 1 goto failed

echo Runtime binaries built in %OUT%
exit /b 0
:no_watcom
echo Open Watcom was not found at C:\WATCOM.
exit /b 1
:failed
echo PCM/CDDA build failed.
exit /b 1
