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

#include "mm/slab.h"
#include "mm/page.h"
#include "mm/mmobj.h"
#include "mm/mm.h"
#include "mm/mman.h"

#include "vm/vmmap.h"

#include "fs/vfs.h"
#include "fs/vfs_syscall.h"
#include "fs/vnode.h"
#include "fs/file.h"

proc_t *curproc = NULL; /* global */
static slab_allocator_t *proc_allocator = NULL;

static list_t _proc_list;
static proc_t *proc_initproc = NULL; /* Pointer to the init process (PID 1) */

void
proc_init()
{
        list_init(&_proc_list);
        proc_allocator = slab_allocator_create("proc", sizeof(proc_t));
        KASSERT(proc_allocator != NULL);
}

static pid_t next_pid = 0;

/**
 * Returns the next available PID.
 *
 * Note: Where n is the number of running processes, this algorithm is
 * worst case O(n^2). As long as PIDs never wrap around it is O(n).
 *
 * @return the next available PID
 */
static int
_proc_getid()
{
        proc_t *p;
        pid_t pid = next_pid;
        while (1) {
failed:
                list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                        if (p->p_pid == pid) {
                                if ((pid = (pid + 1) % PROC_MAX_COUNT) == next_pid) {
                                        return -1;
                                } else {
                                        goto failed;
                                }
                        }
                } list_iterate_end();
                next_pid = (pid + 1) % PROC_MAX_COUNT;
                return pid;
        }
}

/*
 * The new process, although it isn't really running since it has no
 * threads, should be in the PROC_RUNNING state.
 *
 * Don't forget to set proc_initproc when you create the init
 * process. You will need to be able to reference the init process
 * when reparenting processes to the init process.
 */
proc_t *
proc_create(char *name)
{
        /* PROCS {{{ */
        proc_t *p;

        int pid;
        if (0 > (pid = _proc_getid())) {
                dbg(DBG_PROC, ("Could not allocate PID for new process.\n"));
                return NULL;
        }
        KASSERT(PID_IDLE != pid || list_empty(&_proc_list));
        KASSERT(PID_INIT != pid || PID_IDLE == curproc->p_pid);

        if (NULL == (p = slab_obj_alloc(proc_allocator)))
                return NULL;

        p->p_pid = (pid_t) pid;

        list_init(&p->p_threads);
        list_init(&p->p_children);
        p->p_pproc = curproc;

        p->p_status = 0;
        p->p_state = PROC_RUNNING;
        sched_queue_init(&p->p_wait);

        if (NULL == (p->p_pagedir = pt_create_pagedir())) {
                slab_obj_free(proc_allocator, p);
                return NULL;
        }
#ifdef __VM__
        if (NULL == (p->p_vmmap = vmmap_create())) {
                pt_destroy_pagedir(p->p_pagedir);
                slab_obj_free(proc_allocator, p);
                return NULL;
        }
#endif

        if (NULL != p->p_pproc) {
                list_insert_before(&p->p_pproc->p_children, &p->p_child_link);
        }

        list_insert_before(&_proc_list, &p->p_list_link);

        if (PID_INIT == p->p_pid) {
                KASSERT(NULL == proc_initproc);
                proc_initproc = p;
        }

#ifdef __VFS__
        if (curproc)
                p->p_cwd = curproc->p_cwd;
        else
                p->p_cwd = NULL;

        if (p->p_cwd)
                vref(p->p_cwd);

        memset(p->p_files, 0, NFILES * sizeof(file_t *));
#endif

        if (NULL == name) {
                name = "noname";
        }
        strncpy(p->p_comm, name, PROC_NAME_LEN);
        p->p_comm[PROC_NAME_LEN - 1] = '\0';

        dbg(DBG_PROC, "created %s process %u (0x%p)\n", p->p_comm, (uint32_t)p->p_pid, p);

        return p;
        /* PROCS }}} */
        return NULL;
}

