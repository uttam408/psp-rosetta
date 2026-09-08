"""Undo MochiCrypt 3.1c wrapping (as implemented in mochicrypt.Preloader.finish()).

The payload is self-keyed: an RC4-style keystream seeded from the payload's own
last 32 bytes obfuscates the first 131072 bytes, then the whole thing is zlib
data. No external key / no dead patchURL needed.

    python tools/mochicrypt_decrypt.py <Payload.bin> <out.swf>
"""

from __future__ import annotations

import sys
import zlib


def decrypt(data: bytearray) -> bytes:
    n = len(data) - 32
    key = data[n:]                      # last 32 bytes

    s = list(range(256))
    j = 0
    for i in range(256):
        j = (j + s[i] + key[i & 0x1F]) & 0xFF
        s[i], s[j] = s[j], s[i]

    limit = min(n, 131072)
    i = j = 0
    for k in range(limit):
        i = (i + 1) & 0xFF
        u = s[i]
        j = (j + u) & 0xFF
        v = s[j]
        s[i], s[j] = v, u
        data[k] ^= s[(u + v) & 0xFF]

    # AS3 ByteArray.uncompress() -> zlib. Trailing bytes after the stream are junk.
    d = zlib.decompressobj()
    out = d.decompress(bytes(data))
    out += d.flush()
    return out


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    raw = bytearray(open(sys.argv[1], "rb").read())
    swf = decrypt(raw)
    open(sys.argv[2], "wb").write(swf)
    sig = swf[:3]
    print(f"wrote {sys.argv[2]}  {len(swf)} bytes  signature={sig!r} "
          f"({'OK, uncompressed SWF' if sig == b'FWS' else 'compressed SWF' if sig == b'CWS' else 'UNEXPECTED'})")
    return 0 if swf[:3] in (b"FWS", b"CWS", b"ZWS") else 1


if __name__ == "__main__":
    raise SystemExit(main())
