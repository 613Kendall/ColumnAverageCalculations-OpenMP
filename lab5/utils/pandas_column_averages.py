#!/usr/bin/env python3
"""Compute column averages of fitness_processed.csv (all columns except Class) using
pandas, as a reference/validation for the OpenMP C implementation.

Usage: python3 pandas_column_averages.py [csv_path]
"""
import sys

import pandas as pd

DEFAULT_CSV_PATH = "/Users/ksmith25/School/Fall_2026/CMSC483/ColumnAverageCalculations-OpenMP/lab5/data/fitness_0.5x.csv"


def main() -> None:
    csv_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_CSV_PATH

    totals = None
    counts = None
    row_count = 0
    for chunk in pd.read_csv(csv_path, chunksize=10000):
        numeric_chunk = chunk.drop(columns=["Class"], errors="ignore").select_dtypes(
            include="number"
        )
        chunk_totals = numeric_chunk.sum()
        chunk_counts = numeric_chunk.count()
        totals = chunk_totals if totals is None else totals.add(chunk_totals, fill_value=0)
        counts = chunk_counts if counts is None else counts.add(chunk_counts, fill_value=0)
        row_count += len(chunk)

    if totals is None or counts is None:
        raise ValueError(f"No rows found in {csv_path}")

    averages = totals / counts

    print(f"Loaded {row_count} data rows from {csv_path}\n")
    print(f"{'Column':<10} {'Count':>12} {'Total':>18} {'Average':>15}")
    for col in totals.index:
        print(f"{col:<10} {counts[col]:>12.0f} {totals[col]:>18.4f} {averages[col]:>15.8f}")


if __name__ == "__main__":
    main()
