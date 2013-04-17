#include "drivers/tty/n_tty.h"

#include "errno.h"

#include "drivers/tty/driver.h"
#include "drivers/tty/ldisc.h"
#include "drivers/tty/tty.h"

#include "mm/kmalloc.h"

#include "proc/kthread.h"

#include "util/debug.h"

/* helpful macros */
#define EOFC            '\x4'
#define TTY_BUF_SIZE    128
#define ldisc_to_ntty(ldisc) \
        CONTAINER_OF(ldisc, n_tty_t, ntty_ldisc)

static void n_tty_attach(tty_ldisc_t *ldisc, tty_device_t *tty);
static void n_tty_detach(tty_ldisc_t *ldisc, tty_device_t *tty);
static int n_tty_read(tty_ldisc_t *ldisc, void *buf, int len);
static const char *n_tty_receive_char(tty_ldisc_t *ldisc, char c);
static const char *n_tty_process_char(tty_ldisc_t *ldisc, char c);

static tty_ldisc_ops_t n_tty_ops = {
        .attach       = n_tty_attach,
        .detach       = n_tty_detach,
        .read         = n_tty_read,
        .receive_char = n_tty_receive_char,
        .process_char = n_tty_process_char
};

struct n_tty {
        kmutex_t            ntty_rlock;
        ktqueue_t           ntty_rwaitq;
        char               *ntty_inbuf;
        int                 ntty_rhead;
        int                 ntty_rawtail;
        int                 ntty_ckdtail;

        tty_ldisc_t         ntty_ldisc;
};

/* DRIVERS BLANK {{{ */

#define dprintf(x, args...) dbg(DBG_TERM, x, ## args)

/* ASCII codes */
#define BS              0x08
#define DEL             0x7F
#define ESC             0x1B
#define LF              0x0A
#define CR              0x0D
#define SPACE           0x20
#define EOT             0x04

/* Raw input buffer functions */
static int n_tty_rawbuf_full(n_tty_t *ntty);
static int n_tty_rawbuf_almost_full(n_tty_t *ntty);
static int n_tty_rawbuf_empty(n_tty_t *ntty);
static int n_tty_rawbuf_size(n_tty_t *ntty);
static void n_tty_rawbuf_enqueue(n_tty_t *ntty, char c);
static void n_tty_rawbuf_remove_last(n_tty_t *ntty);
static void n_tty_rawbuf_cook(n_tty_t *ntty);

/* Cooked input buffer functions */
static int n_tty_ckdbuf_empty(n_tty_t *ntty);
static int n_tty_ckdbuf_size(n_tty_t *ntty);
static char n_tty_ckdbuf_dequeue(n_tty_t *ntty);

#define N_TTY_ASSERT_VALID(ntty)                                        \
        do {                                                            \
                KASSERT(NULL != (ntty));                                \
                KASSERT((ntty)->ntty_ldisc.ld_ops == &n_tty_ops);       \
                KASSERT(NULL != (ntty)->ntty_inbuf);                    \
                KASSERT((ntty)->ntty_rhead >= 0 &&                      \
                        (ntty)->ntty_rhead < TTY_BUF_SIZE);             \
                KASSERT((ntty)->ntty_rawtail >= 0 &&                    \
                        (ntty)->ntty_rawtail < TTY_BUF_SIZE);           \
                KASSERT((ntty)->ntty_ckdtail >= 0 &&                    \
                        (ntty)->ntty_ckdtail < TTY_BUF_SIZE);           \
        } while (0)

inline int
n_tty_rawbuf_full(n_tty_t *ntty)
{
        return ntty->ntty_rhead ==
               ((ntty->ntty_rawtail + 1) % TTY_BUF_SIZE);
}

inline int
n_tty_rawbuf_almost_full(n_tty_t *ntty)
{
        return ntty->ntty_rhead ==
               ((ntty->ntty_rawtail + 2) % TTY_BUF_SIZE);
}

inline int
n_tty_rawbuf_empty(n_tty_t *ntty)
{
        return ntty->ntty_rawtail == ntty->ntty_ckdtail;
}

inline int
n_tty_rawbuf_size(n_tty_t *ntty)
{
        return (ntty->ntty_rawtail + TTY_BUF_SIZE -
                ntty->ntty_ckdtail) % TTY_BUF_SIZE;
}

inline void
n_tty_rawbuf_enqueue(n_tty_t *ntty, char c)
{
        KASSERT(!n_tty_rawbuf_full(ntty));
        KASSERT(ntty->ntty_rawtail >= 0 && ntty->ntty_rawtail < TTY_BUF_SIZE);
        ntty->ntty_inbuf[ntty->ntty_rawtail] = c;
        ntty->ntty_rawtail = (ntty->ntty_rawtail + 1) % TTY_BUF_SIZE;
}

