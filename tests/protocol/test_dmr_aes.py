#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""DMR AES-256 against a real recording, not against the firmware's own vectors.

dmrgo's tests carry the AES-256 capture of the public archive.org item dmr-encrypted-samples_202511 (Public Domain Mark
1.0): the PI header, then every AMBE+2 frame of a call as received and as dsd-fme decrypted it with the key the item
publishes. The firmware's keystream (IV expansion from the 32-bit MI, OFB, 7 octets a frame, the MI stepped each
superframe) must give back the same frames: 396 frames, 22 superframes. tests/data/dmr_aes256_capture.txt is the first
10 superframes of that file, which CI runs (tools/test_aes.py) through the call stack; this checks that it is a faithful
copy, and runs the whole capture through the keystream. Needs the pinned dmrgo (python3 tools/sources.py fetch)."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CAPTURE = ROOT / "research/protocol/dmrgo/v2/privacy/testdata/aes256.txt"
COPY = ROOT / "tests/data/dmr_aes256_capture.txt"
KEY = "3AF8C96E21BEFDE927EE1D03DF53CFA701516B0642AC7B38643549E2BE79840E"


def main():
    if not CAPTURE.exists():
        raise SystemExit("Fetch the pinned sources first: python3 tools/sources.py fetch (research/protocol/dmrgo)")
    full = [l for l in CAPTURE.read_text().splitlines() if l.strip()]
    copy = [l for l in COPY.read_text().splitlines() if l.strip() and not l.startswith("#")]
    pi = [l for l in full if l.startswith("pi ")][0]
    assert copy[0] == pi, "the copy's PI header is not the capture's"
    head = [l for l in full if not l.startswith(("pi ", "lc "))][: len(copy) - 2]
    assert copy[2:] == head and len(head) == 180, "tests/data/dmr_aes256_capture.txt is not the capture's first 10 superframes"
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "dmr_aes_capture"
        subprocess.run(["cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        "-I" + str(ROOT / "src/crypto"), str(ROOT / "src/crypto/aes.c"), str(ROOT / "src/crypto/voice_aes.c"),
                        str(ROOT / "tests/protocol/dmr_aes_capture.c"), "-o", str(exe)], check=True)
        out = subprocess.run([str(exe), str(CAPTURE), KEY], check=True, capture_output=True, text=True).stdout.strip()
    print("PASS dmr_aes256_capture:", out)


if __name__ == "__main__":
    sys.exit(main())
