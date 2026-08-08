"""
Build a labeled speech / music / distress (scream, shout, crying-sobbing) /
other train and test set from AudioSet.

Good news: someone has already re-hosted AudioSet's *balanced* train + eval
sets on HuggingFace with the actual audio bytes included (agkphysics/AudioSet),
so this skips the whole "download from YouTube, deal with dead links" problem
entirely. The dataset's own train/eval split becomes your train/test split,
with no leakage between them.

Install first:
    pip install datasets soundfile librosa

Then run:
    python build_audioset_subset.py

Output:
    audioset_subset/
        manifest.csv                  <- filepath, label, split, video_id
        train/speech/*.wav
        train/music/*.wav
        train/distress/*.wav
        train/other/*.wav
        test/... (same structure)
"""

import csv
import io
import os
from collections import Counter

import librosa
import numpy as np
import soundfile as sf
from datasets import Audio, load_dataset

# ----------------------------------------------------------------------
# Config - edit this section for your own class scheme
# ----------------------------------------------------------------------

# Maps an AudioSet human-readable label -> your collapsed target class.
# Add/remove entries here if you want to change the scheme.
TARGET_LABELS = {
    "Speech": "speech",
    "Music": "music",
    "Screaming": "distress",
    "Shout": "distress",
    "Crying, sobbing": "distress",
}

# If a clip has labels matching more than one target class (AudioSet clips
# are multi-label - e.g. a clip can be both "Speech" and "Screaming" at
# once), this decides which one wins. Earlier = higher priority.
CLASS_PRIORITY = ["distress", "speech", "music"]

INCLUDE_OTHER_CLASS = True   # also sample a generic negative/"other" class
MAX_PER_CLASS = 500          # cap per target class, per split
SAMPLE_RATE = 16000          # resample everything to this on the fly
OUTPUT_DIR = "audioset_subset"

# The "balanced" config's train split has ~18.7k clips total across 527
# classes, and the class was originally curated to guarantee only ~59
# examples for rarer classes like "Screaming" - so don't be surprised if
# distress ends up far below MAX_PER_CLASS while speech/music hit the cap
# easily. If you need more distress examples, switch this to "unbalanced"
# (config_name="unbalanced", splits use "unbal_train"/"test") - it has ~2M
# clips, but streaming through it to find rare classes takes much longer.
AUDIOSET_CONFIG = "balanced"


def process_split(split_name: str, manifest_writer: csv.writer) -> None:
    print(f"\n--- Processing {split_name} split ---")
    ds = load_dataset(
        "agkphysics/AudioSet", AUDIOSET_CONFIG, split=split_name, streaming=True
    )
    ds = ds.cast_column("audio", Audio(sampling_rate=SAMPLE_RATE, decode=False))

    counts: Counter = Counter()
    n_target_classes = len(set(TARGET_LABELS.values())) + int(INCLUDE_OTHER_CLASS)
    target_total = MAX_PER_CLASS * n_target_classes

    for example in ds:
        if sum(counts.values()) >= target_total:
            break

        human_labels = example["human_labels"]
        matched = {TARGET_LABELS[lbl] for lbl in human_labels if lbl in TARGET_LABELS}

        if matched:
            target_class = next(c for c in CLASS_PRIORITY if c in matched)
        elif INCLUDE_OTHER_CLASS:
            target_class = "other"
        else:
            continue

        if counts[target_class] >= MAX_PER_CLASS:
            continue

        class_dir = os.path.join(OUTPUT_DIR, split_name, target_class)
        os.makedirs(class_dir, exist_ok=True)

        video_id = example["video_id"]
        out_path = os.path.join(class_dir, f"{video_id}.wav")

        try:
            audio_bytes = example["audio"]["bytes"]
            if audio_bytes is None:
                raise ValueError("Audio bytes are missing")

            audio_array, _ = librosa.load(
                io.BytesIO(audio_bytes), sr=SAMPLE_RATE, mono=True
            )
            sf.write(out_path, audio_array, SAMPLE_RATE)
        except Exception as e:
            print(f"  skipping {video_id}: {e}")
            continue

        manifest_writer.writerow([out_path, target_class, split_name, video_id])
        counts[target_class] += 1

        if sum(counts.values()) % 50 == 0:
            print(f"  progress: {dict(counts)}")

    print(f"  done with {split_name}: {dict(counts)}")


def main() -> None:
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    manifest_path = os.path.join(OUTPUT_DIR, "manifest.csv")

    with open(manifest_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["filepath", "label", "split", "video_id"])

        process_split("train", writer)
        process_split("test", writer)

    print(f"\nManifest written to {manifest_path}")


if __name__ == "__main__":
    main()