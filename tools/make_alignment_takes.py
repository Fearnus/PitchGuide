"""Builds synthetic takes from reference vocals with a known time map, for
measuring pitchguide-align accuracy (score them with score_alignment.py).

For each reference: (a) wandering tempo, (b) the same an octave lower with
formants preserved, (c) the middle 40% with wandering tempo, +3 semitones.
Each take gets leading silence. Ground truth (take seconds -> reference
seconds) is written next to it as <take>.truth.csv.

Requires numpy and the rubberband command-line tool (apt install rubberband-cli).

  python3 tools/make_alignment_takes.py <out-dir> <reference.wav>...
"""
import os
import subprocess
import sys
import wave

import numpy as np

RATE_OUT = 44100


def read_wav(path):
    with wave.open(path) as w:
        rate, channels, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
        data = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16 if width == 2 else np.int32)
    return rate, channels, data.reshape(-1, channels)


def write_wav(path, rate, samples):
    samples = np.clip(samples, -32768, 32767).astype(np.int16)
    with wave.open(path, "wb") as w:
        w.setnchannels(samples.shape[1])
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(samples.tobytes())


def tempo_keyframes(num_samples, rate, seed, start=0):
    """Piecewise-linear time map whose local tempo wanders between 0.82x and 1.18x."""
    rng = np.random.default_rng(seed)
    source, target = [start], [0]
    while source[-1] < num_samples:
        step = int(rate * rng.uniform(1.5, 3.0))
        speed = rng.uniform(0.82, 1.18)
        source.append(min(num_samples, source[-1] + step))
        target.append(target[-1] + int((source[-1] - source[-2]) / speed))
    return np.array(source), np.array(target)


def make_take(reference, out_dir, name, lead_seconds, semitones, excerpt, seed):
    rate, channels, data = read_wav(reference)
    begin, end = 0, len(data)
    if excerpt:
        begin, end = int(len(data) * 0.3), int(len(data) * 0.7)
    clip_path = os.path.join(out_dir, name + ".src.wav")
    write_wav(clip_path, rate, data[begin:end])

    source, target = tempo_keyframes(end - begin, rate, seed)
    map_path = os.path.join(out_dir, name + ".map")
    with open(map_path, "w") as f:
        for s, t in zip(source[1:-1], target[1:-1]):
            f.write(f"{s} {t}\n")

    stretched = os.path.join(out_dir, name + ".stretched.wav")
    command = ["rubberband", "-3", "-q", "-M", map_path, "-D", str(target[-1] / rate)]
    if semitones:
        command += ["-p", str(semitones), "-F"]
    subprocess.run(command + [clip_path, stretched], check=True)

    rate2, _, take = read_wav(stretched)
    assert rate2 == rate
    lead = np.zeros((int(lead_seconds * rate), take.shape[1]), dtype=np.int32)
    take_path = os.path.join(out_dir, name + ".wav")
    write_wav(take_path, rate, np.concatenate([lead, take.astype(np.int32)]))

    # Truth: take time -> reference time, sampled every 10 ms.
    take_seconds = np.arange(0, (len(lead) + len(take)) / rate, 0.01)
    stretched_samples = take_seconds * rate - len(lead)
    reference_samples = np.interp(stretched_samples, target, source) + begin
    with open(take_path + ".truth.csv", "w") as f:
        f.write("take_seconds,reference_seconds,in_take\n")
        for t, r, s in zip(take_seconds, reference_samples / rate, stretched_samples):
            f.write(f"{t:.4f},{r:.4f},{int(0 <= s <= target[-1])}\n")
    for path in (clip_path, map_path, stretched):
        os.remove(path)
    return take_path


if __name__ == "__main__":
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    for index, reference in enumerate(sys.argv[2:]):
        stem = f"ref{index}"
        print(make_take(reference, out_dir, stem + "_a_tempo", 0.7, 0, False, 10 + index))
        print(make_take(reference, out_dir, stem + "_b_octave_down", 0.7, -12, False, 10 + index))
        print(make_take(reference, out_dir, stem + "_c_verse_plus3", 1.2, 3, True, 20 + index))
