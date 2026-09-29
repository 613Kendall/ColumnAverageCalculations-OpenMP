// Parse the CSV serially into a row-major matrix, then sum each column
// with an independent OpenMP task. Missing and invalid numbers are skipped.
// Usage: ./column_averages_v2 <num_threads> [path_to_csv]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <omp.h>

#define MAX_LINE_LEN 4096
#define MAX_COL_NAME_LEN 32
#define MAX_FIELDS 256

const char* DEFAULT_CSV_PATH = "lab4/data/fitness_0.5x.csv";

typedef struct {
    char name[MAX_COL_NAME_LEN];
    double count;
    double total;
} ColumnStat;

/*
 * Split an unquoted CSV record, preserving empty fields: "10,,30," has four.
 * Pre: line is writable and null-terminated, with no embedded newlines;
 * fields has room for max_fields pointers, and max_fields > 0.
 * Post: returns the field count, replacing commas/line endings with '\0'.
 * fields points into line, which must remain alive while the pointers are used.
 * Returns -1 on overflow; discard the partially modified line and fields.
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
 * Preconditions: field points to a null-terminated string.
 * Postconditions: leaves field unchanged and returns its numeric value, or
 * NAN for empty, nonnumeric, out-of-range, or nonfinite input. The caller must
 * exclude NAN from both the sum and count when computing an average.
 */
static double parseNumericField(const char *field) {
    char *end;
    errno = 0;
    double value = strtod(field, &end);
    if (end == field || errno == ERANGE || !isfinite(value)) {
        return NAN;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    return *end == '\0' ? value : NAN;
}

/*
 * Compute independent column sums with one OpenMP task per column.
 * Pre: data contains nlines rows of num_avg_cols doubles; global_stats has
 * num_avg_cols entries. Missing or invalid values are represented by NAN.
 * Post: each entry holds its column's valid count and sum; names are unchanged.
 */
static void calculateColumnAverages(const double *data, size_t nlines,
                                    int num_avg_cols, ColumnStat *global_stats) {
    #pragma omp parallel
    {
        #pragma omp single
        {
            for (int c = 0; c < num_avg_cols; c++) {
                #pragma omp task firstprivate(c) shared(data, global_stats)
                {
                    double count = 0.0, total = 0.0;
                    for (size_t i = 0; i < nlines; i++) {
                        double value = data[i * (size_t)num_avg_cols + c];
                        if (isfinite(value)) {
                            total += value;
                            count += 1.0;
                        }
                    }
                    global_stats[c].count = count;
                    global_stats[c].total = total;
                }
            }
        } // The implicit barrier waits for all column tasks.
    }
}

// Free row strings in [first, count), then the array that owns their pointers.
static void freeLines(char **lines, size_t first, size_t count) {
    for (size_t i = first; i < count; i++) free(lines[i]);
    free(lines);
}

static void printResultsAndCleanUp(ColumnStat *global_stats, double *data,
                                   int num_avg_cols, double start_time, double end_time) {
    printf("Elapsed time: %f seconds\n", end_time - start_time);
    printf("%-10s %12s %18s %15s\n", "Column", "Count", "Total", "Average");
    for (int c = 0; c < num_avg_cols; c++) {
        double avg = (global_stats[c].count > 0) ? global_stats[c].total / global_stats[c].count : NAN;
        printf("%-10s %12.0f %18.4f %15.8f\n",
               global_stats[c].name, global_stats[c].count, global_stats[c].total, avg);
    }
    free(global_stats);
    free(data);
}

int main(int argc, char **argv) {
    // Validate the command-line arguments.
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

    double start_time = omp_get_wtime();

    FILE *fp = fopen(csv_path, "r");
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

    // Read all data rows into memory.
    size_t capacity = 300000;
    size_t nlines = 0;
    char **lines = malloc(capacity * sizeof(char *));
    if (!lines) {
        fprintf(stderr, "Out of memory allocating line buffer\n");
        fclose(fp);
        return 1;
    }

    char buf[MAX_LINE_LEN];
    while (fgets(buf, sizeof(buf), fp)) {
        if (!strchr(buf, '\n') && !feof(fp)) {
            fprintf(stderr, "CSV line %zu exceeds the line buffer size\n", nlines + 2);
            freeLines(lines, 0, nlines);
            fclose(fp);
            return 1;
        }
        if (nlines == capacity) {
            capacity *= 2;
            char **grown = realloc(lines, capacity * sizeof(char *));
            if (!grown) {
                fprintf(stderr, "Out of memory growing line buffer\n");
                fclose(fp);
                return 1;
            }
            lines = grown;
        }
        lines[nlines] = strdup(buf);
        if (!lines[nlines]) {
            fprintf(stderr, "Out of memory copying CSV row\n");
            freeLines(lines, 0, nlines);
            fclose(fp);
            return 1;
        }
        nlines++;
    }
    fclose(fp);

    printf("Loaded %zu data rows from %s\n", nlines, csv_path);

    // Parse every row into a row-major numeric matrix up front (serial), so
    // each column-task can read doubles directly. NAN marks missing or
    // invalid numeric values, which do not contribute to the average.
    size_t num_values = nlines * (size_t)num_avg_cols;
    double *data = malloc((num_values ? num_values : 1) * sizeof(double));
    if (!data) {
        fprintf(stderr, "Out of memory allocating numeric matrix\n");
        return 1;
    }
    for (size_t i = 0; i < nlines; i++) {
        int field_count = splitCsvFields(lines[i], fields, MAX_FIELDS);
        if (field_count != num_fields) {
            fprintf(stderr, "CSV line %zu: expected %d fields, found %d (-1 means too many)\n",
                    i + 2, num_fields, field_count);
            freeLines(lines, i, nlines);
            free(data);
            return 1;
        }
        for (int col = 0; col < num_avg_cols; col++) {
            data[i * (size_t)num_avg_cols + col] = parseNumericField(fields[col]);
        }
        free(lines[i]);
    }
    free(lines);

    omp_set_num_threads(num_threads);

    ColumnStat *global_stats = calloc((size_t)num_avg_cols, sizeof(ColumnStat));
    if (!global_stats) {
        fprintf(stderr, "Out of memory allocating global stats\n");
        free(data);
        return 1;
    }
    for (int c = 0; c < num_avg_cols; c++) {
        strncpy(global_stats[c].name, column_names[c], sizeof(global_stats[c].name) - 1);
        global_stats[c].name[sizeof(global_stats[c].name) - 1] = '\0';
    }

    calculateColumnAverages(data, nlines, num_avg_cols, global_stats);

    double end_time = omp_get_wtime();
    printResultsAndCleanUp(global_stats, data, num_avg_cols, start_time, end_time);
    return 0;
}
