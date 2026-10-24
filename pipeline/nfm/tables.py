"""Name/id tables lifted from GameSparker.loadbase().

The original loads every ``models.zip`` entry into one ``ContO[]`` slot:

    cars          slot 0..15          (CARS order)
    track pieces  slot 56 + index     (PIECES order)

A stage file refers to pieces by a small integer ``id`` and the loader does
``slot = id + 46``, so ``id = index_in_PIECES + 10``. (Ids 0..9 are unused by
shipped stages; slots 16..55 are reserved for user/multiplayer cars.)
"""

from __future__ import annotations

CARS = [
    "2000tornados", "formula7", "canyenaro", "lescrab", "nimi", "maxrevenge",
    "leadoxide", "koolkat", "drifter", "policecops", "mustang", "king",
    "audir8", "masheen", "radicalone", "drmonster",
]

PIECES = [
    "road", "froad", "twister2", "twister1", "turn", "offroad", "bumproad",
    "offturn", "nroad", "nturn", "roblend", "noblend", "rnblend", "roadend",
    "offroadend", "hpground", "ramp30", "cramp35", "dramp15", "dhilo15",
    "slide10", "takeoff", "sramp22", "offbump", "offramp", "sofframp",
    "halfpipe", "spikes", "rail", "thewall", "checkpoint", "fixpoint",
    "offcheckpoint", "sideoff", "bsideoff", "uprise", "riseroad", "sroad",
    "soffroad", "tside", "launchpad", "thenet", "speedramp", "offhill",
    "slider", "uphill", "roll1", "roll2", "roll3", "roll4", "roll5", "roll6",
    "opile1", "opile2", "aircheckpoint", "tree1", "tree2", "tree3", "tree4",
    "tree5", "tree6", "tree7", "tree8", "cac1", "cac2", "cac3", "8sroad",
    "8soffroad",
]

PIECE_ID_BASE = 10          # stage id of PIECES[0]


def piece_name(stage_id: int) -> str | None:
    """Model name for a stage-file object id, or None if the id is out of range."""
    i = stage_id - PIECE_ID_BASE
    return PIECES[i] if 0 <= i < len(PIECES) else None


def piece_id(name: str) -> int | None:
    try:
        return PIECES.index(name) + PIECE_ID_BASE
    except ValueError:
        return None
