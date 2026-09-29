#!/usr/bin/env zsh
# Rebuilds the OMPT profiler + a given C source file, then runs the resulting
# binary under the profiler for each thread count in THREAD_COUNTS, saving
# each run's task log as <program>_<N>.csv.
#
# Usage: ./run_profile.sh [program_or_source] [thread_count ...]
# Defaults: column_averages_v2, fitness_0.5x.csv, and threads 1 2 4 8.
# Set CSV_PATH to profile a different dataset.
# strong_scaling.sh and weak_scaling.sh use this shared build/run helper.
#   e.g. ./run_profile.sh tasking_bad_v2 1 2 4 8
#        ./run_profile.sh column_averages/column_averages.c 1 2 4 8
#
# If a program name is provided, this script expects:
#   <lab5>/<program_name>/<program_name>.c

set -e

SCRIPT_DIR="${0:A:h}"
LAB5_DIR="${SCRIPT_DIR:h}"

SCALING_MODE=""
if [[ "${1:-}" == --strong || "${1:-}" == --weak ]]; then
    SCALING_MODE="${1#--}_scaling"
    shift
    if (( $# > 1 )); then
        echo "Usage: $0 --strong|--weak [program_or_source]" >&2
        exit 1
    fi
fi

INPUT="${1:-column_averages_v2}"
if (( $# > 0 )); then
    shift
fi

if [[ -f "$INPUT" ]]; then
    SRC_FILE="${INPUT:A}"
elif [[ -f "$LAB5_DIR/$INPUT/$INPUT.c" ]]; then
    SRC_FILE="$LAB5_DIR/$INPUT/$INPUT.c"
elif [[ -f "$LAB5_DIR/$INPUT.c" ]]; then
    SRC_FILE="$LAB5_DIR/$INPUT.c"
else
    echo "Error: could not resolve source from '$INPUT'" >&2
    echo "Expected either an existing .c path, or $LAB5_DIR/<name>/<name>.c" >&2
    exit 1
fi

PROGRAM_NAME="${SRC_FILE:t:r}"  # basename without directory or .c extension
PROGRAM_DIR="${SRC_FILE:h}"
BINARY_PATH="$PROGRAM_DIR/$PROGRAM_NAME"
LIBOMP_PREFIX="/opt/homebrew/opt/libomp"
THREAD_COUNTS=(1 2 4 8)
if (( $# > 0 )); then
    THREAD_COUNTS=("$@")
fi
for n in "${THREAD_COUNTS[@]}"; do
    if [[ "$n" != <-> ]] || (( n < 1 )); then
        echo "Error: thread counts must be positive integers: $n" >&2
        exit 1
    fi
done

DATASETS=()
if [[ "$SCALING_MODE" == weak_scaling ]]; then
    for scale in 0.5 1 2 4; do
        DATASETS+=("$LAB5_DIR/data/fitness_${scale}x.csv")
    done
else
    CSV_PATH="${CSV_PATH:-$LAB5_DIR/data/fitness_0.5x.csv}"
    if [[ "$SCALING_MODE" == strong_scaling ]]; then
        CSV_PATH="$LAB5_DIR/data/fitness_0.5x.csv"
    fi
    for n in "${THREAD_COUNTS[@]}"; do
        DATASETS+=("${CSV_PATH:A}")
    done
fi
for dataset in "${DATASETS[@]}"; do
    if [[ ! -f "$dataset" || ! -r "$dataset" ]]; then
        echo "Error: CSV file is missing or unreadable: $dataset" >&2
        exit 1
    fi
done

echo "Building libompt_profiler.so..."
clang -O2 -fPIC -shared \
    -I "$LIBOMP_PREFIX/include" -L "$LIBOMP_PREFIX/lib" -lomp \
    "$LAB5_DIR/ompt_profiler.c" -o "$LAB5_DIR/libompt_profiler.so"

echo "Building $PROGRAM_NAME from $SRC_FILE..."
clang -Xpreprocessor -fopenmp \
    -I "$LIBOMP_PREFIX/include" -L "$LIBOMP_PREFIX/lib" -lomp \
    -O2 -o "$BINARY_PATH" "$SRC_FILE"

export OMP_TOOL=enabled
export OMP_TOOL_LIBRARIES="$LAB5_DIR/libompt_profiler.so"
export DYLD_LIBRARY_PATH="$LIBOMP_PREFIX/lib:$DYLD_LIBRARY_PATH"

OUTPUT_DIR="$PROGRAM_DIR"
if [[ -n "$SCALING_MODE" ]]; then
    OUTPUT_DIR="$PROGRAM_DIR/$SCALING_MODE"
fi
mkdir -p "$OUTPUT_DIR"
pushd "$OUTPUT_DIR" >/dev/null

TIMING_FILE="$OUTPUT_DIR/${PROGRAM_NAME}_timings.txt"
echo "--- Benchmark started: $(date '+%Y-%m-%d %H:%M:%S') ---" >> "$TIMING_FILE"
for (( i = 1; i <= ${#THREAD_COUNTS[@]}; i++ )); do
    n="${THREAD_COUNTS[$i]}"
    CSV_PATH="${DATASETS[$i]}"
    echo "Dataset: $CSV_PATH"
    echo "--- Dataset: $CSV_PATH ---" >> "$TIMING_FILE"
    echo "--- Running $PROGRAM_NAME with OMP_NUM_THREADS=$n ---"
    echo "--- OMP_NUM_THREADS=$n ---" >> "$TIMING_FILE"
    gtime -a -o "$TIMING_FILE" env OMP_NUM_THREADS="$n" "$BINARY_PATH" "$n" "$CSV_PATH"
    cp tasks.csv "${PROGRAM_NAME}_${n}.csv"
    echo "Saved ${PROGRAM_NAME}_${n}.csv"
done

rm tasks.csv
popd >/dev/null

echo "Done. Task logs: $OUTPUT_DIR/${PROGRAM_NAME}_<N>.csv"
echo "Timings appended to: $TIMING_FILE"
