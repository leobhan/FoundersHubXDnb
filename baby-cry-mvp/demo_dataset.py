"""PROTOTYPE demo: drive the baby-cry display from the Donate-a-Cry dataset.

This is NOT live cry detection. For each clip the script looks up the clip's
dataset label (the folder it sits in) and sends the board a scripted set of
percentages with that category on top. The board then shows it exactly like a
real classification would: icon, name, percentage and "Was I right?".

Dataset: the cleaned and updated Donate-a-Cry corpus
    https://github.com/gveres/donateacry-corpus   (ODbL licence)
fetched into data/ (git-ignored). The audio itself is never read. Fetch it once:
    git clone --depth 1 --filter=blob:none --sparse https://github.com/gveres/donateacry-corpus.git data/donateacry-corpus
    git -C data/donateacry-corpus sparse-checkout set donateacry_corpus_cleaned_and_updated_data

Dataset folder -> what the board shows
    hungry     -> Hungry
    tired      -> Sleepy
    belly_pain -> Diaper      (prototype shortcut: the dataset has no diaper class)
    discomfort -> Discomfort
    burping    -> not used

Needs the atech SDK:  pip install --pre -r requirements-demo.txt

Usage (run from this folder):
    python demo_dataset.py --dry-run       # print what would be sent, no board
    python demo_dataset.py                  # wait for the parent's green/red press
    python demo_dataset.py --auto 8         # advance every 8 seconds instead
"""

import argparse
import hashlib
import random
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_DATA = HERE / "data" / "donateacry-corpus" / "donateacry_corpus_cleaned_and_updated_data"

# Order must match the firmware: Hungry, Sleepy, Diaper, Discomfort.
BOARD_KEYS = ("hungry", "sleepy", "diaper", "discomfort")

# (dataset folder, board key). belly_pain is shown as Diaper on purpose.
CLASS_MAP = (
    ("hungry", "hungry"),
    ("tired", "sleepy"),
    ("belly_pain", "diaper"),
    ("discomfort", "discomfort"),
)

TOP_SCORE_MIN = 60                # scripted top percentage is 60..84
TOP_SCORE_SPAN = 25
OTHER_WEIGHTS = (5, 3, 2)         # how the remainder is split across the other three
LAST_GUESS_RANK = 1               # firmware MAX_GUESSES = 2, so rank 1 is the last guess
RESULT_PAUSE_S = 2.5              # let the board's "Thanks!" / "Sorry!" screen finish
FEEDBACK_EVENT_KEY = "guess_feedback"

BANNER = "PROTOTYPE: scores are scripted from dataset labels, not computed from audio."


def scripted_scores(true_key: str, clip_name: str) -> dict:
    """Four percentages (sum 100) with `true_key` on top. Deterministic per clip."""
    digest = int(hashlib.sha256(clip_name.encode("utf-8")).hexdigest(), 16)
    top = TOP_SCORE_MIN + digest % TOP_SCORE_SPAN
    remainder = 100 - top
    others = [key for key in BOARD_KEYS if key != true_key]
    shift = (digest >> 8) % len(others)
    others = others[shift:] + others[:shift]
    weight_total = sum(OTHER_WEIGHTS)
    shares = [remainder * weight // weight_total for weight in OTHER_WEIGHTS]
    shares[0] += remainder - sum(shares)
    scores = {true_key: top}
    scores.update(dict(zip(others, shares)))
    return {key: scores[key] for key in BOARD_KEYS}


def pick_clips(data_dir: Path, rounds: int, seed: int) -> list:
    """One clip per class per round, as (folder, board_key, filename)."""
    rng = random.Random(seed)
    pools = {}
    for folder, board_key in CLASS_MAP:
        clips = sorted(p.name for p in (data_dir / folder).glob("*.wav"))
        if not clips:
            raise SystemExit(
                f"No .wav clips found in {data_dir / folder}.\n"
                "Fetch the dataset first - the two git commands are at the top of demo_dataset.py."
            )
        pools[folder] = (board_key, rng.sample(clips, k=min(rounds, len(clips))))
    playlist = []
    for round_index in range(rounds):
        for folder, _ in CLASS_MAP:
            board_key, clips = pools[folder]
            if round_index < len(clips):
                playlist.append((folder, board_key, clips[round_index]))
    return playlist


def wait_for_answer(board, sent_at: float) -> str:
    """Block until the parent finishes answering this clip. Returns a short summary."""
    summary = "no answer"
    for event in board.events(keys=[FEEDBACK_EVENT_KEY]):
        if event.received_at is not None and event.received_at < sent_at:
            continue  # a press from before this clip was sent
        value = event.value if isinstance(event.value, dict) else {}
        verdict = "right" if value.get("correct") else "wrong"
        summary = f"parent said {verdict} for {value.get('category', '?')} ({value.get('percent', '?')}%)"
        if value.get("correct") or value.get("rank", 0) >= LAST_GUESS_RANK:
            return summary
        print(f"      {summary} -> board shows the next guess")
    return summary


def connect(port):
    from atech import Board, NoBoardFoundError
    try:
        return Board.connect(port=port)
    except NoBoardFoundError as exc:
        raise SystemExit(f"{exc}\nTry --dry-run to test without a board.") from exc


def send_scores(board, scores: dict) -> None:
    from atech import ActionRejected, NoAcknowledgement
    try:
        board.call("set_scores", scores)
    except (ActionRejected, NoAcknowledgement) as exc:
        raise SystemExit(
            f"{exc}\nThe board may be running old firmware. Rebuild and flash: atech build . && atech upload ."
        ) from exc


def run(args) -> None:
    playlist = pick_clips(args.data, args.rounds, args.seed)
    print(BANNER)
    board = None if args.dry_run else connect(args.port)
    try:
        for index, (folder, board_key, clip) in enumerate(playlist, start=1):
            scores = scripted_scores(board_key, clip)
            print(f"[{index}/{len(playlist)}] shows {board_key.upper():<10} <- {folder}/{clip[:36]}...")
            print(f"      scores {scores}")
            if board is None:
                continue
            sent_at = time.time()
            send_scores(board, scores)
            if args.auto:
                time.sleep(args.auto)
            else:
                print("      waiting for the green/red button on the board (Ctrl-C to stop)...")
                print(f"      {wait_for_answer(board, sent_at)}")
                time.sleep(RESULT_PAUSE_S)
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        if board is not None:
            board.close()
    print("Done.")


def parse_args(argv) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Prototype: show dataset cries on the baby-cry display.")
    parser.add_argument("--data", type=Path, default=DEFAULT_DATA, help="cleaned dataset folder")
    parser.add_argument("--rounds", type=int, default=1, help="clips per class (default 1)")
    parser.add_argument("--seed", type=int, default=7, help="which clips to pick (default 7)")
    parser.add_argument("--auto", type=float, default=0.0, metavar="SECONDS",
                        help="advance on a timer instead of waiting for a button press")
    parser.add_argument("--port", default=None, help="serial port (default: auto-detect)")
    parser.add_argument("--dry-run", action="store_true", help="print only, do not touch the board")
    args = parser.parse_args(argv)
    if args.rounds < 1:
        parser.error("--rounds must be at least 1")
    if args.auto < 0:
        parser.error("--auto must not be negative")
    return args


if __name__ == "__main__":
    run(parse_args(sys.argv[1:]))
