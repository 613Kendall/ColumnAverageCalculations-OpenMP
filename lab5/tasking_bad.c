#include <stdio.h>
#include <omp.h>

volatile long long g_sink; // observable side effect keeps -O2 from eliding the loop

// Simulated workload function
void do_work(long long iterations) {
    long long sum = 0;
    for (long long i = 0; i < iterations; i++) {
        sum += i;
    }
    g_sink = sum;
}

int main() {
    // Set up a parallel region
    #pragma omp parallel
    {
        #pragma omp single
        {
            printf("Thread %d is generating unbalanced tasks...\n", omp_get_thread_num());

            // BAD LOAD BALANCING: Task 0 is massive
            #pragma omp task
            {
                int tid = omp_get_thread_num();
                printf("Thread %d started the HEAVY task.\n", tid);
                do_work(2000000000LL); // Huge workload
                printf("Thread %d finished the HEAVY task.\n", tid);
            }

            // Tasks 1 through 8 are trivial
            for (int i = 1; i <= 8; i++) {
                #pragma omp task
                {
                    do_work(1000LL); // Tiny workload
                }
            }
        } // Implicit barrier: all tasks must finish here
    }

    printf("All tasks completed.\n");
    return 0;
}
