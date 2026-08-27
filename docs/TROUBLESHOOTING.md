# Troubleshooting

## Build PC

- **Unsupported JK.EXE:** this release intentionally accepts only the documented GOG hash. Do not bypass the check; report the new hash for review.
- **FFmpeg not found:** pass `-Ffmpeg C:\path\ffmpeg.exe`, set `PCMCD_FFMPEG`, or place it on `PATH`.
- **PCM cache rejected:** regenerate it from the same GOG source. Cache source hashes, WAV hashes, rate, frames, and retail mapping must all agree.
- **Output already exists:** choose a new path. The toolkit will not merge with or overwrite a nonempty installation.
- **Conversion failure:** the final output is not promoted. Remove nothing from the GOG source; inspect the error and rerun.

## Windows 95

- Run `VERIFY95.BAT` from the game directory.
- Confirm `SETJEDIREG.BAT` was given the exact final path.
- Confirm no GOG `winmm.dll` or Vorbis DLL is present.
- Confirm `MUSIC_PCM` has tracks 12–18 and 22–32, but no 19–21.
- Run `RESTORE_QUIET.BAT` for normal play.
- For diagnosis, run `SELECT_DEBUG.BAT`, reproduce once, exit normally, and run `COLLECT_LOGS.BAT`.
- A fatal quiet-build file or audio problem creates `pcmcd_error.log`.

If 22.05 kHz playback is rejected by different hardware, regenerate with `-Rate 44100`; the runtime does not need rebuilding.
