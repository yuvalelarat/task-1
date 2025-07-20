#define _POSIX_C_SOURCE 200112L
#include "uthreads.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <setjmp.h>

#define INVALID_TID -1

typedef enum { READY, RUNNING, BLOCKED, TERMINATED } ThreadState;

typedef struct {
    int tid;
    ThreadState state;
    sigjmp_buf context;
    char stack[UTHREAD_STACK_BYTES];
    int sleep_quantums;
} uthread_t;

static uthread_t threads[UTHREAD_MAX_THREADS];
static int current_tid = 0;
static int quantum_us = 0;
static int num_threads = 1;
static struct sigaction sa;
static struct itimerval timer;
static sigset_t vt_sigset;

static int ready_queue[UTHREAD_MAX_THREADS];
static int rq_front = 0, rq_rear = 0;

#ifdef __x86_64__
#define JB_SP 6
#define JB_PC 7
typedef unsigned long addr_t;
addr_t translate_address(addr_t addr) {
    addr_t ret;
    asm volatile("xor %%fs:0x30,%0\n rol $0x11,%0"
                 : "=g"(ret)
                 : "0"(addr));
    return ret;
}
#else
#define JB_SP 4
#define JB_PC 5
typedef unsigned int addr_t;
addr_t translate_address(addr_t addr) {
    addr_t ret;
    asm volatile("xor %%gs:0x18,%0\n rol $0x9,%0"
                 : "=g"(ret)
                 : "0"(addr));
    return ret;
}
#endif

static void enqueue(int tid) { //push to the back of the queue
    ready_queue[rq_rear] = tid;
    rq_rear = (rq_rear + 1) % UTHREAD_MAX_THREADS;
}

static int dequeue() { //pop from the front of the queue
    if (rq_front == rq_rear) return INVALID_TID;
    int tid = ready_queue[rq_front];
    rq_front = (rq_front + 1) % UTHREAD_MAX_THREADS;
    return tid;
}

static int is_thread_runable(int tid){
    return threads[tid].state == READY && threads[tid].sleep_quantums == 0;
}

static void scheduler(int sig) {
    sigprocmask(SIG_BLOCK, &vt_sigset, NULL);

    int ret = sigsetjmp(threads[current_tid].context, 1); //save the current execution context of the running thread into a buffer
    if (ret != 0) {
        sigprocmask(SIG_UNBLOCK, &vt_sigset, NULL);
        return;
    }

    
    if (threads[current_tid].state == RUNNING) {
        threads[current_tid].state = READY;
        enqueue(current_tid);
    }

    for (int i = 0; i < UTHREAD_MAX_THREADS; i++) {
        if (threads[i].sleep_quantums > 0) {
            threads[i].sleep_quantums--; //decrement sleep quantums for threads that are ready but sleeping
        }
    }

    // Find next ready thread to run
    int next_tid = INVALID_TID;
    int queue_size = (rq_rear - rq_front + UTHREAD_MAX_THREADS) % UTHREAD_MAX_THREADS;
    
    for (int i = 0; i < queue_size; i++) {
        int tid = dequeue();
        if (tid == INVALID_TID) break;
        // Check if thread is ready and not sleeping
        if (is_thread_runable(tid)) {
            next_tid = tid; //found a thread that is ready to run
            break;
        }
        if (threads[tid].state == READY) enqueue(tid); //if still ready, put it back in the queue
    }

    
    if (next_tid == INVALID_TID) {//exit if no thread found
        int all_others_terminated = 1;
        for (int i = 1; i < UTHREAD_MAX_THREADS; ++i) {
            if (threads[i].state != TERMINATED) {
                all_others_terminated = 0;
                break;
            }
        }

        if (all_others_terminated) {
            if (threads[0].state != TERMINATED && current_tid != 0) {
                current_tid = 0;
                threads[current_tid].state = RUNNING;
                sigprocmask(SIG_UNBLOCK, &vt_sigset, NULL);
                siglongjmp(threads[current_tid].context, 1);
            } else if (current_tid == 0) {
                sigprocmask(SIG_UNBLOCK, &vt_sigset, NULL);
                exit(0);
            }
        }
        sigprocmask(SIG_UNBLOCK, &vt_sigset, NULL);
        return;
    }

    
    current_tid = next_tid;//switch to the next thread
    threads[current_tid].state = RUNNING;
    
    sigprocmask(SIG_UNBLOCK, &vt_sigset, NULL);
    siglongjmp(threads[current_tid].context, 1); //restore the context of the next thread to run
}

