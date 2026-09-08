"""Generate a synthetic JPEXS-export-shaped directory so the pipeline can run
end to end without any real game assets.

    python tests/make_fixture.py <out-dir>
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw


def make_sprite(path: Path, size, color, shape="ellipse", soft=False) -> None:
    img = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    box = [2, 2, size[0] - 3, size[1] - 3]
    a = 255
    getattr(d, shape)(box, fill=(*color, a))
    if soft:
        # gradient alpha to exercise the RGBA8888 / auto path
        for y in range(size[1]):
            for x in range(size[0]):
                r, g, b, pa = img.getpixel((x, y))
                if pa:
                    img.putpixel((x, y), (r, g, b, int(pa * (x / size[0]))))
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)


def make_audio(path: Path, seconds: float, freq: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["ffmpeg", "-y", "-f", "lavfi", "-i",
         f"sine=frequency={freq}:duration={seconds}", "-ac", "1", str(path)],
        capture_output=True, check=True,
    )


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    root = Path(sys.argv[1])
    (root / "images").mkdir(parents=True, exist_ok=True)
    (root / "sounds").mkdir(parents=True, exist_ok=True)
    (root / "binaryData").mkdir(parents=True, exist_ok=True)

    make_sprite(root / "images" / "3.png", (30, 14), (240, 60, 60), "rectangle")
    make_sprite(root / "images" / "7.png", (48, 48), (80, 200, 255), "ellipse", soft=True)
    make_sprite(root / "images" / "12.png", (17, 9), (255, 220, 40), "rectangle")
    make_sprite(root / "images" / "logo.png", (120, 40), (255, 255, 255), "rectangle")

    make_audio(root / "sounds" / "1.wav", 0.25, 880)    # sfx
    make_audio(root / "sounds" / "2.wav", 0.4, 220)     # sfx
    make_audio(root / "sounds" / "music_loop.wav", 10.0, 330)  # music (by name + length)

    (root / "binaryData" / "5.bin").write_bytes(b"LEVELDATA\x00" + bytes(range(64)))

    (root / "symbols.csv").write_text(
        "3;PlayerShip\n7;ExplosionParticle\n12;Bullet\n5;LevelTable\n"
    )

    print(f"fixture written to {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
