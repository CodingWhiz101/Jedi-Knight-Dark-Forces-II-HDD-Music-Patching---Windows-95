#!/usr/bin/env python3
"""Build-PC-only deterministic Ogg Vorbis to PCM soundtrack converter."""

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

TRACKS = (
    (12, 1, 2), (13, 1, 3), (14, 1, 4), (15, 1, 5),
    (16, 1, 6), (17, 1, 7), (18, 1, 8),
    (22, 2, 2), (23, 2, 3), (24, 2, 4), (25, 2, 5),
    (26, 2, 6), (27, 2, 7), (28, 2, 8), (29, 2, 9),
    (30, 2, 10), (31, 2, 11), (32, 2, 12),
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def ogg_info(path: Path):
    serial = None
    packets = []
    packet = bytearray()
    final_granule = None
    with path.open("rb") as stream:
        while True:
            header = stream.read(27)
            if not header:
                break
            if len(header) != 27 or header[:4] != b"OggS" or header[4] != 0:
                raise ValueError(f"{path}: malformed Ogg page")
            segments = stream.read(header[26])
            if len(segments) != header[26]:
                raise ValueError(f"{path}: truncated Ogg lacing table")
            payload = stream.read(sum(segments))
            if len(payload) != sum(segments):
                raise ValueError(f"{path}: truncated Ogg payload")
            page_serial = struct.unpack_from("<I", header, 14)[0]
            if serial is None:
                serial = page_serial
            if page_serial != serial:
                continue
            granule = struct.unpack_from("<Q", header, 6)[0]
            if granule != 0xFFFFFFFFFFFFFFFF:
                final_granule = granule
            cursor = 0
            for size in segments:
                packet.extend(payload[cursor:cursor + size])
                cursor += size
                if size < 255:
                    if len(packets) < 3:
                        packets.append(bytes(packet))
                    packet.clear()
    if not packets or len(packets[0]) < 16 or packets[0][:7] != b"\x01vorbis":
        raise ValueError(f"{path}: missing Vorbis identification packet")
    version = struct.unpack_from("<I", packets[0], 7)[0]
    channels = packets[0][11]
    rate = struct.unpack_from("<I", packets[0], 12)[0]
    if version != 0 or channels != 2 or rate != 44100:
        raise ValueError(f"{path}: expected Vorbis 44.1 kHz stereo, got version={version}, channels={channels}, rate={rate}")
    if final_granule is None or final_granule <= 0:
        raise ValueError(f"{path}: missing final Vorbis granule position")
    return rate, channels, final_granule


def read_pcm_wave(path: Path):
    raw = path.read_bytes()
    if len(raw) < 12 or raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError(f"{path}: FFmpeg did not produce RIFF/WAVE")
    cursor = 12
    fmt = None
    data = None
    while cursor + 8 <= len(raw):
        tag = raw[cursor:cursor + 4]
        size = struct.unpack_from("<I", raw, cursor + 4)[0]
        start = cursor + 8
        end = start + size
        if end > len(raw):
            raise ValueError(f"{path}: truncated WAV chunk")
        if tag == b"fmt ":
            if size < 16:
                raise ValueError(f"{path}: short WAV fmt chunk")
            fmt = struct.unpack_from("<HHIIHH", raw, start)
        elif tag == b"data":
            data = raw[start:end]
        cursor = end + (size & 1)
    if fmt is None or data is None:
        raise ValueError(f"{path}: WAV lacks fmt or data")
    return fmt, data


def write_canonical_wave(path: Path, rate: int, pcm: bytes):
    if len(pcm) % 4:
        raise ValueError(f"{path}: PCM data is not frame aligned")
    byte_rate = rate * 4
    header = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 2, rate, byte_rate, 4, 16)
    header += b"data" + struct.pack("<I", len(pcm))
    path.write_bytes(header + pcm)


