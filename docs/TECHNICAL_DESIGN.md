# Technical design

Jedi Knight's GOG executable normally imports a local OGG-aware `winmm.dll`. Windows 95 treats the system WinMM module specially, so the toolkit changes the import name to `WINCD.dll`. The supported executable differs at only offsets `0x151091` and `0x151092`; every other byte remains stock.

The proxy exports exactly the ten functions imported by `JK.EXE`. Non-CD MCI, real AUX, timer, and joystick operations are forwarded to the absolute system `WINMM.DLL`. CD-targeted MCI and one synthetic CD AUX device are handled locally. Initialization is lazy and `DllMain` performs no file, audio, DLL-loading, or thread work.

Audio is predecoded stereo signed 16-bit PCM. Classic DirectSound uses `DSSCL_NORMAL`, a software secondary streaming buffer, normal worker priority, integer volume control, and cached file handles. The runtime accepts a uniform 22.05 or 44.1 kHz set.

Retail mapping is fixed: logical 12–18 map to disc 1 physical 2–8, and logical 22–32 map to disc 2 physical 2–12. The 19–21 gap and raw physical requests are invalid.

Each request has an immutable initial plan and repeat plan. At the initial audible endpoint, the worker streams directly into the retail repeat plan while MCI exposes logical completion. When Jedi Knight eventually polls and submits its replacement `PLAY`, a frame-identical request adopts the active stream without a click, gap, or restart.

Runtime OGG decoding was rejected because the rebuilt codec path caused measurable lag on the Pentium MMX. WaveOut was rejected because the Soundscape driver returned `MMSYSERR_ALLOCATED` while the game owned audio. DirectSound PCM coexisted with effects without errors or underruns.
