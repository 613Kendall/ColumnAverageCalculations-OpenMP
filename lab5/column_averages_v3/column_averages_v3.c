// Read CSV rows in batches, then parse and sum each batch in parallel.
// Per-thread sums and counts combine into weighted column averages.
// Usage: ./column_averages_v3 <num_threads> [path_to_csv]
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <locale.h>
#ifdef __APPLE__
#include <xlocale.h>
#endif
#include <omp.h>

#define MAX_LINE_LEN 4096
#define MAX_COL_NAME_LEN 32
#define MAX_FIELDS 256
#define BATCH_ROWS 8192

const char* DEFAULT_CSV_PATH = "lab5/data/fitness_0.5x.csv";

typedef struct {
    char name[MAX_COL_NAME_LEN];
    double count;
    double total;
} ColumnStat;

/*
 * Split an unquoted CSV line into fields without skipping empty columns.
 * For example, "10,,30," contains four fields: "10", "", "30", "".
 *
 * Preconditions: line is a writable, null-terminated string containing one
 * complete record without quoted fields or embedded newlines. fields has
 * room for max_fields pointers, and max_fields is positive.
 * Postconditions: on success, returns the field count and fills fields with
 * pointers into line. Commas and the line ending become null terminators.
 * The caller owns line and must keep it alive while using these pointers.
 * Returns -1 if capacity is exceeded; line may already be partly modified,
 * and the caller must discard the partial result.
 */
static int splitCsvFields(char *line, char *fields[], int max_fields) {
    line[strcspn(line, "\r\n")] = '\0';
    int field_count = 1;
    fields[0] = line;

    for (char *cursor = line; *cursor != '\0'; cursor++) {
        if (*cursor != ',') {
            continue;
        }
        if (field_count == max_fields) {
            return -1;
        }
        *cursor = '\0';
        fields[field_count++] = cursor + 1;
    }
    return field_count;
}

/*
 * Convert a complete field to a finite number, allowing surrounding whitespace.
 * Preconditions: field points to a null-terminated string; numeric_locale is
 * a valid private C locale for the calling thread (decimal separator: '.').
 * Postconditions: leaves field unchanged and returns its numeric value, or
 * NAN for empty, nonnumeric, out-of-range, or nonfinite input. The caller must
 * exclude NAN from both the sum and count when computing an average.
 */
