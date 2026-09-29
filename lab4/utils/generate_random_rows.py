#!/usr/bin/env python3
"""Append random synthetic rows to creditcard.csv, matching the existing schema
(Time, V1..V28, Amount, Class), without loading the whole file into memory.

Usage: python3 generate_random_rows.py [num_rows] [csv_path]
"""
import csv
import subprocess
import sys

import numpy as np

NUM_V_COLS = 28
DEFAULT_NUM_ROWS = 400000
DEFAULT_CSV_PATH = "creditcard.csv"
FRAUD_RATE = 0.0017  # roughly matches the original dataset's class imbalance


def last_time_value(csv_path: str) -> float:
    last_line = subprocess.run(
        ["tail", "-1", csv_path], capture_output=True, text=True, check=True
    ).stdout
    return float(last_line.split(",")[0])


def main() -> None:
    num_rows = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_NUM_ROWS
    csv_path = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_CSV_PATH

    rng = np.random.default_rng()
    start_time = last_time_value(csv_path)

    # V1..V28 are PCA components in the real dataset, so a standard normal is a
    # reasonable stand-in; Amount is non-negative and right-skewed.
    times = start_time + np.sort(rng.integers(1, 3, size=num_rows)).cumsum()
    v_columns = rng.normal(loc=0.0, scale=1.0, size=(num_rows, NUM_V_COLS))
    amounts = rng.exponential(scale=88.0, size=num_rows)
    classes = rng.choice([0, 1], size=num_rows, p=[1 - FRAUD_RATE, FRAUD_RATE])

    with open(csv_path, "a", newline="") as f:
        writer = csv.writer(f)
        for i in range(num_rows):
            row = [int(times[i])] + [f"{v:.15g}" for v in v_columns[i]]
            row.append(f"{amounts[i]:.2f}")
            row.append(f'"{classes[i]}"')
            writer.writerow(row)

    print(f"Appended {num_rows} random rows to {csv_path}")


if __name__ == "__main__":
    main()
