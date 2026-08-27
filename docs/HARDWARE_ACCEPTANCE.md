# Hardware acceptance

Validated target:

- Windows 95 OSR2
- Intel Pentium MMX 233 (P5/MMX; no CMOV, P6 FPU conditionals, or SSE)
- Approximately 128 MiB RAM
- Soundscape Playback device through classic DirectSound

The evidence sequence compared baseline, no-audio trace, WaveOut, and DirectSound probes. Trace introduced no noticeable regression. WaveOut failed with `MMSYSERR_ALLOCATED`. DirectSound played concurrently with effects and showed no backend errors, underruns, recoveries, or scheduling gaps.

The final continuous-loop debug run lasted about 19 minutes. The 12–13 program completed at approximately 7:25 and entered its repeat immediately. Jedi Knight polled roughly 52 seconds later; the shim reported logical stop and adopted the matching replacement play without restarting audio. It repeated and adopted again, then handled explicit stops and close cleanly. The quiet build subsequently played Levels 1 and 2 with continuous music and normal effects/responsiveness.

Accepted SHA-256 values:

- `wincd_pcm.dll`: `799E72113FCA8794E347A84F6C35117E2EBA1C33C7DF797A809E48AAF1D15CDB`
- `wincd_pcm_debug.dll`: `B2BB644709F98ED47A9AB24E7DC80FD51D9DA9812D94F0BF0C58AD1EB36001A6`
- `DISC_TEST.EXE`: `C2D6DB45AA8E7E68F374324A23D22FD6FFE2061A02475F7C7C616F8964F755B5`

The accepted raw log and validation report are retained under `evidence`. A new hardware cycle is required only when runtime code or these binary hashes change.