static double parseNumericField(const char *field, locale_t numeric_locale) {
    char *end;
    errno = 0;
    double value = strtod_l(field, &end, numeric_locale);
    if (end == field || errno == ERANGE || !isfinite(value)) {
        return NAN;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    return *end == '\0' ? value : NAN;
}

/*
 * Parse and sum one batch with a parallel for over rows.
 * Preconditions: lines contains num_rows complete, unquoted CSV records;
 * 0 <= num_cols <= num_fields <= MAX_FIELDS; stats has num_cols entries;
 * num_threads > 0. Existing stats contain sums/counts from previous batches.
 * Postconditions: adds this batch's valid numbers to stats and modifies lines
 * in place. Returns num_rows on success, or the first malformed row index.
 * Returns SIZE_MAX if a thread cannot allocate its numeric locale.
 * On failure, stats may contain partial results and must not be reported.
 */
static size_t accumulateBatch(char lines[][MAX_LINE_LEN], size_t num_rows,
                              int num_fields, int num_cols, int num_threads,
                              ColumnStat *stats) {
    size_t bad_row = num_rows;
    int locale_failed = 0;
    #pragma omp parallel num_threads(num_threads) reduction(min:bad_row) reduction(|:locale_failed)
    {
        // Private locales avoid contention during concurrent numeric conversion.
        locale_t numeric_locale = newlocale(LC_NUMERIC_MASK, "C", NULL);
        if (!numeric_locale) locale_failed = 1;
        double partial_sums[MAX_FIELDS] = {0};
        size_t partial_counts[MAX_FIELDS] = {0};

        #pragma omp for schedule(static)
        for (size_t row = 0; row < num_rows; row++) {
            if (!numeric_locale) continue;
            char *fields[MAX_FIELDS];
            if (splitCsvFields(lines[row], fields, MAX_FIELDS) != num_fields) {
                if (row < bad_row) bad_row = row;
                continue;
            }
            for (int col = 0; col < num_cols; col++) {
                double value = parseNumericField(fields[col], numeric_locale);
                if (isfinite(value)) {
                    partial_sums[col] += value;
                    partial_counts[col]++;
                }
            }
        }
        if (numeric_locale) freelocale(numeric_locale);

        // Keep partial averages as sums/counts until the final division.
        // This weights them correctly without dividing and multiplying again.
        #pragma omp critical
        {
            for (int col = 0; col < num_cols; col++) {
                stats[col].total += partial_sums[col];
                stats[col].count += partial_counts[col];
            }
        }
    }
    if (locale_failed) {
        fprintf(stderr, "Could not allocate a thread's numeric locale\n");
        return SIZE_MAX;
    }
    return bad_row;
}

static void printResults(const ColumnStat *global_stats, int num_avg_cols, double start_time, double end_time) {
    printf("Elapsed time: %f seconds\n", end_time - start_time);
    printf("%-10s %12s %18s %15s\n", "Column", "Count", "Total", "Average");
    for (int c = 0; c < num_avg_cols; c++) {
        double avg = (global_stats[c].count > 0) ? global_stats[c].total / global_stats[c].count : NAN;
        printf("%-10s %12.0f %18.4f %15.8f\n",
               global_stats[c].name, global_stats[c].count, global_stats[c].total, avg);
    }
}

int main(int argc, char **argv) {
    // Boring Proper Program Use Stuff
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <num_threads> [path_to_csv]\n", argv[0]);
        return 1;
    }

    int num_threads = atoi(argv[1]);
    if (num_threads <= 0) {
        fprintf(stderr, "num_threads must be a positive integer\n");
        return 1;
    }
    const char *csv_path = (argc >= 3) ? argv[2] : DEFAULT_CSV_PATH;

    double start_time = omp_get_wtime(); //start timer

    FILE *fp = fopen(csv_path, "r"); //open file
    if (!fp) {
        perror("fopen");
        return 1;
    }

    // Read and parse the header line to recover the column names.
    char header_line[MAX_LINE_LEN];
    if (!fgets(header_line, sizeof(header_line), fp)) {
        fprintf(stderr, "Failed to read header line\n");
        fclose(fp);
        return 1;
    }
    if (!strchr(header_line, '\n') && !feof(fp)) {
        fprintf(stderr, "Header exceeds the line buffer size\n");
        fclose(fp);
        return 1;
    }
    char column_names[MAX_FIELDS][MAX_COL_NAME_LEN];
    char *fields[MAX_FIELDS];
    int num_fields = splitCsvFields(header_line, fields, MAX_FIELDS);
    if (num_fields < 0 || (num_fields == 1 && fields[0][0] == '\0')) {
        fprintf(stderr, "Header is empty or exceeds %d columns\n", MAX_FIELDS);
        fclose(fp);
        return 1;
    }
    for (int col = 0; col < num_fields; col++) {
        snprintf(column_names[col], sizeof(column_names[col]), "%s", fields[col]);
    }
    int num_avg_cols = num_fields;
    if (strcmp(column_names[num_fields - 1], "Class") == 0) {
        num_avg_cols--;
    }

    ColumnStat global_stats[MAX_FIELDS] = {0};
    for (int col = 0; col < num_avg_cols; col++) {
        snprintf(global_stats[col].name, MAX_COL_NAME_LEN, "%s", column_names[col]);
    }

    // Reuse one 32 MiB buffer instead of retaining the whole CSV and matrix.
    char (*lines)[MAX_LINE_LEN] = malloc(BATCH_ROWS * sizeof(*lines));
    if (!lines) {
        fprintf(stderr, "Out of memory allocating batch buffer\n");
        fclose(fp);
        return 1;
    }

    size_t total_rows = 0;
    while (1) {
        size_t batch_rows = 0;
        while (batch_rows < BATCH_ROWS && fgets(lines[batch_rows], MAX_LINE_LEN, fp)) {
            if (!strchr(lines[batch_rows], '\n') && !feof(fp)) {
                fprintf(stderr, "CSV line %zu exceeds the line buffer size\n",
                        total_rows + batch_rows + 2);
                goto failure;
            }
            batch_rows++;
        }
        if (ferror(fp)) {
            perror("Error reading CSV");
            goto failure;
        }
        if (batch_rows == 0) break;

        size_t bad_row = accumulateBatch(lines, batch_rows, num_fields,
                                        num_avg_cols, num_threads, global_stats);
        if (bad_row == SIZE_MAX) goto failure;
        if (bad_row != batch_rows) {
            fprintf(stderr, "CSV line %zu: expected %d fields\n",
                    total_rows + bad_row + 2, num_fields);
            goto failure;
        }
        total_rows += batch_rows;
    }

    free(lines);
    fclose(fp);
    printf("Loaded %zu data rows from %s\n", total_rows, csv_path);
    printResults(global_stats, num_avg_cols, start_time, omp_get_wtime());
    return 0;

failure:
    free(lines);
    fclose(fp);
    return 1;
}
