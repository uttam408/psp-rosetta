"""NFM format + adapter tests. Synthetic data only — no game assets in the repo."""

from __future__ import annotations

import io
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO))

from pipeline.adapters.java import JavaAdapter          # noqa: E402
from pipeline.convert import convert_asset              # noqa: E402
from pipeline.ir import Kind                            # noqa: E402
from pipeline.nfm import tables                         # noqa: E402
from pipeline.nfm.pmesh import pack_pmesh, unpack_pmesh  # noqa: E402
from pipeline.nfm.rad import (MAT_GLASS, PAINT_FIRST, PAINT_SECOND, args,  # noqa: E402
                              f32, parse_rad)
from pipeline.nfm.stage import parse_stage              # noqa: E402

CAR = """\
// car: test
1stColor(78,94,238)
2ndColor(230,51,11)
ScaleX(200)
ScaleY(100)
ScaleZ(50)
div(20)

<p>
c(78,94,238)
p(10,-10,70)
p(10,11,80)
p(10,11,35)
</p>

<p>
c(230,51,11)
gr(-9)
lightB
noOutline
p(40,-10,-68)
p(40,-10,-51)
p(30,-35,-51)
p(30,-35,-19)
</p>

<p>
glass()
p(1,2,3)
p(4,5,6)
p(7,8,9)
</p>

<p>
c(78,94,238)
glass
p(1,1,1)
</p>

rims(1,2,3,4,5)
w(-10,10,20,11,4,6)
w(10,10,20,11,4,6)
gwgr(-20)
w(10,10,-20,0,4,6)
w(-10,10,-20,0,4,6)
w(99,99,99,0,4,6)

disp(3)
tracks(2)
<track>
c(0,255,0)
xy(90)
radx(50)
rady(20)
radz(100)
ty(-5)
skid(2)
</track>
<track>
c(1,2,3)
dam
notwall
</track>
shadow
"""

STAGE = """\

name(Test|Stage)
stagemaker(Someone)
snap(-5,-5,20)
sky(207,232,255)
fog(198,219,224)
clouds(255,255,255,3,-1062)
ground(185,210,205)
texture(41,119,204,20)
fadefrom(5000)
density(3)
mountains(99406)
lightson
soundtrack(stage1,150,35000)
nlaps(4)

set(47,0,0,0)p
set(10,0,5600,180)pt
set(26,0,9000,0)pr
chk(40,0,28000,0)
chk(96,10,20,0,-500)
fix(41,-9100,10600,-1400,0)s
pile(3,4,5,6,7)
maxr(11,4800,-200)
"""


class TestTables(unittest.TestCase):
    def test_ids(self):
        self.assertEqual(tables.piece_name(10), "road")
        self.assertEqual(tables.piece_name(47), "sroad")        # stage start piece
        self.assertEqual(tables.piece_name(40), "checkpoint")
        self.assertEqual(tables.piece_name(26), "ramp30")
        self.assertIsNone(tables.piece_name(9))
        self.assertIsNone(tables.piece_name(10 + len(tables.PIECES)))
        self.assertEqual(tables.piece_id("road"), 10)
        self.assertEqual(len(tables.CARS), 16)
        self.assertEqual(len(tables.PIECES), 68)
        self.assertEqual(len(set(tables.PIECES) | set(tables.CARS)), 84)


class TestArgs(unittest.TestCase):
    def test_truncates_like_java(self):
        self.assertEqual(args("p(1.9,-2.9,3)"), [1, -2, 3])
        self.assertEqual(args("rims(1,2"), [1, 2])              # missing ')' tolerated
        self.assertEqual(args("glass"), [])

    def test_f32(self):
        self.assertNotEqual(f32(0.1), 0.1)
        self.assertEqual(f32(0.5), 0.5)


