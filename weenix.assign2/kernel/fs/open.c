/*
 *  FILE: open.c
 *  AUTH: mcc | jal
 *  DESC:
 *  DATE: Mon Apr  6 19:27:49 1998
 */

#include "globals.h"
#include "errno.h"
#include "fs/fcntl.h"
#include "util/string.h"
#include "util/printf.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "fs/file.h"
#include "fs/vfs_syscall.h"
#include "fs/open.h"
#include "fs/stat.h"
#include "util/debug.h"

/* find empty index in p->p_files[] */
int
get_empty_fd(proc_t *p)
{
        int fd;

        for (fd = 0; fd < NFILES; fd++) {
                if (!p->p_files[fd])
                        return fd;
        }

        dbg(DBG_ERROR | DBG_VFS, "ERROR: get_empty_fd: out of file descriptors "
            "for pid %d\n", curproc->p_pid);
        return -EMFILE;
}

/*
 * There a number of steps to opening a file:
 *      1. Get the next empty file descriptor.
 *      2. Call fget to get a fresh file_t.
 *      3. Save the file_t in curproc's file descriptor table.
 *      4. Set file_t->f_mode to OR of FMODE_(READ|WRITE|APPEND) based on
 *         oflags, which can be O_RDONLY, O_WRONLY or O_RDWR, possibly OR'd with
 *         O_APPEND.
 *      5. Use open_namev() to get the vnode for the file_t.
 *      6. Fill in the fields of the file_t.
 *      7. Return new fd.
 *
 * If anything goes wrong at any point (specifically if the call to open_namev
 * fails), be sure to remove the fd from curproc, fput the file_t and return an
 * error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EINVAL
 *        oflags is not valid.
 *      o EMFILE
 *        The process already has the maximum number of files open.
 *      o ENOMEM
 *        Insufficient kernel memory was available.
 *      o ENAMETOOLONG
 *        A component of filename was too long.
 *      o ENOENT
 *        O_CREAT is not set and the named file does not exist.  Or, a
 *        directory component in pathname does not exist.
 *      o EISDIR
 *        pathname refers to a directory and the access requested involved
 *        writing (that is, O_WRONLY or O_RDWR is set).
 *      o ENXIO
 *        pathname refers to a device special file and no corresponding device
 *        exists.
 */
/* VFS BLANK {{{ */
static void
nukefd(int fd)
{
        fput(curproc->p_files[fd]);
        curproc->p_files[fd] = 0;
}
/* VFS BLANK }}} */

int
do_open(const char *filename, int oflags)
{
        /* VFS {{{ */
        int fd;
        file_t *f;
        vnode_t *file_vnode;
        int ret;

        if ((oflags & O_WRONLY) && (oflags & O_RDWR)) {
                return -EINVAL;
        }

        if ((fd = get_empty_fd(curproc)) < 0) {
                return fd;
        }

        if ((f = fget(-1)) == NULL) {
                return -ENOMEM;
        }

        curproc->p_files[fd] = f;

        dbg(DBG_VFS, "open on %s, file at %p\n", filename, curproc->p_files[fd]);

        switch (oflags & 0x3) {
                case O_RDONLY:
                        f->f_mode = FMODE_READ;
                        break;
                case O_WRONLY:
                        f->f_mode = FMODE_WRITE;
                        break;
                case O_RDWR:
                        f->f_mode = FMODE_READ | FMODE_WRITE;
                        break;
                default:
                        /*kthread_cleanup_pop(1);*/
                        nukefd(fd);
                        return -EINVAL;
        }

        if ((ret = open_namev(filename, oflags, &file_vnode, NULL)) < 0) {
                /*kthread_cleanup_pop(1);*/
                nukefd(fd);
                return ret;
        }

        if (((oflags & O_WRONLY) || (oflags & O_RDWR)) && S_ISDIR(file_vnode->vn_mode)) {
                /*kthread_cleanup_pop(1);*/
                nukefd(fd);
                vput(file_vnode);
                return -EISDIR;
        }

        if (S_ISBLK(file_vnode->vn_mode) && (!file_vnode->vn_bdev)) {
                /*kthread_cleanup_pop(1);*/
                nukefd(fd);
                vput(file_vnode);
                return -ENXIO;
        }

        if (S_ISCHR(file_vnode->vn_mode) && (!file_vnode->vn_cdev)) {
                /*kthread_cleanup_pop(1);*/
                nukefd(fd);
                vput(file_vnode);
                return -ENXIO;
        }

        f->f_vnode = file_vnode;
        dbg(DBG_VFS, " vnode at %p\n", file_vnode);
        if (oflags & O_APPEND) {
                f->f_mode |= FMODE_APPEND;
        }

        /*kthread_cleanup_pop(0);*/
        return fd;
        /* VFS }}} */
        return -1;
}
