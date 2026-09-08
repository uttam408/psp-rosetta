"""End-to-end + unit tests. Runs under pytest or `python -m unittest`."""

from __future__ import annotations

import struct
import subprocess
import sys
import unittest
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO))

from pipeline.adapters import get_adapter          # noqa: E402
from pipeline.adapters.base import slug            # noqa: E402
from pipeline.convert import convert_asset         # noqa: E402
from pipeline.ir import Kind, Manifest             # noqa: E402
from pipeline.pack import build_pak                # noqa: E402

PY = str(REPO / ".venv" / "bin" / "python")
FIXTURE = REPO / "build" / "_fixture" / "luftrauser"


def ensure_fixture() -> None:
    if not FIXTURE.exists():
        subprocess.run([PY, str(REPO / "tests" / "make_fixture.py"), str(FIXTURE)],
                       check=True)


class TestSlug(unittest.TestCase):
    def test_slug(self):
        self.assertEqual(slug("Player Ship!"), "player_ship")
        self.assertEqual(slug("DefineBits (7)"), "definebits_7")
        self.assertEqual(slug("///"), "unnamed")


class TestFlashAdapter(unittest.TestCase):
    def setUp(self):
        ensure_fixture()
        self.man = get_adapter("flash").crawl("luftrauser", FIXTURE)

    def test_symbol_names_applied(self):
        ids = {a.id for a in self.man.assets}
        self.assertIn("image/playership", ids)      # 3.png via symbols.csv
        self.assertIn("data/leveltable", ids)       # binaryData/5.bin via symbols.csv

    def test_kinds(self):
        counts = self.man.counts()
        self.assertEqual(counts.get("image"), 4)
        self.assertEqual(counts.get("audio"), 3)
        self.assertEqual(counts.get("data"), 1)

    def test_music_role_by_name(self):
        roles = {a.id: a.role for a in self.man.assets if a.kind is Kind.AUDIO}
        self.assertEqual(roles["audio/music_loop"], "music")
        self.assertTrue(all(r == "sfx" for k, r in roles.items() if "music" not in k))

    def test_no_duplicate_ids(self):
        ids = [a.id for a in self.man.assets]
        self.assertEqual(len(ids), len(set(ids)))


class TestTextureConvert(unittest.TestCase):
    def setUp(self):
        ensure_fixture()
        self.man = get_adapter("flash").crawl("luftrauser", FIXTURE)
        self.out = REPO / "build" / "_test" / "assets"

    def _one(self, needle, cfg):
        asset = next(a for a in self.man.assets if needle in a.id and a.kind is Kind.IMAGE)
        return convert_asset(asset, self.out, {"textures": cfg})

    def test_ptx_header_and_pot(self):
        rec = self._one("playership", {"format": "rgba5551", "swizzle": True})
        data = (self.out / rec["file"]).read_bytes()
        self.assertEqual(data[:4], b"PTX1")
        w, h, fmt, flags, pal = struct.unpack("<HHBBH", data[4:12])
        self.assertEqual((w, h), (32, 16))          # 30x14 padded to POT
        self.assertEqual(rec["content_w"], 30)
        self.assertEqual(fmt, 1)                     # rgba5551
        self.assertTrue(flags & 1)                  # swizzled
        self.assertEqual(len(data), 12 + w * h * 2)

    def test_formats(self):
        for fmt, code, bpp in [("rgba8888", 0, 4), ("rgba4444", 2, 2)]:
            rec = self._one("playership", {"format": fmt, "swizzle": False})
            data = (self.out / rec["file"]).read_bytes()
            _, _, f, _, _ = struct.unpack("<HHBBH", data[4:12])
            self.assertEqual(f, code)
            self.assertEqual(len(data), 12 + 32 * 16 * bpp)

    def test_idx8_has_palette(self):
        rec = self._one("explosionparticle", {"format": "idx8", "swizzle": False})
        data = (self.out / rec["file"]).read_bytes()
        w, h, fmt, _, pal = struct.unpack("<HHBBH", data[4:12])
        self.assertEqual(fmt, 3)
        self.assertGreater(pal, 0)
        self.assertEqual(len(data), 12 + pal * 4 + w * h)

    def test_max_size_downscale(self):
        rec = self._one("logo", {"format": "rgba8888", "max_size": 64})
        self.assertLessEqual(max(rec["content_w"], rec["content_h"]), 64)


