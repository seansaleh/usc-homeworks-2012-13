#include "kernel.h"
#include "config.h"
#include "globals.h"
#include "errno.h"

#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"
#include "util/printf.h"

#include "proc/kthread.h"
#include "proc/proc.h"
#include "proc/sched.h"
#include "proc/proc.h"
#include "proc/kmutex.h"

#include "mm/slab.h"
#include "mm/page.h"
#include "mm/mmobj.h"
#include "mm/mm.h"
#include "mm/mman.h"

ktqueue_t wake_me_q;
int wake_me_len = 0;
int race=0;

kmutex_t mutex;

typedef struct {
    struct proc *p;
    struct kthread *t;
} proc_thread_t;

/*
 * Create a process and a thread with the given name and calling teh given
 * function. Arg1 is passed to the function (arg2 is always NULL).  The thread
 * is immediately placed on the run queue.  A proc_thread_t is returned, giving
 * the caller a pointer to the new process and thread to coordinate tests.  NB,
 * the proc_thread_t is returned by value, so there are no stack problems.
 */
static proc_thread_t start_proc(char *name, kthread_func_t f, int arg1) {
    proc_thread_t pt;

    pt.p = proc_create(name);
    pt.t = kthread_create(pt.p, f, arg1, NULL);
    KASSERT(pt.p && pt.t && "Cannot create thread or process");
    sched_make_runnable(pt.t);
    return pt;
}

/**
 * Call do_waitpid with the process ID of the given process.  Print a debug
 * message with the exiting process's status.
 */
static void wait_for_proc(proc_t *p) {
    int rv;
    pid_t pid;
    char pname[PROC_NAME_LEN];

    strncpy(pname, p->p_comm, PROC_NAME_LEN); 
    pid = do_waitpid(p->p_pid, 0, &rv);
    dbg_print("%s (%d) exited: %d\n", pname, pid, rv);
}

/**
 * Call waitpid with a -1 pid and print a message about any process that exits.
 * Returns the pid found, including -ECHILD when this process has no children.
 */
static pid_t wait_for_any() {
    int rv;
    pid_t pid;

    pid = do_waitpid(-1, 0, &rv);
    if ( pid != -ECHILD) dbg_print("child (%d) exited: %d\n", pid, rv);
    return pid;
}

/*
 * Repeatedly call wait_for_any() until it returns -ECHILD
 */
static void wait_for_all() {
    while (wait_for_any() != -ECHILD) 
	;
}

/*
 * Keep context switching until *count >= tot.  Used to count the number of
 * nodes waiting for the kernel thread queue.
 */
static void stop_until_queued(int tot, int *count) {
    while ( *count < tot) 
	sched_switch();
}

/*
 * A thread function that simply calls do_exit() with status arg1
 */
void *waitpid_test(int arg1, void *arg2) {
    do_exit(arg1);
    return NULL;
}

/*
 * A thread function that returns NULL, silently invoking kthread_exit()
 */
void *kthread_exit_test(int arg1, void *arg2) {
    return NULL;
}

/*
 * A thread function that waits on wake_me_q and exist when released.  If it is
 * cancelled, it prints an error message.
 */
void *wakeme_test(int arg1, void *arg2) {
    wake_me_len++;
    if (sched_cancellable_sleep_on(&wake_me_q) == -EINTR ) {
	dbg_print("Wakeme cancelled?! pid (%d)\n", curproc->p_pid);
	wake_me_len--;
	do_exit(-1);
    }
    wake_me_len--;
    do_exit(arg1);
    return NULL;
}

/*
 * A thread function that uncancellably waits on wake_me_q and exist when
 * released.  
 */
void *wakeme_uncancellable_test(int arg1, void *arg2) {
    wake_me_len++;
    sched_sleep_on(&wake_me_q);
    wake_me_len--;
    do_exit(arg1);
    return NULL;
}


/*
 * A thread function that waits on wake_me_q and exist when released.  If it is
 * not cancelled, it prints an error message.
 */
void *cancelme_test(int arg1, void *arg2) {
    wake_me_len++;
    if (sched_cancellable_sleep_on(&wake_me_q) != -EINTR ) {
	dbg_print("Wakeme returned?! pid (%d)\n", curproc->p_pid);
	wake_me_len--;
	do_exit(-1);
    }
    wake_me_len--;
    do_exit(arg1);
    return NULL;
}

/*
 * A Thread function that exhibits a race condition on the race global.  It
 * loads increments and stores race, context switching between each line of C.
 */