class TestRad(unittest.TestCase):
    def setUp(self):
        self.m = parse_rad(CAR, "test")

    def test_scale_and_div(self):
        # div(20) -> 2.0, ScaleX(200) -> 2.0, ScaleY(100) -> 1.0, ScaleZ(50) -> 0.5
        # p(10,-10,70): x = 10*2*2 = 40, y = -10*2*1 = -20, z = 70*2*0.5 = 70
        self.assertEqual(self.m.polys[0].verts[0], (40, -20, 70))
        self.assertEqual(self.m.polys[1].verts[0], (160, -20, -68))

    def test_paint_slots(self):
        self.assertEqual(self.m.polys[0].paint, PAINT_FIRST)
        self.assertEqual(self.m.polys[1].paint, PAINT_SECOND)

    def test_glass_not_painted(self):
        g = self.m.polys[3]                      # c(78,94,238) then glass
        self.assertEqual(g.material, MAT_GLASS)
        self.assertEqual(g.paint, 0)

    def test_poly_flags(self):
        p = self.m.polys[1]
        self.assertEqual((p.gr, p.light, p.no_outline), (-9, 2, True))
        self.assertFalse(self.m.polys[0].no_outline)      # reset at each <p>

    def test_wheels_capped_at_four(self):
        self.assertEqual(len(self.m.wheels), 4)
        self.assertEqual(self.m.wheels[0].gwgr, 0)        # gwgr comes after w0/w1...
        self.assertEqual(self.m.wheels[2].gwgr, -20)      # ...and sticks for later wheels

    def test_tracks(self):
        t0, t1 = self.m.tracks
        self.assertEqual((t0.color, t0.xy, t0.radx, t0.skid, t0.dam), ((0, 255, 0), 90, 100, 2, 1))
        self.assertEqual(t0.y, -10)
        self.assertEqual((t1.dam, t1.notwall), (3, True))

    def test_tracks_ignored_before_disp(self):
        m = parse_rad("<track>\nxy(5)\n</track>\n")
        self.assertEqual(m.tracks, [])

    def test_flags_and_misc(self):
        self.assertIn("shadow", self.m.flags)
        self.assertEqual(self.m.rims, (1, 2, 3, 4, 5))
        self.assertEqual(self.m.unknown, [])

    def test_max_r(self):
        self.assertGreater(self.m.max_r, 100)

    def test_pmesh_roundtrip(self):
        blob = pack_pmesh(self.m)
        d = unpack_pmesh(blob)
        self.assertEqual(len(d["polys"]), 4)
        self.assertEqual(len(d["indices"]), sum(len(p.verts) for p in self.m.polys))
        self.assertEqual(d["verts"][0], (40.0, -20.0, 70.0))
        self.assertEqual(d["first"], (78, 94, 238))
        self.assertEqual(d["rims"], (1, 2, 3, 4, 5))
        self.assertEqual(len(d["wheels"]), 4)
        self.assertEqual(len(d["tracks"]), 2)
        self.assertEqual(d["tracks"][1][-2:], (3, 1))     # dam, notwall
        self.assertEqual(len(blob) % 4, 0)
        # fan indices are contiguous per poly
        first_idx = d["polys"][1][0]
        self.assertEqual(d["indices"][first_idx:first_idx + 4], [3, 4, 5, 6])


class TestStage(unittest.TestCase):
    def setUp(self):
        self.s = parse_stage(STAGE, 1)

    def test_header(self):
        s = self.s
        self.assertEqual(s["name"], "Test,Stage")            # '|' -> ','
        self.assertEqual(s["maker"], "Someone")
        self.assertEqual(s["sky"], [207, 232, 255])
        self.assertEqual(s["clouds"], [255, 255, 255, 3, -1062])
        self.assertEqual(s["nlaps"], [4])
        self.assertTrue(s["lightson"])
        self.assertEqual(s["soundtrack"], {"track": "stage1", "volume": 150, "size": 35000})
        self.assertNotIn("unknown", s)

    def test_objects(self):
        o = self.s["objects"]
        self.assertEqual([x["op"] for x in o],
                         ["set", "set", "set", "chk", "chk", "fix", "pile", "maxr"])
        self.assertEqual((o[0]["model"], o[0]["flags"]), ("sroad", "p"))
        self.assertEqual((o[1]["rot"], o[1]["flags"]), (180, "pt"))
        self.assertEqual(o[3]["model"], "checkpoint")
        self.assertEqual(o[4]["y"], -500)
        self.assertTrue(o[5]["special"])
        self.assertEqual(o[6]["args"], [3, 4, 5, 6, 7])
        self.assertEqual(o[7], {"op": "maxr", "count": 11, "pos": 4800, "offset": -200})

    def test_volume_clamped(self):
        s = parse_stage("soundtrack(x,5,0)\n")
        self.assertEqual(s["soundtrack"]["volume"], 50)