int uthread_system_init(int quantum_usecs) {
    if (quantum_usecs <= 0) return -1; //invalid quantum value handling - failure
    quantum_us = quantum_usecs;

    for (int i = 0; i < UTHREAD_MAX_THREADS; ++i) { //intialize threads on the memory
        threads[i].tid = 0;
        threads[i].state = TERMINATED;
        threads[i].sleep_quantums = 0;
    }

    threads[0].tid = 0; //main thread
    threads[0].state = RUNNING;

    if (sigsetjmp(threads[0].context, 1) == 0) {
    }

    sigemptyset(&sa.sa_mask); //when the signal handler is running dont block any additional signals
    sa.sa_handler = &scheduler; //when signal SIGVTALRM is received call scheduler
    sa.sa_flags = 0;
    if (sigaction(SIGVTALRM, &sa, NULL) < 0) return -1; //install the SIGVTALRM signal handler to manage virtual timer interrupts (used for context switching)

    timer.it_value.tv_sec = 0; //set the initial value of the timer to 0 seconds
    timer.it_value.tv_usec = quantum_us; //set the initial value of the timer to quantum_usecs microseconds
    timer.it_interval.tv_sec = 0; //set the interval of the timer to 0 seconds
    timer.it_interval.tv_usec = quantum_us; //set the interval of the timer to quantum_usecs microseconds
    if (setitimer(ITIMER_VIRTUAL, &timer, NULL) < 0) return -1; //set the timer to ITIMER_VIRTUAL, which counts only when the process is executing in user mode

    sigemptyset(&vt_sigset); //initialize the signal set
    sigaddset(&vt_sigset, SIGVTALRM); //add the SIGVTALRM signal to the set

    return 0; //success
}

int uthread_create(uthread_entry func) {
    sigprocmask(SIG_BLOCK, &vt_sigset, NULL);

    if (num_threads >= UTHREAD_MAX_THREADS) {
        sigprocmask(SIG_UNBLOCK, &vt_sigset, NULL);
        return -1; 
    }

    int tid = -1;
    for (int i = 1; i < UTHREAD_MAX_THREADS; ++i) {
        if (threads[i].state == TERMINATED) {
            tid = i; //find an available thread id
            break;
        }
    }
    if (tid == -1) {
        sigprocmask(SIG_UNBLOCK, &vt_sigset, NULL);
        return -1;
    }

    threads[tid].tid = tid;
    threads[tid].state = READY;
    threads[tid].sleep_quantums = 0;

    addr_t sp = (addr_t)(threads[tid].stack + UTHREAD_STACK_BYTES - sizeof(addr_t)); //set the stack pointer to the top of the thread's stack (black box)
    addr_t pc = (addr_t)func; //set the program counter to the entry function of the thread (black box)

    sigsetjmp(threads[tid].context, 1); //saving the current execution context of the running thread into a buffer
    threads[tid].context->__jmpbuf[JB_SP] = translate_address(sp); //set the stack pointer in the context to the translated address of the stack pointer
    threads[tid].context->__jmpbuf[JB_PC] = translate_address(pc); //set the program counter in the context to the translated address of the entry function
    sigemptyset(&threads[tid].context->__saved_mask); //initialize the saved signal mask in the context

    enqueue(tid); //add the new thread to the ready queue
    num_threads++;

    sigprocmask(SIG_UNBLOCK, &vt_sigset, NULL);
    return tid; //success returning the tid of new thread
}

int uthread_exit(int tid) {
    sigprocmask(SIG_BLOCK, &vt_sigset, NULL);

    if (tid < 0 || tid >= UTHREAD_MAX_THREADS) {
        sigprocmask(SIG_UNBLOCK, &vt_sigset, NULL);
        return -1;
    }

    if (tid == 0) exit(0);

    threads[tid].state = TERMINATED; //"freeing" the thread as i didnt make it dynamically allocated
    num_threads--; 
    
    sigprocmask(SIG_UNBLOCK, &vt_sigset, NULL);

    if (tid == current_tid) scheduler(0); //if the thread here is currently running switch to next thread

    return 0;
}

int uthread_block(int tid) {
    sigprocmask(SIG_BLOCK, &vt_sigset, NULL);

    if (tid <= 0 || tid >= UTHREAD_MAX_THREADS ||
        (threads[tid].state != RUNNING && threads[tid].state != READY)) {
        sigprocmask(SIG_UNBLOCK, &vt_sigset, NULL);
        return -1;
    }

    threads[tid].state = BLOCKED;

    sigprocmask(SIG_UNBLOCK, &vt_sigset, NULL);

    if (tid == current_tid) scheduler(0);

    return 0;
}

int uthread_unblock(int tid) {
    sigprocmask(SIG_BLOCK, &vt_sigset, NULL);

    if (tid <= 0 || tid >= UTHREAD_MAX_THREADS || threads[tid].state != BLOCKED) {
        sigprocmask(SIG_UNBLOCK, &vt_sigset, NULL);
        return -1;
    }

    threads[tid].state = READY;
    enqueue(tid);

    sigprocmask(SIG_UNBLOCK, &vt_sigset, NULL);

    return 0;
}

int uthread_sleep_quantums(int num_quantums) {
    sigprocmask(SIG_BLOCK, &vt_sigset, NULL);

    if (current_tid == 0 || num_quantums <= 0) {
        sigprocmask(SIG_UNBLOCK, &vt_sigset, NULL);
        return -1;
    }

    threads[current_tid].sleep_quantums = num_quantums;
    threads[current_tid].state = READY;
    enqueue(current_tid);

    sigprocmask(SIG_UNBLOCK, &vt_sigset, NULL);

    scheduler(0);

    return 0;
}