# Column Average Calculations with OpenMP

## What's included

- **v2:** loads the CSV into memory and uses one OpenMP task per column.
- **v3:** processes batches of rows in parallel to reduce memory use.
- `lab5/data/`: fitness CSV datasets at 0.5x, 1x, 2x, and 4x sizes. Not included in the repository; download separately.
- `lab5/utils/`: scripts for profiling, scaling tests, and checking results.

## Setup and run

Run these commands from the repository root on macOS with Homebrew:

```sh
xcode-select --install  # Only if command-line tools are missing
brew install libomp gnu-time
zsh lab5/utils/run_profile.sh column_averages_v3 1 2 4 8
```

The script compiles the program and profiler, then runs each thread count.
Replace `column_averages_v3` with `column_averages_v2` to run v2.
Scripts expect libomp under `/opt/homebrew/opt/libomp`.
After building, run a single calculation with:

```sh
./lab5/column_averages_v3/column_averages_v3 4 lab5/data/fitness_0.5x.csv
```

Arguments are the thread count and optional CSV path.
Results show each column's valid count, sum, and average in the terminal.

## Scaling tests

```sh
zsh lab5/utils/strong_scaling.sh column_averages_v3
zsh lab5/utils/weak_scaling.sh column_averages_v3
```

Strong scaling uses 0.5x data with 1, 2, 4, and 8 threads.
Weak scaling pairs those thread counts with 0.5x, 1x, 2x, and 4x data.
Use `column_averages_v2` in either command to test v2.

## What's in the `.txt` files?

Each program's scaling folders contain `*_timings.txt` logs.
Logs append the date, dataset, thread count, and GNU time measurements.
