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
#    NFM:        clone a released/decompiled NFM source tree

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
    java.py           NFM source tree   -> IR   (partial: media yes, meshes stubbed)
  convert/
    textures.py       image -> .ptx  (POT, PSP swizzle, RGBA8888/5551/4444/IDX8)
    audio.py          audio -> .pcm (sfx) / .ogg (music) via ffmpeg
    models.py         mesh  -> passthrough  (STUB)
  pack.py             build assets.pak + manifest
games/<game>/game.toml  per-game adapter + conversion settings
```

## Not done yet

Runtime (C/C++), `.vag` audio, NFM mesh parsing, verification harness — see PROJECT.md.
