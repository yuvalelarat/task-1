#include "uthreads.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>

static struct timeval start_time;
#define FIXED_QUANTUM_US 100000 // 100ms

long get_elapsed_ms() {
    struct timeval current;
    gettimeofday(&current, NULL);
    return (current.tv_sec - start_time.tv_sec) * 1000 +
           (current.tv_usec - start_time.tv_usec) / 1000;
}

int tid1, tid2, tid3, tid4;

void worker_thread_1_wrapper() {
    printf("[%ld ms] Thread %d started\n", get_elapsed_ms(), tid1);

    for (int i = 0; i < 3; i++) {
        printf("[%ld ms] Thread %d working... iteration %d\n", get_elapsed_ms(), tid1, i + 1);
        volatile long count = 0;
        for (long j = 0; j < 20000000; j++) count++;
    }

    printf("[%ld ms] Thread %d finished\n", get_elapsed_ms(), tid1);
    uthread_exit(tid1);
}

void worker_thread_2_wrapper() {
    printf("[%ld ms] Thread %d started\n", get_elapsed_ms(), tid2);

    for (int i = 0; i < 3; i++) {
        printf("[%ld ms] Thread %d working... iteration %d\n", get_elapsed_ms(), tid2, i + 1);
        volatile long count = 0;
        for (long j = 0; j < 20000000; j++) count++;
    }

    printf("[%ld ms] Thread %d finished\n", get_elapsed_ms(), tid2);
    uthread_exit(tid2);
}

void sleeping_thread_wrapper() {
    printf("[%ld ms] Thread %d (sleeper) started\n", get_elapsed_ms(), tid3);
    volatile long count = 0;
    for (long j = 0; j < 10000000; j++) count++;
    printf("[%ld ms] Thread %d sleeping for 2 quantums\n", get_elapsed_ms(), tid3);
    uthread_sleep_quantums(2);
    printf("[%ld ms] Thread %d woke up\n", get_elapsed_ms(), tid3);
    for (long j = 0; j < 10000000; j++) count++;
    printf("[%ld ms] Thread %d finished\n", get_elapsed_ms(), tid3);
    uthread_exit(tid3);
}

void quick_thread_wrapper() {
    printf("[%ld ms] Thread %d (quick) started and finished\n", get_elapsed_ms(), tid4);
    uthread_exit(tid4);
}

int main() {
    printf("=== User-Level Threading Library Test ===\n");
    gettimeofday(&start_time, NULL);

    if (uthread_system_init(FIXED_QUANTUM_US) != 0) {
        printf("Failed to initialize threading system\n");
        return 1;
    }

    printf("[%ld ms] Threading system initialized\n", get_elapsed_ms());

    tid1 = uthread_create(worker_thread_1_wrapper);
    tid2 = uthread_create(worker_thread_2_wrapper);
    tid3 = uthread_create(sleeping_thread_wrapper);
    tid4 = uthread_create(quick_thread_wrapper);

    printf("[%ld ms] Created threads: %d, %d, %d, %d\n", get_elapsed_ms(), tid1, tid2, tid3, tid4);

    raise(SIGVTALRM); //trigger signal to start

    while (1) {
        pause(); //let it run
    }

    return 0;
}