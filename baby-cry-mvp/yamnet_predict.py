import base64
import collections
import csv
import sys

import numpy as np

from atech import Board, NoBoardFoundError

SAMPLE_RATE = 16000
WINDOW_SAMPLES = 15600

try:
    import tensorflow as tf
    import tensorflow_hub as hub
except Exception as exc:
    raise SystemExit(
        "YAMNet dependencies missing. Use Python 3.11, then run:\n"
        "py -3.11 -m pip install -r requirements-yamnet.txt\n"
        "py -3.11 yamnet_predict.py"
    ) from exc

print("Loading YAMNet...")
yamnet = hub.load("https://tfhub.dev/google/yamnet/1")
class_names = tf.keras.utils.get_file(
    "yamnet_class_map.csv",
    "https://raw.githubusercontent.com/tensorflow/models/master/research/audioset/yamnet/yamnet_class_map.csv",
)
with open(class_names, "r", encoding="utf-8") as class_file:
    rows = list(csv.DictReader(class_file))

pcm_window = collections.deque(maxlen=WINDOW_SAMPLES)


print("Opening board and listening for mic_pcm...")
try:
    board = Board.connect()
except NoBoardFoundError as exc:
    raise SystemExit("No Atech board found. Reconnect it on USB-C, then run atech ports.") from exc

with board:
    for event in board.events(keys=["mic_pcm"]):
        try:
            raw = base64.b64decode(event.value or "")
            samples = np.frombuffer(raw, dtype="<i2").astype(np.float32)
        except Exception:
            continue

        if samples.size == 0:
            continue

        pcm_window.extend(samples / 32768.0)
        if len(pcm_window) < WINDOW_SAMPLES:
            continue

        waveform = np.asarray(pcm_window, dtype=np.float32)
        scores, embeddings, spectrogram = yamnet(waveform)
        mean_scores = tf.reduce_mean(scores, axis=0).numpy()
        top_indices = mean_scores.argsort()[-5:][::-1]

        baby_score = 0.0
        matched_labels = []
        for index in top_indices:
            label = rows[index]["display_name"]
            score = float(mean_scores[index])
            normalized = label.lower()
            if any(word in normalized for word in (
                "baby cry", "baby crying", "infant cry", "crying baby", "whimper"
            )):
                baby_score = max(baby_score, score)
                matched_labels.append((label, score))

        if matched_labels:
            print(f"BABY CRY probability: {baby_score:.1%} | {matched_labels}")
        else:
            top = [(rows[i]["display_name"], float(mean_scores[i])) for i in top_indices]
            print(f"NOT CRY likely | top: {top}")
