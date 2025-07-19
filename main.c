#include "uthreads.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

static int thread_counter = 0;
static struct timeval start_time;

#define FIXED_QUANTUM_US 100000 // 100ms

long get_elapsed_ms() {
    struct timeval current;
    gettimeofday(&current, NULL);
    return (current.tv_sec - start_time.tv_sec) * 1000 +
           (current.tv_usec - start_time.tv_usec) / 1000;
}

void worker_thread_1(void) {
    int thread_id = ++thread_counter;
    printf("[%ld ms] Thread %d started\n", get_elapsed_ms(), thread_id);
    for (int i = 0; i < 3; i++) {
        printf("[%ld ms] Thread %d working... iteration %d\n",
               get_elapsed_ms(), thread_id, i + 1);
        volatile long count = 0;
        for (long j = 0; j < 20000000; j++) count++;  // smaller loop
    }
    printf("[%ld ms] Thread %d finished\n", get_elapsed_ms(), thread_id);
    uthread_exit(thread_id);  // call exit explicitly
}

void sleeping_thread(void) {
    int thread_id = ++thread_counter;
    printf("[%ld ms] Thread %d (sleeper) started\n", get_elapsed_ms(), thread_id);
    volatile long count = 0;
    for (long j = 0; j < 10000000; j++) count++;
    printf("[%ld ms] Thread %d sleeping for 2 quantums\n", get_elapsed_ms(), thread_id);
    uthread_sleep_quantums(2);
    printf("[%ld ms] Thread %d woke up\n", get_elapsed_ms(), thread_id);
    for (long j = 0; j < 10000000; j++) count++;
    printf("[%ld ms] Thread %d finished\n", get_elapsed_ms(), thread_id);
    uthread_exit(thread_id);
}

void quick_thread(void) {
    int thread_id = ++thread_counter;
    printf("[%ld ms] Thread %d (quick) started and finished\n", get_elapsed_ms(), thread_id);
    uthread_exit(thread_id);
}

int main() {
    printf("=== User-Level Threading Library Test ===\n");
    gettimeofday(&start_time, NULL);

    if (uthread_system_init(FIXED_QUANTUM_US) != 0) {
        printf("Failed to initialize threading system\n");
        return 1;
    }

    printf("[%ld ms] Threading system initialized\n", get_elapsed_ms());

    // Create threads
    int tid1 = uthread_create(worker_thread_1);
    int tid2 = uthread_create(worker_thread_1);
    int tid3 = uthread_create(sleeping_thread);
    int tid4 = uthread_create(quick_thread);

    printf("[%ld ms] Created threads: %d, %d, %d, %d\n", get_elapsed_ms(), tid1, tid2, tid3, tid4);

    // Main() thread doing some loops so the other threads can run
    for (int i = 0; i < 2000; i++) {
        if (i % 1000 == 0) {
            printf("[%ld ms] Main() thread working iteration %d\n", get_elapsed_ms(), i / 1000 + 1);
        }
        // printf("[%ld ms] Main() thread working iteration %d\n", get_elapsed_ms(), i + 1);
        volatile long count = 0;
        for (long j = 0; j < 10000000; j++) count++;
    }

    printf("[%ld ms] Main() thread exiting\n", get_elapsed_ms());
    return 0;
}