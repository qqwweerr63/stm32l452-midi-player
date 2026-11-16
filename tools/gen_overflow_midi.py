"""Generate a synthetic Type-0 .mid whose miditones bytestream exceeds 32 KB.

Used to verify the bytestream overflow guard added to midi_convert.c.
The .mid itself stays well under the heap limit (~70 KB after shrinking
g_score), but the note density makes the converted bytestream overflow.
"""
import struct
import sys


def vlq(n):
    if n == 0:
        return b"\x00"
    out = []
    while n:
        out.append(n & 0x7F)
        n >>= 7
    out = out[::-1]
    return bytes([b | 0x80 for b in out[:-1]] + [out[-1]])


def make_midi(path, n_notes):
    trk = bytearray()
    for i in range(n_notes):
        note = 48 + (i % 36)
        trk += vlq(1) + bytes([0x90, note, 100])  # note on
        trk += vlq(1) + bytes([0x80, note, 0])    # note off
    trk += vlq(0) + bytes([0xFF, 0x2F, 0x00])     # end of track

    hdr = b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480)
    data = hdr + b"MTrk" + struct.pack(">I", len(trk)) + bytes(trk)
    with open(path, "wb") as f:
        f.write(data)
    print("wrote %s: %d bytes, %d notes" % (path, len(data), n_notes))


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "overflow_test.mid"
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 8000
    make_midi(out, n)
