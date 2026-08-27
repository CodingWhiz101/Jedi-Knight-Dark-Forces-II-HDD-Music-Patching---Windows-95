# Building and releases

Ordinary users should use the accepted binaries in `bin/win95`. Rebuilding is for maintainers and requires:

- Open Watcom 2.0 installed at `C:\WATCOM`.
- Python 3.
- GNU `objdump` under the supported MSYS2 paths.
- A local, validated `MUSIC_PCM` cache for the DirectSound self-tests.

Build candidate binaries:

```bat
build.cmd
```

They are written under `build\out`; accepted prebuilt files are not overwritten. Build flags remain Win32/Pentium `-5s`, native static runtime, and PE OS/subsystem 4.0.

Build and validate a distributable toolkit:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Build-Release.ps1 -PcmCache C:\local\MUSIC_PCM
```

The release process applies the PE, import, exact-export, Win95 API, and whole-code Pentium ISA gates; runs `/SELFTEST` against both DLL variants; refuses binaries differing from the hardware-approved hashes; and scans the ZIP for game or soundtrack content.

Any runtime source change creates an unqualified candidate. Update the accepted binary hashes only after repeating the Windows 95 hardware test described in `HARDWARE_ACCEPTANCE.md`.
