"""Build PSP XMB assets (ICON0.PNG, PIC1.PNG) from the game's own title art.

    .venv/bin/python tools/make_psp_icon.py

Writes runtime/icon0.png (144x80, the game-list icon) and runtime/pic1.png
(480x272, shown as the background when the game is highlighted).
"""
from pathlib import Path
from PIL import Image

REPO = Path(__file__).resolve().parent.parent
LOGO = Path("/Users/uttam/psp-rosetta/sources/luftrauser/assets/images/43_Interaction.UBoot_clLogo.png")
CREAM = (229, 221, 172, 255)   # Game.as:96 FP.screen.color


def composite(logo: Image.Image, w: int, h: int, scale: float) -> Image.Image:
    bg = Image.new("RGBA", (w, h), CREAM)
    lw = round(logo.width * scale)
    lh = round(logo.height * scale)
    resized = logo.resize((lw, lh), Image.LANCZOS)
    bg.alpha_composite(resized, ((w - lw) // 2, (h - lh) // 2))
    return bg.convert("RGB")


def main() -> int:
    logo = Image.open(LOGO).convert("RGBA")

    icon = composite(logo, 144, 80, 144 / logo.width * 0.92)
    icon.save(REPO / "runtime" / "icon0.png")

    pic1 = composite(logo, 480, 272, 480 / logo.width * 0.7)
    pic1.save(REPO / "runtime" / "pic1.png")

    print("wrote runtime/icon0.png", icon.size, "and runtime/pic1.png", pic1.size)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