class TestSheetSlicer(unittest.TestCase):
    def test_plan_atlas_fits_512(self):
        from pipeline.convert.textures import _plan_atlas
        # 14 frames of 64x64 (largeexplosion) — fits without downscale
        scale, dfw, dfh, cols, rows, aw, ah = _plan_atlas(64, 64, 14)
        self.assertEqual(scale, 1.0)
        self.assertLessEqual(aw, 512)
        self.assertLessEqual(ah, 512)
        self.assertGreaterEqual(cols * rows, 14)

    def test_plan_atlas_downscales_when_needed(self):
        from pipeline.convert.textures import _plan_atlas
        # 4 frames of 320x160 (cloud) — can't fit 512 at native size
        scale, dfw, dfh, cols, rows, aw, ah = _plan_atlas(320, 160, 4)
        self.assertLess(scale, 1.0)
        self.assertLessEqual(aw, 512)
        self.assertLessEqual(ah, 512)

    def test_pack_sheet_frame_rects(self):
        from pipeline.convert.textures import _pack_sheet
        strip = Image.new("RGBA", (64, 16), (0, 0, 0, 0))       # 4x 16x16
        atlas, meta = _pack_sheet(strip, 16, 16)
        self.assertEqual(meta["frame_count"], 4)
        self.assertEqual(len(meta["frames"]), 4)
        for x, y, w, h in meta["frames"]:
            self.assertLessEqual(x + w, atlas.width)
            self.assertLessEqual(y + h, atlas.height)
        self.assertEqual(atlas.size, (64, 16))

    def test_end_to_end_sheet_record(self):
        ensure_fixture()
        man = get_adapter("flash").crawl("luftrauser", FIXTURE)
        asset = next(a for a in man.assets if a.id == "image/logo")
        out = REPO / "build" / "_test" / "sheet"
        rec = convert_asset(asset, out, {"textures": {
            "format": "rgba5551", "sheets": {"image/logo": [40, 40]}}})
        self.assertTrue(rec["sheet"])
        self.assertEqual(rec["frame_count"], 3)                 # 120x40 / 40x40
        data = (out / rec["file"]).read_bytes()
        self.assertEqual(data[:4], b"PTX1")


class TestSwizzleRoundTrip(unittest.TestCase):
    def test_unswizzle_matches(self):
        from pipeline.convert.textures import _swizzle
        bw, h = 64, 16                              # 4x2 blocks
        src = bytes((i * 7) & 0xFF for i in range(bw * h))
        sw = _swizzle(src, bw, h)
        self.assertEqual(len(sw), len(src))
        # reverse using the same index math
        out = bytearray(len(sw))
        rowblocks = bw // 16
        for y in range(h):
            for x in range(bw):
                block = ((x >> 4) + (y >> 3) * rowblocks) << 7
                out[y * bw + x] = sw[block + (x & 15) + ((y & 7) << 4)]
        self.assertEqual(bytes(out), src)


class TestEndToEnd(unittest.TestCase):
    def test_build_produces_pak(self):
        ensure_fixture()
        bd = REPO / "build" / "luftrauser"
        r = subprocess.run(
            [PY, "-m", "pipeline.cli", "build", "--game", "luftrauser",
             "--src", str(FIXTURE)],
            capture_output=True, text=True, cwd=REPO,
        )
        self.assertEqual(r.returncode, 0, r.stderr)
        pak = bd / "assets.pak"
        self.assertTrue(pak.exists())
        data = pak.read_bytes()
        self.assertEqual(data[:4], b"PAK1")
        count = struct.unpack("<I", data[4:8])[0]
        self.assertEqual(count, 9)                  # @index + 4 img + 3 audio + 1 data
        self.assertTrue((bd / "assets.manifest.json").exists())

    def test_index_entry_present(self):
        ensure_fixture()
        bd = REPO / "build" / "luftrauser"
        subprocess.run([PY, "-m", "pipeline.cli", "build", "--game", "luftrauser",
                        "--src", str(FIXTURE)], capture_output=True, text=True, cwd=REPO)
        # the @index entry holds the AIDX binary asset table
        raw = (bd / "assets.pak").read_bytes()
        self.assertIn(b"AIDX", raw)


if __name__ == "__main__":
    unittest.main()
