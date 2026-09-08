"""spec/<game>.toml  ->  runtime/gen/spec_<game>.h

Emits every constant as a #define, namespaced by its TOML path (SPEC_PLAYER_PHYSICS_GRAVITY).
Ints stay ints; floats get an 'f' suffix; 0x.. hex is preserved.

    .venv/bin/python spec/gen_spec.py [game]        # default: luftrauser
"""

from __future__ import annotations

import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def emit(prefix: str, obj: dict, out: list[str]) -> None:
    for k, v in obj.items():
        name = f"{prefix}_{k}".upper()
        if isinstance(v, dict):
            emit(name, v, out)
        elif isinstance(v, bool):
            out.append(f"#define {name} {1 if v else 0}")
        elif isinstance(v, int):
            out.append(f"#define {name} {v}")
        elif isinstance(v, float):
            out.append(f"#define {name} {v!r}f")
        else:
            raise SystemExit(f"{name}: unsupported value {v!r}")


def main() -> int:
    game = sys.argv[1] if len(sys.argv) > 1 else "luftrauser"
    src = ROOT / "spec" / f"{game}.toml"
    raw = src.read_text()
    data = tomllib.loads(raw)

    lines = [
        f"/* generated from spec/{game}.toml by spec/gen_spec.py — do not edit */",
        f"#ifndef SPEC_{game.upper()}_H",
        f"#define SPEC_{game.upper()}_H",
        "",
    ]
    body: list[str] = []
    for section, obj in data.items():
        body.append(f"/* [{section}] */")
        emit(f"SPEC_{section}", obj, body)
        body.append("")
    lines += body
    lines.append(f"#endif /* SPEC_{game.upper()}_H */")

    dst = ROOT / "runtime" / "gen" / f"spec_{game}.h"
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text("\n".join(lines) + "\n")
    n = sum(1 for ln in body if ln.startswith("#define"))
    print(f"{src.name}: {n} constants -> {dst.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
