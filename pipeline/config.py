from __future__ import annotations

import tomllib
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parent.parent
GAMES_DIR = REPO_ROOT / "games"


def load_game(game: str) -> dict[str, Any]:
    path = GAMES_DIR / game / "game.toml"
    if not path.exists():
        raise SystemExit(f"no game config: {path}")
    cfg = tomllib.loads(path.read_text())
    cfg.setdefault("game", {}).setdefault("id", game)
    return cfg


def build_dir(game: str) -> Path:
    return REPO_ROOT / "build" / game