void *racer_test(int arg1, void *arg2) {
    int local;

    sched_switch();
    local = race;
    sched_switch();
    local++;
    sched_switch();
    race = local;
    sched_switch();
    do_exit(race);
    return NULL;
}

/*
 * A Thread function that exhibits a race condition on the race global being
 * removed by a mutex.  It loads increments and stores race, context switching
 * between each line of C after acquiring mutex.  The mutex acquire cannot
 * be cancelled.
 */
void *mutex_uncancellable_test(int arg1, void *arg2) {
    int local;

    kmutex_lock(&mutex); 
    sched_switch();
    local = race;
    sched_switch();
    local++;
    sched_switch();
    race = local;
    sched_switch();
    kmutex_unlock(&mutex);
    do_exit(race);
    return NULL;
}

/*
 * A Thread function that exhibits a race condition on the race global being
 * removed by a mutex.  It loads increments and stores race, context switching
 * between each line of C after acquiring mutex.  The mutex acquire can
 * be cancelled, but will print an error message if this happens.
 */
void *mutex_test(int arg1, void *arg2) {
    int local;

    if ( kmutex_lock_cancellable(&mutex) ) {
	dbg_print("Mutex cancelled? %d", curproc->p_pid);
	do_exit(-1);
    }
    sched_switch();
    local = race;
    sched_switch();
    local++;
    sched_switch();
    race = local;
    sched_switch();
    kmutex_unlock(&mutex);
    do_exit(race);
    return NULL;
}

/*
 * A Thread function that exhibits a race condition on the race global being
 * removed by a mutex.  It loads increments and stores race, context switching
 * between each line of C after acquiring mutex.  The mutex acquire can
 * be cancelled, but will print an error message if the mutex acquire succeeds
 * - it expects to be cancelled.
 */
void *mutex_test_cancelme(int arg1, void *arg2) {
    int local;

    if ( kmutex_lock_cancellable(&mutex) ) 
	do_exit(0);
    dbg_print("Mutex not cancelled? %d", curproc->p_pid);
    sched_switch();
    local = race;
    sched_switch();
    local++;
    sched_switch();
    race = local;
    sched_switch();
    kmutex_unlock(&mutex);
    do_exit(race);
    return NULL;
}

/*
 * A thread function to test reparenting.  Start a child wakeme_test process,
 * and if arg1 is > 1, create a child process that will do the same (with arg1
 * decrementd.  Calling this with arg1 results in 2 * arg1 processes, half of
 * them waiting on wake_me_q.  None of them wait, so as they exit, they should
 * all become children of init.
 */
void *reparent_test(int arg1, void *arg2) {
    start_proc("reparented" , wakeme_test, arg1);
    if ( arg1 > 1 ) 
	start_proc("reparent ", reparent_test, arg1-1);
    do_exit(0);
    return NULL;
}

