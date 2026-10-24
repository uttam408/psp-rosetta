<p align="center">
  <img src="docs/logo.png" alt="PSP-Rosetta" width="560">
</p>

# psp-rosetta

Asset pipeline for re-targeting Flash / Java browser games to homebrew PSP.
See [PROJECT.md](PROJECT.md) for the full design.

## Setup

```sh
make setup          # venv + Pillow  (needs python3, ffmpeg on PATH)
```

## Use

```sh
# 1. get a source directory
#    Luftrauser: decompile the SWF with JPEXS -> "Export all parts" -> <export dir>
#    NFM:        clone a decompiled NFM source tree (catb0t/need-for-madness-source),
#                point --src at its root; see docs/nfm-port-plan.md

# 2. run the pipeline
.venv/bin/python -m pipeline.cli build --game luftrauser --src <export-dir>

# or step by step
.venv/bin/python -m pipeline.cli crawl   --game luftrauser --src <export-dir>
.venv/bin/python -m pipeline.cli info    --game luftrauser
.venv/bin/python -m pipeline.cli convert --game luftrauser
.venv/bin/python -m pipeline.cli pack    --game luftrauser
```

Outputs land in `build/<game>/`:

| file | what |
|---|---|
| `manifest.json` | crawl result — every asset the adapter found |
| `assets/` | converted `.ptx` / `.pcm` / `.ogg` / `.bin` files |
| `convert.json` | per-asset conversion records |
| `assets.pak` | packed archive for the runtime |
| `assets.manifest.json` | runtime manifest (asset table + screen info) |

## Try it without real game files

```sh
make demo           # builds a synthetic Luftrauser-shaped fixture and runs the pipeline
make test
```

## Layout

```
pipeline/
  ir.py               IR: Asset / Manifest — the adapter<->converter contract
  config.py           game.toml loader
  cli.py              crawl / convert / pack / build / info
  adapters/
    base.py           Adapter ABC + crawl() (dedupe, sort)
    flash.py          JPEXS export dir  -> IR
    java.py           NFM source tree   -> IR   (unpacks models/images/sounds/music zips, stages)
  nfm/
    rad.py            .rad model parser (float32-exact vs ContO.java)
    pmesh.py          .pmesh runtime model format (pack/unpack)
    stage.py          stage .txt parser
    tables.py         car / track-piece names and stage-id mapping
  convert/
    textures.py       image -> .ptx  (POT, PSP swizzle, RGBA8888/5551/4444/IDX8)
    audio.py          audio -> .pcm (sfx) / .ogg (music) via ffmpeg
    models.py         NFM .rad -> .pmesh
    data.py           NFM stage -> .stage.json; other data passthrough
  pack.py             build assets.pak + manifest
games/<game>/game.toml  per-game adapter + conversion settings
```

## Not done yet

NFM runtime (C), `.vag` audio, NFM fidelity harness — see PROJECT.md and
[docs/nfm-port-plan.md](docs/nfm-port-plan.md). (The Luftrauser runtime exists.)
