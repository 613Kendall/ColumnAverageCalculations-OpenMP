#!/usr/bin/env zsh
# Dataset/thread pairs: 0.5x/1, 1x/2, 2x/4, and 4x/8.
# Usage: ./weak_scaling.sh [program_or_source]
# Defaults to column_averages_v3. Timings append under <program>/weak_scaling/.
set -e
if (( $# > 1 )); then
    echo "Usage: $0 [program_or_source]" >&2
    exit 1
fi
SCRIPT_DIR="${0:A:h}"
exec zsh "$SCRIPT_DIR/run_profile.sh" --weak "${1:-column_averages_v3}"
