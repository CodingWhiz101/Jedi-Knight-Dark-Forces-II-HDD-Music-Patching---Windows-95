@echo off
echo Checking Jedi Knight Windows 95 installation...
if not exist JK.EXE goto failed
if not exist JK.EXE.bak goto failed
if not exist SMACKW32.DLL goto failed
if not exist wincd.dll goto failed
if not exist wincd_pcm.dll goto failed
if not exist wincd_pcm_debug.dll goto failed
if not exist DISC_TEST.EXE goto failed
if not exist Episode\JK1.GOB goto failed
if not exist Resource\Res1hi.gob goto failed
if not exist Resource\Res2.gob goto failed
if not exist MUSIC_PCM\MANIFEST.TXT goto failed
if not exist MUSIC_PCM\Track12.wav goto failed
if not exist MUSIC_PCM\Track18.wav goto failed
if not exist MUSIC_PCM\Track22.wav goto failed
if not exist MUSIC_PCM\Track32.wav goto failed
if exist MUSIC_PCM\Track19.wav goto failed
if exist MUSIC_PCM\Track20.wav goto failed
if exist MUSIC_PCM\Track21.wav goto failed
if exist winmm.dll goto failed
if exist libvorbisfile-3.dll goto failed
echo Basic file-layout checks passed.
echo Run DISC_TEST.EXE for the audible mapping check.
goto end
:failed
echo ERROR: This folder is incomplete or contains an incompatible GOG audio file.
:end
pause
