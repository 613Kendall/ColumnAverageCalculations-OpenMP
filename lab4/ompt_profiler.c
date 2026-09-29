#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <omp.h>
#include <omp-tools.h>
#include <pthread.h>

/*
 * Information we maintain for every OpenMP task.
 */
typedef struct {
    uint64_t task_id;

    int creator_thread;

    int executor_thread;

    double start_time;

    double end_time;

    int running;
} task_info_t;


/*
 * Maximum number of tasks for this example.
 *
 * Increase this if your application creates more tasks.
 */
#define MAX_TASKS 1000000

static task_info_t *tasks;

static uint64_t next_task_id = 1;

static FILE *log_file;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;


/*
 * Convert OpenMP wall-clock time to microseconds.
 */
static double now_us(void)
{
    return omp_get_wtime() * 1000000.0;
}


/*
 * Find information associated with an OMPT task.
 */
static task_info_t *get_task(ompt_data_t *data)
{
    if (data == NULL)
        return NULL;

    uint64_t id = data->value;

    if (id == 0 || id >= MAX_TASKS)
        return NULL;

    return &tasks[id];
}


/*
 * ---------------------------------------------------------
 * TASK CREATION
 * ---------------------------------------------------------
 */
static void on_task_create(
    ompt_data_t *encountering_task_data,
    const ompt_frame_t *encountering_task_frame,
    ompt_data_t *new_task_data,
    int type,
    int has_dependences,
    const void *codeptr_ra)
{
    pthread_mutex_lock(&lock);

    uint64_t id = next_task_id++;

    if (id >= MAX_TASKS) {
        fprintf(stderr,
                "ERROR: MAX_TASKS exceeded\n");

        pthread_mutex_unlock(&lock);
        return;
    }

    /*
     * Store our profiler task ID inside the OMPT task data.
     */
    new_task_data->value = id;

    task_info_t *task = &tasks[id];

    task->task_id = id;

    /*
     * The thread creating the task.
     */
    task->creator_thread =
        omp_get_thread_num();

    task->executor_thread = -1;

    task->start_time = 0.0;
    task->end_time = 0.0;

    task->running = 0;

    pthread_mutex_unlock(&lock);
}


/*
 * ---------------------------------------------------------
 * TASK SCHEDULE
 * ---------------------------------------------------------
 *
 * This callback is invoked when the OpenMP runtime changes
 * from one task to another.
 *
 * prior_task_data:
 *      task that was running
 *
 * prior_status:
 *      why that task stopped running
 *
 * next_task_data:
 *      task that is going to run
 */
static void on_task_schedule(
    ompt_data_t *prior_task_data,
    ompt_task_status_t prior_status,
    ompt_data_t *next_task_data)
{
    pthread_mutex_lock(&lock);

    /*
     * --------------------------------------------
     * Previous task
     * --------------------------------------------
     */

    task_info_t *previous =
        get_task(prior_task_data);

    if (previous != NULL) {

        /*
         * The task is actually finished.
         */
        if (prior_status == ompt_task_complete) {

            previous->end_time = now_us();

            previous->running = 0;
        }
    }


    /*
     * --------------------------------------------
     * Next task
     * --------------------------------------------
     */

    task_info_t *next =
        get_task(next_task_data);

    if (next != NULL) {

        /*
         * Record the thread that actually executes
         * the task.
         */
        next->executor_thread =
            omp_get_thread_num();

        /*
         * Record the first time the task starts.
         *
         * If the task is suspended and later resumed,
         * don't overwrite the original start time.
         */
        if (!next->running &&
            next->start_time == 0.0) {

            next->start_time = now_us();
        }

        next->running = 1;
    }

    pthread_mutex_unlock(&lock);
}


/*
 * ---------------------------------------------------------
 * OMPT INITIALIZATION
 * ---------------------------------------------------------
 */
static int ompt_initialize(
    ompt_function_lookup_t lookup,
    int initial_device_num,
    ompt_data_t *tool_data)
{
    ompt_set_callback_t set_callback;

    set_callback =
        (ompt_set_callback_t)
        lookup("ompt_set_callback");


    /*
     * Allocate task table.
     */
    tasks = calloc(
        MAX_TASKS,
        sizeof(task_info_t));

    if (tasks == NULL) {

        fprintf(stderr,
                "Could not allocate task table\n");

        return 0;
    }


    /*
     * Open CSV file.
     */
    log_file =
        fopen("tasks.csv", "w");

    if (log_file == NULL) {

        fprintf(stderr,
                "Could not open tasks.csv\n");

        free(tasks);

        return 0;
    }


    /*
     * Register callbacks.
     */
    set_callback(
        ompt_callback_task_create,
        (ompt_callback_t)
        on_task_create);

    set_callback(
        ompt_callback_task_schedule,
        (ompt_callback_t)
        on_task_schedule);


    /*
     * Header for our final CSV.
     */
    fprintf(
        log_file,
        "task_id,creator_thread,"
        "executor_thread,start_time,end_time,duration\n");


    fflush(log_file);

    return 1;
}


/*
 * ---------------------------------------------------------
 * OMPT FINALIZATION
 * ---------------------------------------------------------
 */
static void ompt_finalize(
    ompt_data_t *tool_data)
{
    if (log_file != NULL) {

        /*
         * Write all completed tasks.
         */
        for (uint64_t i = 1;
             i < next_task_id;
             i++) {

            task_info_t *task =
                &tasks[i];

            /*
             * Ignore tasks that never obtained
             * a recorded execution interval.
             */
            if (task->start_time == 0.0)
                continue;

            if (task->end_time == 0.0)
                continue;

            double duration =
                task->end_time -
                task->start_time;

            fprintf(
                log_file,
                "%llu,%d,%d,%.3f,%.3f,%.3f\n",
                task->task_id,
                task->creator_thread,
                task->executor_thread,
                task->start_time,
                task->end_time,
                duration);
        }

        fclose(log_file);
    }


    free(tasks);
}


/*
 * ---------------------------------------------------------
 * OMPT ENTRY POINT
 * ---------------------------------------------------------
 */
ompt_start_tool_result_t *
ompt_start_tool(
    unsigned int omp_version,
    const char *runtime_version)
{
    static ompt_start_tool_result_t result = {
        .initialize = ompt_initialize,
        .finalize = ompt_finalize,
        .tool_data = {0}
    };

    return &result;
}


