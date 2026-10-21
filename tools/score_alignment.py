"""Scores a pitchguide-align CSV against a take's truth CSV from
make_alignment_takes.py.

Only sung take frames (activity > 0.5) inside the stretched audio count.

  python3 tools/score_alignment.py <alignment.csv> <take.wav.truth.csv>
"""
import sys

import numpy as np


def load(path):
    with open(path) as f:
        header = f.readline().strip().split(",")
        rows = np.array([[float(x) for x in line.split(",")] for line in f if line.strip()])
    return {name: rows[:, i] for i, name in enumerate(header)}


alignment, truth = load(sys.argv[1]), load(sys.argv[2])
truth_reference = np.interp(alignment["take_seconds"], truth["take_seconds"], truth["reference_seconds"])
in_take = np.interp(alignment["take_seconds"], truth["take_seconds"], truth["in_take"]) > 0.5
sung = (alignment["take_activity"] > 0.5) & in_take
error_ms = np.abs(alignment["reference_seconds"] - truth_reference)[sung] * 1000
print(f"sung frames {sung.sum():5d}  median {np.median(error_ms):6.1f} ms  "
      f"p90 {np.percentile(error_ms, 90):6.1f} ms  max {error_ms.max():7.1f} ms  "
      f"<=50ms {np.mean(error_ms <= 50) * 100:5.1f}%  <=100ms {np.mean(error_ms <= 100) * 100:5.1f}%")