def _gif() -> bytes:
    b = io.BytesIO()
    Image.new("P", (20, 12), 1).save(b, "GIF")
    return b.getvalue()


def _tree(root: Path) -> Path:
    data = root / "OBJ" / "data"
    (root / "OBJ" / "stages").mkdir(parents=True)
    (root / "OBJ" / "music").mkdir()
    (root / "OBJ" / "mycars").mkdir()
    data.mkdir()
    with zipfile.ZipFile(data / "models.zip", "w") as z:
        z.writestr("mustang.rad", CAR)
        z.writestr("road.rad", CAR)
        z.writestr("sroad.rad", CAR)
        z.writestr("weird.rad", CAR)
    with zipfile.ZipFile(data / "images.zip", "w") as z:
        z.writestr("digit.gif", _gif())
    with zipfile.ZipFile(data / "sounds.zip", "w") as z:
        z.writestr("../evil.wav", b"x")                 # zip-slip attempt: must be ignored
    with zipfile.ZipFile(root / "OBJ" / "music" / "stage1.zip", "w") as z:
        z.writestr("stage1.mod", b"\0" * 1080 + b"M.K.")
    (root / "OBJ" / "stages" / "1.txt").write_text(STAGE)
    (root / "OBJ" / "stages" / "notes.txt").write_text("ignore me")
    (root / "OBJ" / "mycars" / "Simple Car.rad").write_text(CAR)
    (data / "loose.gif").write_bytes(_gif())
    (root / "Foo.java").write_text("class Foo {}")
    return root


class TestJavaAdapter(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        tmp = Path(self._tmp.name)
        self.src = _tree(tmp / "src")
        self.adapter = JavaAdapter(extract_root=tmp / "extract")
        self.man = self.adapter.crawl("nfm", self.src)
        self.by_id = {a.id: a for a in self.man.assets}

    def test_ids(self):
        self.assertEqual(sorted(self.by_id), sorted([
            "mesh/car/mustang", "mesh/piece/road", "mesh/piece/sroad", "mesh/other/weird",
            "image/digit", "image/loose", "data/stage/1", "data/music/stage1"]))

    def test_mesh_meta(self):
        self.assertEqual(self.by_id["mesh/car/mustang"].meta["car_index"], 10)
        self.assertEqual(self.by_id["mesh/piece/road"].meta["stage_id"], 10)
        self.assertEqual(self.by_id["mesh/piece/sroad"].meta["stage_id"], 47)

    def test_skips_user_content_and_code(self):
        self.assertFalse(any("simple" in i for i in self.by_id))
        self.assertFalse(any("foo" in i for i in self.by_id))

    def test_music_is_data_passthrough(self):
        a = self.by_id["data/music/stage1"]
        self.assertIs(a.kind, Kind.DATA)
        self.assertEqual(a.role, "music")

    def test_zip_slip_ignored(self):
        self.assertFalse((self.adapter.extract_root / "evil.wav").exists())
        self.assertFalse(any(a.kind is Kind.AUDIO for a in self.man.assets))

    def test_crawl_from_obj_dir_directly(self):
        man = self.adapter.crawl("nfm", self.src / "OBJ")
        self.assertEqual(len(man.assets), len(self.man.assets))

    def test_convert_all(self):
        out = Path(self._tmp.name) / "out"
        recs = {a.id: convert_asset(a, out, {"textures": {"format": "rgba5551", "pot": True}})
                for a in self.man.assets}
        r = recs["mesh/car/mustang"]
        self.assertTrue(r["parsed"])
        self.assertEqual((r["polys"], r["wheels"], r["tracks"]), (4, 4, 2))
        blob = (out / r["file"]).read_bytes()
        self.assertEqual(blob[:4], b"PMSH")
        st = recs["data/stage/1"]
        self.assertTrue(st["parsed"])
        self.assertEqual(st["objects"], 8)
        self.assertEqual(recs["data/music/stage1"]["passthrough"], True)


if __name__ == "__main__":
    unittest.main()
