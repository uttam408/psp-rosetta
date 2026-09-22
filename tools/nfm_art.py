#!/usr/bin/env python3
"""Builds the XMB art for the NFM EBOOT from the game's own images.zip (nothing is committed).

usage: nfm_art.py <need-for-madness-source> <outdir>
writes <outdir>/ICON0.PNG (144x80) and <outdir>/PIC1.PNG (480x272)."""
import io, sys, zipfile
from pathlib import Path
from PIL import Image

src, out = Path(sys.argv[1]), Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
z = zipfile.ZipFile(src / "OBJ/data/images.zip")
bg = Image.open(io.BytesIO(z.read("logomadbg.jpg"))).convert("RGB")     # 670x400, skull head top centre
logo = Image.open(io.BytesIO(z.read("logomad.png"))).convert("RGBA")    # 367x41 red title

# PIC1: background scaled to cover 480x272, title logo across the lower part
s = max(480 / bg.width, 272 / bg.height)
pic = bg.resize((round(bg.width * s), round(bg.height * s)), Image.LANCZOS)
pic = pic.crop(((pic.width - 480) // 2, 0, (pic.width - 480) // 2 + 480, 272))
lw = 400
l2 = logo.resize((lw, round(logo.height * lw / logo.width)), Image.LANCZOS)
pic.paste(l2, ((480 - lw) // 2, 272 - l2.height - 24), l2)
pic.save(out / "PIC1.PNG")

# ICON0: skull crop (the head fills the centre of the 144x80 tile) with the title under it
head = bg.crop((203, 0, 203 + 288, 160)).resize((144, 80), Image.LANCZOS)
ic = head.copy()
lw = 132
l3 = logo.resize((lw, round(logo.height * lw / logo.width)), Image.LANCZOS)
ic.paste(l3, ((144 - lw) // 2, 80 - l3.height - 3), l3)
ic.save(out / "ICON0.PNG")