/**
 * Cleans up as much as the process as can be done from within the
 * process. This involves:
 *    - Closing all open files (VFS)
 *    - Cleaning up VM mappings (VM)
 *    - Waking up its parent if it is waiting
 *    - Reparenting any children to the init process
 *    - Setting its status and state appropriately
 *
 * The parent will finish destroying the process within do_waitpid (make
 * sure you understand why it cannot be done here). Until the parent
 * finishes destroying it, the process is informally called a 'zombie'
 * process.
 *
 * This is also where any children of the current process should be
 * reparented to the init process (unless, of course, the current
 * process is the init process. However, the init process should not
 * have any children at the time it exits).
 *
 * Note: You do _NOT_ have to special case the idle process. It should
 * never exit this way.
 *
 * @param status the status to exit the process with
 */
void
proc_cleanup(int status)
{
        /* PROCS {{{ */
        proc_t *child;

        KASSERT(NULL != proc_initproc);
        KASSERT(1 <= curproc->p_pid);
        KASSERT(NULL != curproc->p_pproc);

#ifdef __VM__
        vmmap_destroy(curproc->p_vmmap);
#endif

#ifdef __VFS__
        int i;
        for (i = 0; i < NFILES; ++i) {
                do_close(i);
        }
#endif

#ifdef __VFS__
        if (curproc->p_cwd)
                vput(curproc->p_cwd);
#endif

        if ((!list_empty(&curproc->p_children)) && (1 != curproc->p_pid)) {
                /* a process (other than init) is exiting and has children--
                 * reparent them to init */
                KASSERT(2 < PROC_MAX_COUNT && "hey, ya never know...");

                list_iterate_begin(&curproc->p_children,
                                   child, proc_t, p_child_link) {
                        KASSERT(child->p_pproc == curproc);
                        child->p_pproc = proc_initproc;
                        list_remove(&child->p_child_link);
                        list_insert_before(&proc_initproc->p_children,
                                           &child->p_child_link);
                } list_iterate_end();

        } else if ((curproc->p_pid == 1) &&
                   (!list_empty(&curproc->p_children))) {
                panic("caught init exiting with children!");
        }

        curproc->p_state = PROC_DEAD;
        curproc->p_status = status;

        KASSERT(curproc->p_pproc);
        sched_broadcast_on(&curproc->p_pproc->p_wait);
        /* PROCS }}} */
}

/*
 * This has nothing to do with signals and kill(1).
 *
 * Calling this on the current process is equivalent to calling
 * do_exit().
 *
 * In Weenix, this is only called from proc_kill_all.
 */
void
proc_kill(proc_t *p, int status)
{
        /* PROCS {{{ */
        kthread_t *thr;
        if (curproc ==  p)
                do_exit(status);
        /* If not, just cancel all threads */
        list_iterate_begin(&p->p_threads, thr, kthread_t, kt_plink) {
                kthread_cancel(thr, (void *) status);
        } list_iterate_end();
        /* PROCS }}} */
}

/*
 * Remember, proc_kill on the current process will _NOT_ return.
 * Don't kill direct children of the idle process.
 *
 * In Weenix, this is only called by sys_halt.
 */
void
proc_kill_all()
{
        /* PROCS {{{ */
        proc_t *p;
        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                if (p != curproc && p->p_pid != PID_IDLE && p->p_pproc->p_pid != PID_IDLE)
                        proc_kill(p, -1);
        } list_iterate_end();
        /* Kill self last */
        do_exit(0);
        /* PROCS }}} */
}

proc_t *
proc_lookup(int pid)
{
        proc_t *p;
        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                if (p->p_pid == pid) {
                        return p;
                }
        } list_iterate_end();
        return NULL;
}

list_t *
proc_list()
{
        return &_proc_list;
}

/*
 * This function is only called from kthread_exit.
 *
 * Unless you are implementing MTP, this just means that the process
 * needs to be cleaned up and a new thread needs to be scheduled to
 * run. If you are implementing MTP, a single thread exiting does not
 * necessarily mean that the process should be exited.
 */
