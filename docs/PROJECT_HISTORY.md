# Project history

## Goal

Adapt a stock GOG Jedi Knight installation for a Windows 95 OSR2/Pentium MMX 233 machine whose CD drive is unavailable, while retaining music, effects, retail sequencing, and acceptable performance.

## Abandoned OGG approach

The first approach patched `JK.EXE` to import `WINCD.dll` and rebuilt an OGG WinMM proxy for Pentium. Modern MinGW support objects repeatedly introduced instructions unavailable on P5 hardware, including CMOV, `fucomip`, and SSE2. After object filtering and improvised CRT substitutions removed illegal opcodes, runtime OGG decoding still produced noticeable lag and unreliable music. That implementation is not shipped.

## Backend evidence

An Open Watcom `-5s` evidence proxy measured MCI traffic and tried WaveOut and classic DirectSound using deterministic PCM. Trace was responsive. WaveOut could not share the Soundscape device with the game. DirectSound PCM and effects coexisted with no errors, underruns, or scheduling gaps, so DirectSound was selected.

## Functional PCM shim

The production proxy uses 18 predecoded tracks at 22.05 kHz stereo 16-bit PCM, exact retail disc mapping, integer TMSF arithmetic, cached WAV resources, a software DirectSound streaming buffer, integer volume scaling, and debug/quiet builds. It is built natively with Open Watcom 2.0 for Win32/Pentium and imports only Win95 system APIs.

Initial hardware testing revealed silence after a range because Jedi Knight could wait many seconds before polling MCI completion. The final transport streams directly into a provisional retail repeat, reports logical completion, and adopts the game's eventual matching replacement request without restarting audio.

## Final acceptance — 2026-08-27

The accepted debug run demonstrated two seamless provisional/adoption cycles, correct stop/close behavior, and zero audio or scheduling faults. The quiet build then completed Level 1 looping and maintained consistent music in Level 2 with solid effects and no noticeable lag. The functional objective is complete; later disc-2 gameplay is additional coverage rather than a release blocker because automated transport tests and the audible disc utility validate its files and mapping.