/* The core testproc code */
void *testproc(int arg1, void *arg2) {
    proc_thread_t pt;
    pid_t pid = -1;
    int rv = 0;
    int i = 0;

#if CS402TESTS > 0
    dbg_print("waitpid any test");
    pt = start_proc("waitpid any test", waitpid_test, 23);
    wait_for_any();

    dbg_print("waitpid test");
    pt = start_proc("waitpid test", waitpid_test, 32);
    pid = do_waitpid(2323, 0, &rv);
    if ( pid != -ECHILD ) dbg_print("Allowed wait on non-existent pid\n");
    wait_for_proc(pt.p);

    dbg_print("kthread exit test");
    pt = start_proc("kthread exit test", kthread_exit_test, 0);
    wait_for_proc(pt.p);

    dbg_print("many test");
    for (i = 0; i < 10; i++) 
	start_proc("many test", waitpid_test, i);
    wait_for_all();
#endif

#if CS402TESTS > 1
    dbg_print("Context switch test");
    pt = start_proc("Context switch", racer_test, 0);
    wait_for_proc(pt.p);
#endif

#if CS402TESTS > 2
    sched_queue_init(&wake_me_q);

    dbg_print("wake me test");
    wake_me_len = 0;
    pt = start_proc("wake me test", wakeme_test, 0);
    /* Make sure p has blocked */
    stop_until_queued(1, &wake_me_len);
    sched_wakeup_on(&wake_me_q);
    wait_for_proc(pt.p);
    KASSERT(wake_me_len == 0 && "Error on wakeme bookkeeping");

    dbg_print("broadcast me test");
    for (i = 0; i < 10; i++ ) 
	start_proc("broadcast me test", wakeme_test, 0);
    stop_until_queued(10, &wake_me_len);
    /* Make sure the processes have blocked */
    sched_broadcast_on(&wake_me_q);
    wait_for_all();
    KASSERT(wake_me_len == 0 && "Error on wakeme bookkeeping");
#endif

#if CS402TESTS > 3
    dbg_print("wake me uncancellable test");
    pt = start_proc("wake me uncancellable test", 
	    wakeme_uncancellable_test, 0);
    /* Make sure p has blocked */
    stop_until_queued(1, &wake_me_len);
    sched_wakeup_on(&wake_me_q);
    wait_for_proc(pt.p);
    KASSERT(wake_me_len == 0 && "Error on wakeme bookkeeping");

    dbg_print("broadcast me uncancellable test");
    for (i = 0; i < 10; i++ ) 
	start_proc("broadcast me uncancellable test", 
		wakeme_uncancellable_test, 0);
    /* Make sure the processes have blocked */
    stop_until_queued(10, &wake_me_len);
    sched_broadcast_on(&wake_me_q);
    wait_for_all();
    KASSERT(wake_me_len == 0 && "Error on wakeme bookkeeping");
#endif

#if CS402TESTS > 4
    dbg_print("cancel me test");
    pt = start_proc("cancel me test", cancelme_test, 0);
    /* Make sure p has blocked */
    stop_until_queued(1, &wake_me_len);
    sched_cancel(pt.t);
    wait_for_proc(pt.p);
    KASSERT(wake_me_len == 0 && "Error on wakeme bookkeeping");

    dbg_print("prior cancel me test");
    pt = start_proc("prior cancel me test", cancelme_test, 0);
    /*  Cancel before sleep */
    sched_cancel(pt.t);
    wait_for_proc(pt.p);
    KASSERT(wake_me_len == 0 && "Error on wakeme bookkeeping");

    dbg_print("cancel me head test");
    pt = start_proc("cancel me head test", cancelme_test, 0);
    start_proc("cancel me head test", wakeme_test, 0);
    stop_until_queued(2, &wake_me_len);
    sched_cancel(pt.t);
    sched_wakeup_on(&wake_me_q);
    wait_for_all();
    KASSERT(wake_me_len == 0 && "Error on wakeme bookkeeping");
    
    dbg_print("cancel me tail test");
    start_proc("cancel me tail test", wakeme_test, 0);
    pt = start_proc("cancel me tail test", cancelme_test, 0);
    stop_until_queued(2, &wake_me_len);
    sched_cancel(pt.t);
    sched_wakeup_on(&wake_me_q);
    wait_for_all();
    KASSERT(wake_me_len == 0 && "Error on wakeme bookkeeping");
#endif

#if CS402TESTS > 5
    dbg_print("Reparenting test");
    start_proc("Reparenting test", reparent_test, 0);
    stop_until_queued(1, &wake_me_len);
    sched_wakeup_on(&wake_me_q);
    wait_for_all();
    dbg_print("Reparenting stress test");
    start_proc("Reparenting stress test", reparent_test, 10);
    stop_until_queued(10, &wake_me_len);
    sched_broadcast_on(&wake_me_q);
    wait_for_all();
    KASSERT(wake_me_len == 0 && "Error on wakeme bookkeeping");
#endif


#if CS402TESTS > 6
    kmutex_init(&mutex);

    dbg_print("show race test");
    race = 0;
    for (i = 0; i < 10; i++ ) 
	start_proc("show race test", racer_test, 0);
    wait_for_all();

    dbg_print("fix race test");
    race = 0;
    for (i = 0; i < 10; i++ ) 
	start_proc("fix race test", mutex_uncancellable_test, 0);
    wait_for_all();

    dbg_print("fix race test w/cancel");
    race = 0;
    for (i = 0; i < 10; i++ ) {
	if ( i % 2 == 0) { 
	    start_proc("fix race test w/cancel", mutex_test, 0);
	} else {
	    pt = start_proc("fix race test w/cancel", mutex_test_cancelme, 0);
	    sched_cancel(pt.t);
	}
    }
    wait_for_all();
#endif

#if CS402TESTS > 7
    dbg_print("kill all test");
    for ( i=0 ; i < 10; i++ )
	start_proc("kill all test", cancelme_test, 0);
    stop_until_queued(10, &wake_me_len);
    proc_kill_all();
    wait_for_all();
    KASSERT(wake_me_len == 0 && "Error on wakeme bookkeeping");
#endif
#if CS402TESTS > 8
    extern void *student_tests(int, void*);
    student_tests(arg1, arg2);
#endif
    dbg_print("All tests completed\n\n");
    return NULL;
}
