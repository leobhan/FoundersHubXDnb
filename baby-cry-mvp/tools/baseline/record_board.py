"""Record what the board's cry detector hears, as an 8 kHz WAV.

    python -I tools/baseline/record_board.py out.wav                 (from baby-cry-mvp/)
    python -I tools/baseline/record_board.py out.wav --seconds 8
    python -I tools/baseline/record_board.py out.wav --play clip.wav (Windows: plays it first)

The board records after its decimator (action mic_record), so the WAV is exactly
the signal features.py and the detector see. Use it to tune the detector on real
room sound, or to keep a real cry for later.
"""
import argparse
import base64
import sys
import time
import wave

import numpy as np
from atech import Board, NoAcknowledgement


def record(board, seconds, play=None):
    board.call("mic_record", str(seconds), timeout=5)
    if play:
        import winsound
        winsound.PlaySound(str(play), winsound.SND_FILENAME | winsound.SND_ASYNC)
    deadline = time.time() + seconds + 10
    for event in board.events(keys=["mic_record_end"]):
        break
    if time.time() > deadline:
        sys.exit("the recording did not finish")
    total = seconds * 8000
    pcm = np.zeros(total, dtype="<i2")
    at, retries = 0, 0
    while at < total:
        try:   # USB sometimes drops bytes from a line, the answer's or the chunk's: ask again
            value = board.query("mic_record_get", str(at), reply="mic_record", timeout=2)
            raw = base64.b64decode(value["pcm"])
        except (NoAcknowledgement, ValueError):
            value, raw = {"at": -1, "n": 0}, b"-"
        if value["at"] != at or len(raw) != 2 * value["n"]:
            retries += 1
            if retries > 200:
                sys.exit("too many garbled chunks")
            continue
        pcm[at: at + value["n"]] = np.frombuffer(raw, dtype="<i2")
        at += value["n"]
    board.call("mic_record_free")
    return pcm, retries


def write_wav(path, pcm):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(8000)
        w.writeframes(pcm.astype("<i2").tobytes())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--seconds", type=int, default=6)
    ap.add_argument("--play", help="a WAV to play from the laptop as the recording starts (Windows)")
    args = ap.parse_args()
    with Board.connect() as board:
        pcm, retries = record(board, args.seconds, args.play)
    write_wav(args.out, pcm)
    print(f"wrote {args.out}: {len(pcm) / 8000:.1f} s, peak {int(np.abs(pcm).max())}, {retries} chunks fetched again")


if __name__ == "__main__":
    main()
