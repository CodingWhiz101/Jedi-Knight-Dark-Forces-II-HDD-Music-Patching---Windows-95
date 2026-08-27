# Installation

## Prepare on a modern Windows PC

Install Python 3 and make an FFmpeg executable available through `-Ffmpeg`, `PCMCD_FFMPEG`, or `PATH`. The script never downloads FFmpeg. Use a destination with at least 1 GiB free.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Prepare-Win95Install.ps1 -Source "C:\GOG Games\Star Wars Jedi Knight - Dark Forces 2" -Output "C:\Transfer\DARKF2_95"
```

Optional switches:

- `-Rate 44100` selects the predefined full-rate fallback. The tested default is `22050`.
- `-PcmCache C:\Cache\MUSIC_PCM` reuses matching WAVs and avoids FFmpeg.
- `-IncludePlayerData` copies profiles and saves. Without it, only `player\dummy.txt` is created.

The output path must be absent or empty. Source validation occurs before staging. A failed run removes only its uniquely named partial directory.

Run a build-PC verification at any time:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Test-Win95Install.ps1 -Path "C:\Transfer\DARKF2_95" -Source "C:\GOG Games\Star Wars Jedi Knight - Dark Forces 2"
```

## Transfer to Windows 95

Copy the complete output directory to the Win95 PC. Preserve `MUSIC_PCM` and use a short path without spaces, preferably `D:\GAMES\DARKF2`.

1. Run `SETJEDIREG.BAT D:\GAMES\DARKF2`.
2. Run `VERIFY95.BAT`.
3. Run `DISC_TEST.EXE`; it audibly checks the disc mapping.
4. Start `JK.EXE`.

`SELECT_DEBUG.BAT` enables the diagnostic DLL. `SELECT_QUIET.BAT` and `RESTORE_QUIET.BAT` restore the production DLL. Run `COLLECT_LOGS.BAT` after a diagnostic session.

Do not copy GOG's `winmm.dll`, `libogg-0.dll`, `libvorbis-0.dll`, or `libvorbisfile-3.dll` into the generated folder.
