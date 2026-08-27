# Cleanup report

Completed 2026-08-27 after the stock-conversion toolkit, release archive, and replacement Win95 installation passed all gates.

## Preserved

- Untouched stock GOG reference: `..\Star Wars Jedi Knight - Dark Forces 2`
- Clean generated Win95 installation: `..\DARKF2_95`
- Repository-ready source, documentation, tests, payload, and accepted binaries: this directory
- Accepted hardware debug log and validation report: `evidence`
- Sanitized toolkit release ZIP and checksum: `release`

Accepted custom binary SHA-256 values:

- `wincd_pcm.dll`: `799E72113FCA8794E347A84F6C35117E2EBA1C33C7DF797A809E48AAF1D15CDB`
- `wincd_pcm_debug.dll`: `B2BB644709F98ED47A9AB24E7DC80FD51D9DA9812D94F0BF0C58AD1EB36001A6`
- `DISC_TEST.EXE`: `C2D6DB45AA8E7E68F374324A23D22FD6FFE2061A02475F7C7C616F8964F755B5`

## Final verification

- Stock reference: 97 files; tree SHA-256 `74ec3847629d0cd29107bd1c22386ab8349ff2b5a3e76e673d7c8299dcfe5bc7`
- Stock before/after digest: identical
- Win95 output: 85 files, 867.49 MiB; tree SHA-256 `11e5855b329e4b122c72364aed907728779ace3d2b750f8b08907eee1d2e22de`
- Release ZIP SHA-256: `0AA9A2E5FF751DF12E0F7A2DE3FC30F873A46C9309FA16355B2AE05F82B68339`
- Release checksum file: matched
- Forbidden game/media/save/FFmpeg files in release: zero
- Debug and quiet `/SELFTEST`: zero failures
- Audible disc mapping, including 12→13, 22→23, and track 32: zero failures
- Fresh conversion from the 18 raw GOG OGG files: passed; all PCM files and the soundtrack manifest matched the accepted cache byte-for-byte
- Final post-cleanup install validation: passed

## Permanently removed

- The superseded `DARKF2_95.legacy` tree, whose pre-cleanup tree SHA-256 was `0db505fdf30383b12a382bd58ef4fb532fc3e997f4b823a3118519844bb3d7c9`
- The complete `pcmcd_evidence` experiment and its Trace/WaveOut/DirectSound artifacts
- The abandoned MinGW/OGG proxy, codec-era configuration, filtered-object/CRT scripts, ISA scratch reports, and duplicate OGG music
- Root object, map, disassembly, smoke, Python cache, and other reproducible build files
- Temporary PCM smoke-test data and the unpacked release staging directory
- The generated `pcmcd_debug.log` from the clean output smoke test

These deleted legacy files were removed permanently after preservation, promotion, and final-path validation. They are not recoverable from this workspace. Their relevant findings survive in `docs/PROJECT_HISTORY.md`, `docs/HARDWARE_ACCEPTANCE.md`, and the accepted evidence files.