inline void
n_tty_rawbuf_remove_last(n_tty_t *ntty)
{
        KASSERT(!n_tty_rawbuf_empty(ntty));
        ntty->ntty_rawtail =
                (ntty->ntty_rawtail - 1 + TTY_BUF_SIZE) % TTY_BUF_SIZE;
}

inline void
n_tty_rawbuf_cook(n_tty_t *ntty)
{
        ntty->ntty_ckdtail = ntty->ntty_rawtail;
}

inline int
n_tty_ckdbuf_empty(n_tty_t *ntty)
{
        return ntty->ntty_ckdtail == ntty->ntty_rhead;
}

inline int
n_tty_ckdbuf_size(n_tty_t *ntty)
{
        return (ntty->ntty_ckdtail + TTY_BUF_SIZE -
                ntty->ntty_rhead) % TTY_BUF_SIZE;
}

inline char
n_tty_ckdbuf_dequeue(n_tty_t *ntty)
{
        char c = ntty->ntty_inbuf[ntty->ntty_rhead];
        ntty->ntty_rhead = (ntty->ntty_rhead + 1) % TTY_BUF_SIZE;
        return c;
}
/* DRIVERS BLANK }}} */

tty_ldisc_t *
n_tty_create()
{
        n_tty_t *ntty = (n_tty_t *)kmalloc(sizeof(n_tty_t));
        if (NULL == ntty) return NULL;
        ntty->ntty_ldisc.ld_ops = &n_tty_ops;
        return &ntty->ntty_ldisc;
}

void
n_tty_destroy(tty_ldisc_t *ldisc)
{
        KASSERT(NULL != ldisc);
        kfree(ldisc_to_ntty(ldisc));
}

/*
 * Initialize the fields of the n_tty_t struct, allocate any memory
 * you will need later, and set the tty_ldisc field of the tty.
 */
void
n_tty_attach(tty_ldisc_t *ldisc, tty_device_t *tty)
{
        /* DRIVERS {{{ */
        n_tty_t *ntty;

        KASSERT(NULL != ldisc);
        KASSERT(NULL != tty);
        KASSERT(NULL == tty->tty_ldisc);

        ntty = ldisc_to_ntty(ldisc);

        dprintf("Attaching n_tty line discipline to tty %u\n", tty->tty_id);

        kmutex_init(&ntty->ntty_rlock);
        sched_queue_init(&ntty->ntty_rwaitq);
        ntty->ntty_inbuf = (char *)kmalloc(sizeof(char) * TTY_BUF_SIZE);
        if (NULL == ntty->ntty_inbuf)
                panic("Not enough memory for n_tty input buffer\n");
        KASSERT(NULL != ntty->ntty_inbuf);

        ntty->ntty_rhead = 0;
        ntty->ntty_rawtail = 0;
        ntty->ntty_ckdtail = 0;

        tty->tty_ldisc = ldisc;

        KASSERT(n_tty_rawbuf_empty(ntty));
        KASSERT(!n_tty_rawbuf_full(ntty));
        KASSERT(n_tty_rawbuf_size(ntty) == 0);
        KASSERT(n_tty_ckdbuf_empty(ntty));
        KASSERT(n_tty_ckdbuf_size(ntty) == 0);
        /* DRIVERS }}} */
}

/*
 * Free any memory allocated in n_tty_attach and set the tty_ldisc
 * field of the tty.
 */
void
n_tty_detach(tty_ldisc_t *ldisc, tty_device_t *tty)
{
        /* DRIVERS {{{ */
        n_tty_t *ntty;

        KASSERT(NULL != ldisc);
        ntty = ldisc_to_ntty(ldisc);

        KASSERT(NULL != tty);
        KASSERT(tty->tty_ldisc == ldisc);
        tty->tty_ldisc = NULL;

        kfree(ntty->ntty_inbuf);
        /* DRIVERS}}} */
}

/*
 * Read a maximum of len bytes from the line discipline into buf. If
 * the buffer is empty, sleep until some characters appear. This might
 * be a long wait, so it's best to let the thread be cancellable.
 *
 * Then, read from the head of the buffer up to the tail, stopping at
 * len bytes or a newline character, and leaving the buffer partially
 * full if necessary. Return the number of bytes you read into the
 * buf.

 * In this function, you will be accessing the input buffer, which
 * could be modified by other threads. Make sure to make the
 * appropriate calls to ensure that no one else will modify the input
 * buffer when we are not expecting it.
 *
 * Remember to handle newline characters and CTRL-D, or ASCII 0x04,
 * properly.
 */
