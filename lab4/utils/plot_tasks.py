import sys
import os
import pandas as pd
import matplotlib.pyplot as plt

# Usage: python3 plot_tasks.py [tasks_csv_path] [output_png_path]
input_csv = sys.argv[1] if len(sys.argv) > 1 else "tasks.csv"
output_png = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(os.path.basename(input_csv))[0] + "_gantt.png"

# Read profiler output
df = pd.read_csv(input_csv)

if len(df) == 0:
    print("No completed tasks found.")
    exit()

# Convert time to milliseconds relative to first task
t0 = df["start_time"].min()

df["start_ms"] = (
    df["start_time"] - t0
) / 1000.0

df["duration_ms"] = (
    df["duration"] / 1000.0
)

# Number of threads
threads = sorted(
    df["executor_thread"].unique()
)

fig, ax = plt.subplots(
    figsize=(14, 6)
)

# Draw each task
for _, task in df.iterrows():

    thread = int(
        task["executor_thread"]
    )

    task_id = int(
        task["task_id"]
    )

    ax.barh(
        thread,
        task["duration_ms"],
        left=task["start_ms"],
        height=0.6,
        color=plt.cm.tab20(
            task_id % 20
        ),
        edgecolor="black"
    )

    # Task number inside bar
    ax.text(
        task["start_ms"]
        + task["duration_ms"] / 2,
        thread,
        str(task_id),
        ha="center",
        va="center",
        fontsize=8
    )


ax.set_xlabel(
    "Time (ms)"
)

ax.set_ylabel(
    "OpenMP Thread"
)

ax.set_title(
    f"OpenMP Task Mapping ({input_csv})"
)

ax.set_yticks(threads)

ax.grid(
    axis="x",
    linestyle="--",
    alpha=0.4
)

plt.tight_layout()

plt.savefig(
    output_png,
    dpi=200
)
print(f"Saved plot to {output_png}")

plt.show()