void
proc_thread_exited(void *retval)
{
        /* PROCS {{{ */
#ifdef __MTP__
        kthread_t *kthr;

        /* Check for other living threads of the current proc */
        list_iterate_begin(&curproc->p_threads, kthr, kthread_t, kt_plink) {
                if (!(kthr->kt_state == KT_EXITED))
                        goto switchout;
        } list_iterate_end();
#endif

        /* If we're here, we are the last running thread, so need to
         * cleanup proc */
        proc_cleanup((int) retval);

#ifdef __MTP__
switchout:
#endif
        sched_switch();

        panic("switch returned to dead thread!\n");
        /* PROCS }}} */
}

/* If pid is -1 dispose of one of the exited children of the current
 * process and return its exit status in the status argument, or if
 * all children of this process are still running, then this function
 * blocks on its own p_wait queue until one exits.
 *
 * If pid is greater than 0 and the given pid is a child of the
 * current process then wait for the given pid to exit and dispose
 * of it.
 *
 * If the current process has no children, or the given pid is not
 * a child of the current process return -ECHILD.
 *
 * Pids other than -1 and positive numbers are not supported.
 * Options other than 0 are not supported.
 */
pid_t
do_waitpid(pid_t pid, int options, int *status)
{
        /* PROCS {{{ */
        proc_t *p = NULL;

        if (0 == pid || -1 > pid)
                return -ENOTSUP;
        if (0 != options)
                return -ENOTSUP;

        if (-1 == pid) {
                while (1) {
                        if (list_empty(&curproc->p_children))
                                return -ECHILD;

                        list_iterate_begin(&curproc->p_children, p, proc_t, p_child_link) {
                                if (p->p_state == PROC_DEAD) {
                                        goto deadchild;
                                }
                        } list_iterate_end();

                        /* we have at least one child, but none that are dead. */
                        /* sleep, to be awoken when a child proc_exit's */
                        sched_sleep_on(&curproc->p_wait);
                }
        } else {
                while (1) {
                        int found = 0;
                        list_iterate_begin(&curproc->p_children, p, proc_t, p_child_link) {
                                if (p->p_pid == pid) {
                                        found = 1;
                                        if (p->p_state == PROC_DEAD) {
                                                goto deadchild;
                                        }
                                }
                        } list_iterate_end();

                        if (!found)
                                return -ECHILD;

                        sched_sleep_on(&curproc->p_wait);
                }
        }

        /* This flow control was made possible by you, our listeners, and by
         * list_iterate_begin, bringing you terrible macro hacks since 1997 */

deadchild:

        KASSERT(p);
        KASSERT(-1 == pid || p->p_pid == pid);

        kthread_t *thr;
        list_iterate_begin(&p->p_threads, thr, kthread_t, kt_plink) {
                KASSERT(KT_EXITED == thr->kt_state);
                kthread_destroy(thr);
        } list_iterate_end();

        dbg(DBG_THR, "thread %p of proc %d cleaning up proc %d\n",
            curthr, curproc->p_pid, p->p_pid);

        /* set status */
        if (status) {
                *status = p->p_status;
        }
        pid = p->p_pid;

        KASSERT(NULL != p->p_pagedir);
        pt_destroy_pagedir(p->p_pagedir);

        /* free proc */
        list_remove(&p->p_child_link);
        list_remove(&p->p_list_link);
        slab_obj_free(proc_allocator, p);

        return pid;
        /* PROCS }}} */
        return 0;
}

/*
 * Cancel all threads, join with them, and exit from the current
 * thread.
 *
 * @param status the exit status of the process
 */
