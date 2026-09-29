#!/usr/bin/env zsh
# Fixed dataset: fitness_0.5x.csv with 1, 2, 4, and 8 threads.
# Usage: ./strong_scaling.sh [program_or_source]
# Defaults to column_averages_v3. Timings append under <program>/strong_scaling/.
set -e
if (( $# > 1 )); then
    echo "Usage: $0 [program_or_source]" >&2
    exit 1
fi
SCRIPT_DIR="${0:A:h}"
exec zsh "$SCRIPT_DIR/run_profile.sh" --strong "${1:-column_averages_v3}"