def find_ffmpeg(explicit=None) -> Path:
    if explicit:
        candidate = Path(explicit).expanduser()
        if candidate.is_file():
            return candidate.resolve()
        raise RuntimeError(f"FFmpeg was not found at {candidate}")
    configured = os.environ.get("PCMCD_FFMPEG")
    candidate = Path(configured).expanduser() if configured else None
    if candidate and candidate.is_file():
        return candidate.resolve()
    found = shutil.which("ffmpeg")
    if found:
        return Path(found).resolve()
    raise RuntimeError(
        "FFmpeg was not found. Install an official/current build on the build PC, "
        "then put ffmpeg on PATH or set PCMCD_FFMPEG to ffmpeg.exe. FFmpeg is never shipped to Windows 95."
    )


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="Convert Jedi Knight music to deterministic PCM WAV files")
    parser.add_argument("--source", type=Path, default=root / "MUSIC")
    parser.add_argument("--output", type=Path, default=root / "local" / "MUSIC_PCM")
    parser.add_argument("--rate", type=int, choices=(22050, 44100), default=22050)
    parser.add_argument("--ffmpeg", type=Path, help="explicit ffmpeg executable (then PCMCD_FFMPEG, then PATH)")
    parser.add_argument("--validate-only", action="store_true", help="validate the 18 source OGG files without converting")
    args = parser.parse_args()
    source = args.source.resolve()
    output = args.output.resolve()
    if not source.is_dir():
        raise FileNotFoundError(f"Source music directory is missing: {source}")
    actual = sorted(path.name.lower() for path in source.glob("*.ogg"))
    expected = sorted(f"track{logical}.ogg" for logical, _, _ in TRACKS)
    if actual != expected:
        raise ValueError("source must contain exactly tracks 12-18 and 22-32 as OGG files")
    inspected = []
    for logical, disc, physical in TRACKS:
        src = source / f"Track{logical}.ogg"
        rate, channels, granule = ogg_info(src)
        inspected.append((logical, disc, physical, src, rate, channels, granule))
    if args.validate_only:
        for logical, _, _, _, rate, channels, granule in inspected:
            print(f"Track {logical}: Vorbis {rate} Hz, {channels} channels, {granule} granules")
        print("Validated 18 GOG soundtrack files")
        return 0
    ffmpeg = find_ffmpeg(args.ffmpeg)
    version_text = subprocess.check_output([str(ffmpeg), "-version"], text=True, errors="replace")
    version_line = version_text.splitlines()[0].strip()
    ffmpeg_hash = sha256(ffmpeg)
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix="pcmcd_convert_", dir=str(output.parent)))
    rows = []
    try:
        for logical, disc, physical, src, source_rate, channels, granule in inspected:
            if args.rate == 22050:
                if granule & 1:
                    raise ValueError(f"{src}: odd source granule count cannot produce an exact half-rate frame count")
                expected_frames = granule // 2
            else:
                expected_frames = granule
            temporary = stage / f"Track{logical}.ffmpeg.wav"
            final = stage / f"Track{logical}.wav"
            command = [
                str(ffmpeg), "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
                "-i", str(src), "-map_metadata", "-1", "-vn", "-ac", "2",
                "-af", f"aresample={args.rate}:resampler=swr:dither_method=none",
                "-ar", str(args.rate), "-c:a", "pcm_s16le", str(temporary),
            ]
            subprocess.run(command, check=True)
            fmt, pcm = read_pcm_wave(temporary)
            format_tag, out_channels, out_rate, byte_rate, block_align, bits = fmt
            if (format_tag != 1 or out_channels != 2 or out_rate != args.rate or block_align != 4 or bits != 16):
                raise ValueError(f"{temporary}: unexpected FFmpeg PCM format {fmt}")
            frames = len(pcm) // 4
            difference = frames - expected_frames
            if abs(difference) > 1:
                raise ValueError(
                    f"Track {logical}: FFmpeg produced {frames} frames; expected {expected_frames} from granule {granule}"
                )
            if difference == 1:
                pcm = pcm[:-4]
            elif difference == -1:
                pcm += b"\0\0\0\0"
            write_canonical_wave(final, args.rate, pcm)
            temporary.unlink()
            rows.append((
                logical, disc, physical, sha256(src), sha256(final), source_rate,
                granule, args.rate, expected_frames, expected_frames / args.rate,
                expected_frames * 4,
            ))
            print(f"Track {logical}: {expected_frames} frames, {expected_frames * 4} bytes")
        manifest = stage / "MANIFEST.TXT"
        lines = [
            "Jedi Knight PCM/CDDA soundtrack manifest",
            f"FFmpeg version\t{version_line}",
            f"FFmpeg executable\t{ffmpeg}",
            f"FFmpeg SHA-256\t{ffmpeg_hash}",
            f"Output sample rate\t{args.rate}",
            "",
            "logical\tdisc\tphysical\tsource_sha256\twav_sha256\tsource_rate\tsource_granules\twav_rate\twav_frames\tduration_seconds\tdata_bytes",
        ]
        for row in rows:
            lines.append("\t".join((str(v) if not isinstance(v, float) else f"{v:.6f}") for v in row))
        manifest.write_text("\r\n".join(lines) + "\r\n", encoding="ascii", newline="")
        output.mkdir(parents=True, exist_ok=True)
        for forbidden in (19, 20, 21):
            if (output / f"Track{forbidden}.wav").exists():
                raise RuntimeError(f"Refusing to package invalid gap placeholder Track{forbidden}.wav; remove it first")
        for logical, _, _ in TRACKS:
            os.replace(stage / f"Track{logical}.wav", output / f"Track{logical}.wav")
        os.replace(manifest, output / "MANIFEST.TXT")
    finally:
        shutil.rmtree(stage, ignore_errors=True)
    total = sum(row[-1] for row in rows)
    print(f"Converted {len(rows)} tracks: {total} PCM bytes ({total / 1048576:.2f} MiB data)")
    print(f"Manifest: {output / 'MANIFEST.TXT'}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