void
do_exit(int status)
{
        /* PROCS {{{ */
#ifdef __MTP__
        kthread_t *thr;
        list_iterate_begin(&curproc->p_threads, thr, kthread_t, kt_plink) {
                if (thr != curthr) /* Make sure current thread is
                                    * cancelled last */
                        kthread_cancel(thr, NULL);
        } list_iterate_end();

        list_iterate_begin(&curproc->p_threads, thr, kthread_t, kt_plink) {
                kthread_join(thr, NULL); /* Some of these will fail,
                                          * but don't care */
        } list_iterate_end();
#endif

        kthread_exit((void *) status);
        /* PROCS }}} */
}

size_t
proc_info(const void *arg, char *buf, size_t osize)
{
        const proc_t *p = (proc_t *) arg;
        size_t size = osize;
        proc_t *child;

        KASSERT(NULL != p);
        KASSERT(NULL != buf);

        iprintf(&buf, &size, "pid:          %i\n", p->p_pid);
        iprintf(&buf, &size, "name:         %s\n", p->p_comm);
        if (NULL != p->p_pproc) {
                iprintf(&buf, &size, "parent:       %i (%s)\n",
                        p->p_pproc->p_pid, p->p_pproc->p_comm);
        } else {
                iprintf(&buf, &size, "parent:       -\n");
        }

#ifdef __MTP__
        int count = 0;
        kthread_t *kthr;
        list_iterate_begin(&p->p_threads, kthr, kthread_t, kt_plink) {
                ++count;
        } list_iterate_end();
        iprintf(&buf, &size, "thread count: %i\n", count);
#endif

        if (list_empty(&p->p_children)) {
                iprintf(&buf, &size, "children:     -\n");
        } else {
                iprintf(&buf, &size, "children:\n");
        }
        list_iterate_begin(&p->p_children, child, proc_t, p_child_link) {
                iprintf(&buf, &size, "     %i (%s)\n", child->p_pid, child->p_comm);
        } list_iterate_end();

        iprintf(&buf, &size, "status:       %i\n", p->p_status);
        iprintf(&buf, &size, "state:        %i\n", p->p_state);

#ifdef __VFS__
#ifdef __GETCWD__
        if (NULL != p->p_cwd) {
                char cwd[256];
                lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                iprintf(&buf, &size, "cwd:          %-s\n", cwd);
        } else {
                iprintf(&buf, &size, "cwd:          -\n");
        }
#endif /* __GETCWD__ */
#endif

#ifdef __VM__
        iprintf(&buf, &size, "start brk:    0x%p\n", p->p_start_brk);
        iprintf(&buf, &size, "brk:          0x%p\n", p->p_brk);
#endif

        return size;
}

size_t
proc_list_info(const void *arg, char *buf, size_t osize)
{
        size_t size = osize;
        proc_t *p;

        KASSERT(NULL == arg);
        KASSERT(NULL != buf);

#if defined(__VFS__) && defined(__GETCWD__)
        iprintf(&buf, &size, "%5s %-13s %-18s %-s\n", "PID", "NAME", "PARENT", "CWD");
#else
        iprintf(&buf, &size, "%5s %-13s %-s\n", "PID", "NAME", "PARENT");
#endif

        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                char parent[64];
                if (NULL != p->p_pproc) {
                        snprintf(parent, sizeof(parent),
                                 "%3i (%s)", p->p_pproc->p_pid, p->p_pproc->p_comm);
                } else {
                        snprintf(parent, sizeof(parent), "  -");
                }

#if defined(__VFS__) && defined(__GETCWD__)
                if (NULL != p->p_cwd) {
                        char cwd[256];
                        lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                        iprintf(&buf, &size, " %3i  %-13s %-18s %-s\n",
                                p->p_pid, p->p_comm, parent, cwd);
                } else {
                        iprintf(&buf, &size, " %3i  %-13s %-18s -\n",
                                p->p_pid, p->p_comm, parent);
                }
#else
                iprintf(&buf, &size, " %3i  %-13s %-s\n",
                        p->p_pid, p->p_comm, parent);
#endif
        } list_iterate_end();
        return size;
}
