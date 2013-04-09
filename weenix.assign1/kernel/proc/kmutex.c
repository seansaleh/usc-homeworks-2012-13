#include "globals.h"
#include "errno.h"

#include "util/debug.h"

#include "proc/kthread.h"
#include "proc/kmutex.h"

/*
 * IMPORTANT: Mutexes can _NEVER_ be locked or unlocked from an
 * interrupt context. Mutexes are _ONLY_ lock or unlocked from a
 * thread context.
 */

void
kmutex_init(kmutex_t *mtx)
{
	sched_queue_init(&mtx->km_waitq);
	mtx->km_holder = NULL;
}

/*
 * This should block the current thread (by sleeping on the mutex's
 * wait queue) if the mutex is already taken.
 *
 * No thread should ever try to lock a mutex it already has locked.
 */
void
kmutex_lock(kmutex_t *mtx)
{
	if (mtx->km_holder == NULL)
		mtx->km_holder = curthr;
	else {
		sched_sleep_on(&mtx->km_waitq);
	}
}

/*
 * This should do the same as kmutex_lock, but use a cancellable sleep
 * instead.
 */
int
kmutex_lock_cancellable(kmutex_t *mtx)
{
	int ret = 0;
	if (mtx->km_holder == NULL)
		mtx->km_holder = curthr;
	else {
		ret = sched_cancellable_sleep_on(&mtx->km_waitq);
	}        return ret;
}

/*
 * If there are any threads waiting to take a lock on the mutex, one
 * should be woken up and given the lock.
 *
 * Note: This should _NOT_ be a blocking operation!
 *
 * Note: Don't forget to add the new owner of the mutex back to the
 * run queue.
 *
 * Note: Make sure that the thread on the head of the mutex's wait
 * queue becomes the new owner of the mutex.
 *
 * @param mtx the mutex to unlock
 */
void
kmutex_unlock(kmutex_t *mtx)
{
	if (!sched_queue_empty(&mtx->km_waitq)) {
		kthread_t *t = sched_wakeup_on(&mtx->km_waitq);
		mtx->km_holder = t;	
		}
	else
		mtx->km_holder = NULL;
}