int
n_tty_read(tty_ldisc_t *ldisc, void *buf, int len)
{
        /* DRIVERS {{{ */
        int i;
        char *cbuf = (char *)buf;
        n_tty_t *ntty;

        KASSERT(TTY_BUF_SIZE < PAGE_SIZE);
        KASSERT(NULL != ldisc);

        ntty = ldisc_to_ntty(ldisc);

        N_TTY_ASSERT_VALID(ntty);
        KASSERT(NULL != buf);
        KASSERT(len >= 0);

        kmutex_lock(&ntty->ntty_rlock);

        if (n_tty_ckdbuf_empty(ntty)) {
                dprintf("Cooked buffer is empty. Sleeping\n");

                if (sched_cancellable_sleep_on(&ntty->ntty_rwaitq) == -EINTR) {
                        dprintf("Sleep cancelled. Returning -EINTR\n");
                        kmutex_unlock(&ntty->ntty_rlock);
                        return -EINTR;
                }
                dprintf("Woken up from sleep\n");
        }

        for (i = 0; i < len && !n_tty_ckdbuf_empty(ntty); ++i) {
                cbuf[i] = n_tty_ckdbuf_dequeue(ntty);
                if (cbuf[i] == LF) {
                        ++i;
                        break;
                } else if (cbuf[i] == EOT) {
                        break;
                }
        }

        kmutex_unlock(&ntty->ntty_rlock);

        return i;
        /* DRIVERS }}} */
        return 0;
}

/*
 * The tty subsystem calls this when the tty driver has received a
 * character. Now, the line discipline needs to store it in its read
 * buffer and move the read tail forward.
 *
 * Special cases to watch out for: backspaces (both ASCII characters
 * 0x08 and 0x7F should be treated as backspaces), newlines ('\r' or
 * '\n'), and full buffers.
 *
 * Return a null terminated string containing the characters which
 * need to be echoed to the screen. For a normal, printable character,
 * just the character to be echoed.
 */
const char *
n_tty_receive_char(tty_ldisc_t *ldisc, char c)
{
        /* DRIVERS{{{ */
        static const char echo_newline[] = {CR, LF, '\0'};
        static const char echo_bs[]      = {BS, SPACE, BS, '\0'};
        static const char echo_esc[]     = { '^', '\0' };
        static const char echo_null[]    = { '\0' };
        static char echo_char[]          = { ' ', '\0' };

        n_tty_t *ntty;

        KASSERT(NULL != ldisc);

        ntty = ldisc_to_ntty(ldisc);
        N_TTY_ASSERT_VALID(ntty);

        switch (c) {
                case BS:
                case DEL:
                        dprintf("Received backspace\n");
                        if (!n_tty_rawbuf_empty(ntty)) {
                                n_tty_rawbuf_remove_last(ntty);
                                return echo_bs;
                        }
                        return echo_null;
                case ESC:
                        dprintf("Received escape\n");
                        if (n_tty_rawbuf_almost_full(ntty)) return echo_null;
                        n_tty_rawbuf_enqueue(ntty, c);
                        return echo_esc;
                case CR:
                        c = LF;
                case LF:
                        dprintf("Received newline\n");
                        if (n_tty_rawbuf_full(ntty)) return echo_null;
                        n_tty_rawbuf_enqueue(ntty, c);
                        n_tty_rawbuf_cook(ntty);
                        if (!sched_queue_empty(&ntty->ntty_rwaitq)) {
                                kthread_t *thr = sched_wakeup_on(&ntty->ntty_rwaitq);
                                KASSERT(NULL != thr);
                        }
                        return echo_newline;
                case EOT:
                        dprintf("Received EOT\n");
                        n_tty_rawbuf_cook(ntty);
                        if (!sched_queue_empty(&ntty->ntty_rwaitq)) {
                                kthread_t *thr = sched_wakeup_on(&ntty->ntty_rwaitq);
                                KASSERT(NULL != thr);
                        }
                        return echo_null;
                default:
                        dprintf("Receiving printable character\n");
                        if (n_tty_rawbuf_almost_full(ntty)) return echo_null;
                        n_tty_rawbuf_enqueue(ntty, c);
                        echo_char[0] = c;
                        return echo_char;
        }

        panic("Should never get here\n");
        /* DRIVERS }}} */
        return NULL;
}

/*
 * Process a character to be written to the screen.
 *
 * The only special case is '\r' and '\n'.
 */
const char *
n_tty_process_char(tty_ldisc_t *ldisc, char c)
{
        /* DRIVERS {{{ */
        static const char echo_newline[] = { CR, LF, '\0' };
        static char echo_char[]          = { ' ', '\0' };
        KASSERT(NULL != ldisc);

        if (c == CR || c == LF) {
                return echo_newline;
        } else {
                echo_char[0] = c;
                return echo_char;
        }
        /* DRIVERS }}} */

        return NULL;
}
