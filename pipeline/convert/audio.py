"""Audio -> PSP-friendly formats via ffmpeg.

  role "sfx"   -> headerless PCM signed-16 LE, mono, reduced sample rate  (".pcm")
  role "music" -> MP3 by default (the PSP has a hardware MP3 decoder, sceMp3);
                  set convert.audio.music_codec = "vorbis" | "opus" to change.

``.vag`` (Sony ADPCM, the PSP's native compressed voice format) is intentionally
NOT produced here — ffmpeg has no VAG encoder. See PROJECT.md TODO.
"""

from __future__ import annotations

import json
import shutil
import subprocess
from pathlib import Path
from typing import Any

from ..ir import Asset

FFMPEG = shutil.which("ffmpeg")
FFPROBE = shutil.which("ffprobe")

# music_codec -> (ffmpeg encoder, file extension)
_MUSIC_CODECS = {
    "mp3": ("libmp3lame", ".mp3"),
    "vorbis": ("libvorbis", ".ogg"),
    "opus": ("libopus", ".opus"),
}


def _encoders() -> set[str]:
    if not FFMPEG:
        return set()
    out = subprocess.run([FFMPEG, "-hide_banner", "-encoders"],
                         capture_output=True, text=True).stdout
    return {ln.split()[1] for ln in out.splitlines() if ln.startswith(" ") and len(ln.split()) > 1}


def convert_audio(asset: Asset, out_dir: Path, cfg: dict[str, Any]) -> dict[str, Any]:
    if not FFMPEG:
        raise SystemExit("ffmpeg not found on PATH — required for audio conversion")

    sr = int(cfg.get("sample_rate", 22050))
    music_sr = int(cfg.get("music_sample_rate", 44100))
    music_bitrate = str(cfg.get("music_bitrate", "96k"))
    music_min_s = float(cfg.get("music_min_seconds", 8.0))
    music_codec = str(cfg.get("music_codec", "mp3")).lower()
    if music_codec not in _MUSIC_CODECS:
        raise SystemExit(f"unknown music_codec {music_codec!r}")
    enc, ext = _MUSIC_CODECS[music_codec]
    avail = _encoders()
    if enc not in avail:
        fallback = next((c for c, (e, _) in _MUSIC_CODECS.items() if e in avail), None)
        if fallback is None:
            raise SystemExit(f"ffmpeg has no usable music encoder (wanted {enc})")
        enc, ext = _MUSIC_CODECS[fallback]
        music_codec = fallback

    dur = _duration(asset.source)
    role = asset.role or ("music" if dur is not None and dur >= music_min_s else "sfx")

    dst_stem = out_dir / _safe(asset.id)
    dst_stem.parent.mkdir(parents=True, exist_ok=True)

    if role == "music":
        dst = dst_stem.with_suffix(ext)
        _run([FFMPEG, "-y", "-i", str(asset.source), "-vn", "-ac", "2",
              "-ar", str(music_sr), "-c:a", enc, "-b:a", music_bitrate, str(dst)])
        rec = {"codec": music_codec, "sample_rate": music_sr, "channels": 2}
    else:
        dst = dst_stem.with_suffix(".pcm")
        _run([FFMPEG, "-y", "-i", str(asset.source), "-vn", "-ac", "1",
              "-ar", str(sr), "-f", "s16le", "-acodec", "pcm_s16le", str(dst)])
        frames = dst.stat().st_size // 2
        rec = {"codec": "pcm_s16le", "sample_rate": sr, "channels": 1, "frames": frames}

    return {
        "id": asset.id, "kind": asset.kind.value, "role": role,
        "file": str(dst.relative_to(out_dir)),
        "duration_s": round(dur, 3) if dur is not None else None,
        "bytes": dst.stat().st_size, **rec,
    }


def _duration(path: Path) -> float | None:
    if not FFPROBE:
        return None
    try:
        out = subprocess.run(
            [FFPROBE, "-v", "error", "-show_entries", "format=duration",
             "-of", "json", str(path)],
            capture_output=True, text=True, check=True,
        ).stdout
        return float(json.loads(out)["format"]["duration"])
    except (subprocess.CalledProcessError, KeyError, ValueError):
        return None


def _run(cmd: list[str]) -> None:
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"ffmpeg failed:\n{r.stderr.strip()[-2000:]}")


def _safe(asset_id: str) -> str:
    return asset_id.replace("..", "_")
