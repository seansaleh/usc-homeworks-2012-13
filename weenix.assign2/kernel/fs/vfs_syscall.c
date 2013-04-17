/*
 *  FILE: vfs_syscall.c
 *  AUTH: mcc | jal
 *  DESC:
 *  DATE: Wed Apr  8 02:46:19 1998
 *  $Id: vfs_syscall.c,v 1.9.2.2 2006/06/04 01:02:32 afenn Exp $
 */

#include "kernel.h"
#include "errno.h"
#include "globals.h"
#include "fs/vfs.h"
#include "fs/file.h"
#include "fs/vnode.h"
#include "fs/vfs_syscall.h"
#include "fs/open.h"
#include "fs/fcntl.h"
#include "fs/lseek.h"
#include "mm/kmalloc.h"
#include "util/string.h"
#include "util/printf.h"
#include "fs/stat.h"
#include "util/debug.h"

/* To read a file:
 *      o fget(fd)
 *      o call its virtual read f_op
 *      o update f_pos
 *      o fput() it
 *      o return the number of bytes read, or an error
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd is not a valid file descriptor or is not open for reading.
 *      o EISDIR
 *        fd refers to a directory.
 *
 * In all cases, be sure you do not leak file refcounts by returning before
 * you fput() a file that you fget()'ed.
 */
int
do_read(int fd, void *buf, size_t nbytes)
{
        /* VFS {{{ */
        file_t *f = NULL;
        int ret = 0;

        if ((0 > fd) || (f = fget(fd)) == NULL) {
                ret = -EBADF;
                goto done;
        }

        if (NULL == f->f_vnode->vn_ops->read || !(f->f_mode & FMODE_READ)) {
                ret = -EBADF;
                goto done;
        }

        if (S_ISDIR(f->f_vnode->vn_mode)) {
                ret = -EISDIR;
                goto done;
        }

        /* We don't want f->f_pos to wrap around, so we make sure
           that nbytes will not be large enough to allow for this
           no matter how many bytes are successfully read */
        if ((f->f_pos + (int)nbytes) < 0) {
                nbytes = ((long long)1 << (sizeof(f->f_pos) * 8 - 1)) - 1 - f->f_pos;
        }

        if (0 < (ret = f->f_vnode->vn_ops->read(f->f_vnode, f->f_pos, buf, nbytes))) {
                f->f_pos += ret;
        }

done:
        if (likely(NULL != f)) {
                fput(f);
        }

        return ret;
        /* VFS }}} */
        return -1;
}

/* Very similar to do_read.  Check f_mode to be sure the file is writable.  If
 * f_mode & FMODE_APPEND, do_lseek() to the end of the file, call the write
 * f_op, and fput the file.  As always, be mindful of refcount leaks.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd is not a valid file descriptor or is not open for writing.
 */
int
do_write(int fd, const void *buf, size_t nbytes)
{
        /* VFS {{{ */
        file_t *f = NULL;
        int ret = 0;

        if ((0 > fd) || (f = fget(fd)) == NULL) {
                ret = -EBADF;
                goto done;
        }

        if (NULL == f->f_vnode->vn_ops->write || !(f->f_mode & FMODE_WRITE)) {
                ret = -EBADF;
                goto done;
        }

        if (f->f_mode & FMODE_APPEND) {
                f->f_pos = f->f_vnode->vn_len;
        }

        /* We don't want f->f_pos to wrap around, so we make sure
           that nbytes will not be large enough to allow for this
           no matter how many bytes are successfully written */
        if (0 > f->f_pos + (int)nbytes) {
                nbytes = ((long long)1 << (sizeof(f->f_pos) * 8 - 1)) - 1 - f->f_pos;
        }

        if (0 < (ret = f->f_vnode->vn_ops->write(f->f_vnode, f->f_pos, buf, nbytes))) {
                f->f_pos += ret;

                KASSERT((S_ISCHR(f->f_vnode->vn_mode) || (S_ISBLK(f->f_vnode->vn_mode))
                         || ((S_ISREG(f->f_vnode->vn_mode))
                             && (f->f_pos <= f->f_vnode->vn_len)))
                        && "fs 'write' entry point should have updated "
                        "vn_len if necessary");
        }

done:
        if (likely(NULL != f)) {
                fput(f);
        }

        return ret;
        /* VFS }}} */
        return -1;
}

/*
 * Zero curproc->p_files[fd], and fput() the file. Return 0 on success
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd isn't a valid open file descriptor.
 */
int
do_close(int fd)
{
        /* VFS {{{ */
        file_t *f;

        if ((0 > fd) || NULL == (f = fget(fd))) {
                return -EBADF;
        }

        fput(f);
        curproc->p_files[fd] = NULL;
        fput(f);

        return 0;
        /* VFS }}} */
        return -1;
}

/* To dup a file:
 *      o fget(fd) to up fd's refcount
 *      o get_empty_fd()
 *      o point the new fd to the same file_t* as the given fd
 *      o return the new file descriptor
 *
 * Don't fput() the fd unless something goes wrong.  Since we are creating
 * another reference to the file_t*, we want to up the refcount.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd isn't an open file descriptor.
 *      o EMFILE
 *        The process already has the maximum number of file descriptors open
 *        and tried to open a new one.
 */
int
do_dup(int fd)
{
        /* VFS {{{ */
        file_t *f = NULL;
        int ret = 0;

        if ((0 > fd) || NULL == (f = fget(fd))) {
                ret = -EBADF;
                goto done;
        }

        if (0 > (ret = get_empty_fd(curproc))) {
                goto done;
        }

        curproc->p_files[ret] = f;
        fref(f);

done:
        if (likely(NULL != f)) {
                fput(f);
        }

        return ret;
        /* VFS }}} */
        return -1;
}

/* Same as do_dup, but insted of using get_empty_fd() to get the new fd,
 * they give it to us in 'nfd'.  If nfd is in use (and not the same as ofd)
 * do_close() it first.  Then return the new file descriptor.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        ofd isn't an open file descriptor, or nfd is out of the allowed
 *        range for file descriptors.
 */
int
do_dup2(int ofd, int nfd)
{
        /* VFS {{{ */
        file_t *f = NULL;
        int ret = 0;

        if (0 > ofd || 0 > nfd || NFILES <= nfd || NULL == (f = fget(ofd))) {
                ret = -EBADF;
                goto done;
        }

        if (ofd == nfd) {
                ret = nfd;
                goto done;
        }

        /* Closes the file descriptor if open, but ignores the return
           code since we don't care if the file descriptor was not
           open. */
        do_close(nfd);

        curproc->p_files[nfd] = f;
        fref(f);
        ret = nfd;

done:
        if (likely(NULL != f)) {
                fput(f);
        }

        return ret;
        /* VFS }}} */
        return -1;
}

/*
 * This routine creates a special file of the type specified by 'mode' at
 * the location specified by 'path'. 'mode' should be one of S_IFCHR or
 * S_IFBLK (you might note that mknod(2) normally allows one to create
 * regular files as well-- for simplicity this is not the case in Weenix).
 * 'devid', as you might expect, is the device identifier of the device
 * that the new special file should represent.
 *
 * You might use a combination of dir_namev, lookup, and the fs-specific
 * mknod (that is, the containing directory's 'mknod' vnode operation).
 * Return the result of the fs-specific mknod, or an error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EINVAL
 *        mode requested creation of something other than a device special
 *        file.
 *      o EEXIST
 *        path already exists.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_mknod(const char *path, int mode, unsigned devid)
{
        /* VFS {{{ */
        int ret = 0;
        size_t namelen;
        vnode_t *basedir = NULL, *targ = NULL;
        const char *name;

        if (!(S_ISCHR(mode) || S_ISBLK(mode) || S_ISREG(mode))) {
                ret = -EINVAL;
                goto done;
        }

        if (0 > (ret = dir_namev(path, &namelen, &name, curproc->p_cwd, &basedir))) {
                KASSERT(NULL == basedir);
                goto done;
        }

        if (namelen >= NAME_LEN) {
                ret = -ENAMETOOLONG;
                goto done;
        }

        if (0 == lookup(basedir, name, namelen, &targ)) {
                ret = -EEXIST;
                goto done;
        } else {
                /* if there was an error targ should not be updated */
                KASSERT(NULL == targ);
        }
        /* since lookup succeeded this must be a directory */
        KASSERT(NULL != basedir->vn_ops->mknod);

        ret = basedir->vn_ops->mknod(basedir, name, namelen, mode, devid);

done:
        if (likely(basedir != NULL)) {
                vput(basedir);
        }
        if (likely(targ != NULL)) {
                vput(targ);
        }

        return ret;
        /* VFS }}} */
        return -1;
}

/* Use dir_namev() to find the vnode of the dir we want to make the new
 * directory in.  Then use lookup() to make sure it doesn't already exist.
 * Finally call the dir's mkdir vn_ops. Return what it returns.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EEXIST
 *        path already exists.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_mkdir(const char *path)
{
        /* VFS {{{ */
        int ret;
        size_t namelen;
        vnode_t *basedir = NULL, *targ = NULL;
        const char *name;

        if (0 > (ret = dir_namev(path, &namelen, &name, curproc->p_cwd, &basedir))) {
                KASSERT(NULL == basedir);
                return ret;
        }

        if (namelen >= NAME_LEN) {
                ret = -ENAMETOOLONG;
                goto done;
        }

        if (!S_ISDIR(basedir->vn_mode)) {
                ret = -ENOTDIR;
                goto done;
        }

        if (0 == lookup(basedir, name, namelen, &targ)) {
                ret = -EEXIST;
                goto done;
        } else {
                /* if there was an error targ should not be updated */
                KASSERT(NULL == targ);
        }

        KASSERT(NULL != basedir->vn_ops->mkdir);
        ret = basedir->vn_ops->mkdir(basedir, name, namelen);

done:
        if (likely(basedir != NULL)) {
                vput(basedir);
        }
        if (likely(targ != NULL)) {
                vput(targ);
        }

        return ret;
        /* VFS }}} */
        return -1;
}

/* Use dir_namev() to find the vnode of the directory containing the dir to be
 * removed. Then call the containing dir's rmdir v_op.  The rmdir v_op will
 * return an error if the dir to be removed does not exist or is not empty, so
 * you don't need to worry about that here. Return the value of the v_op,
 * or an error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EINVAL
 *        path has "." as its final component.
 *      o ENOTEMPTY
 *        path has ".." as its final component.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_rmdir(const char *path)
{
        /* VFS {{{ */
        int ret = 0;
        size_t namelen;
        vnode_t *basedir = NULL, *targ = NULL;
        const char *name;

        if (0 > (ret = dir_namev(path, &namelen, &name, curproc->p_cwd, &basedir))) {
                KASSERT(NULL == basedir);
                goto done;
        }

        if (name_match(".", name, namelen)) {
                ret = -EINVAL;
                goto done;
        }

        if (name_match("..", name, namelen)) {
                ret = -ENOTEMPTY;
                goto done;
        }

        /* to make sure it is removable we must look it up */
        if (0 > (ret = lookup(basedir, name, namelen, &targ))) {
                KASSERT(NULL == targ);
                goto done;
        }

        if (!S_ISDIR(targ->vn_mode)) {
                ret = -ENOTDIR;
                goto done;
        }

#ifdef __MOUNTING__
        if (targ->vn_fs->fs_root == targ) {
                ret = -EBUSY;
                goto done;
        }
#endif

        KASSERT(NULL != basedir->vn_ops->rmdir);
        ret = basedir->vn_ops->rmdir(basedir, name, namelen);

done:
        if (likely(NULL != basedir)) {
                vput(basedir);
        }
        if (likely(NULL != targ)) {
                vput(targ);
        }

        return ret;
        /* VFS }}} */
        return -1;
}

/*
 * Same as do_rmdir, but for files.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EISDIR
 *        path refers to a directory.
 *      o ENOENT
 *        A component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_unlink(const char *path)
{
        /* VFS {{{ */
        int ret = 0;
        size_t namelen;
        vnode_t *basedir = NULL, *targ = NULL;
        const char *name;

        if (0 > (ret = dir_namev(path, &namelen, &name, curproc->p_cwd, &basedir))) {
                KASSERT(NULL == basedir);
                goto done;
        }

        if (0 > (ret = lookup(basedir, name, namelen, &targ))) {
                KASSERT(NULL == targ);
                goto done;
        }

        if (S_ISDIR(targ->vn_mode)) {
                ret = -EISDIR;
                goto done;
        }

        KASSERT(NULL != basedir->vn_ops->unlink);
        ret = basedir->vn_ops->unlink(basedir, name, namelen);

done:
        if (likely(NULL != basedir)) {
                vput(basedir);
        }
        if (likely(NULL != targ)) {
                vput(targ);
        }

        return ret;
        /* VFS }}} */
        return -1;
}

/* To link:
 *      o open_namev(from)
 *      o dir_namev(to)
 *      o call the destination dir's (to) link vn_ops.
 *      o return the result of link, or an error
 *
 * Remember to vput the vnodes returned from open_namev and dir_namev.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EEXIST
 *        to already exists.
 *      o ENOENT
 *        A directory component in from or to does not exist.
 *      o ENOTDIR
 *        A component used as a directory in from or to is not, in fact, a
 *        directory.
 *      o ENAMETOOLONG
 *        A component of from or to was too long.
 */
int
do_link(const char *from, const char *to)
{
        /* VFS {{{ */
        int ret;
        size_t namelen;
        vnode_t *fromvn = NULL, *tovn = NULL;
        const char *name;

        if (0 > (ret = open_namev(from, O_RDONLY, &fromvn, curproc->p_cwd))) {
                KASSERT(NULL == fromvn);
                goto done;
        }

        if (0 > (ret = dir_namev(to, &namelen, &name, curproc->p_cwd, &tovn))) {
                KASSERT(NULL == tovn);
                goto done;
        }

        if (namelen >= NAME_LEN) {
                ret = -ENAMETOOLONG;
                goto done;
        }

#ifdef __MOUNTING__
        if (fromvn->vn_fs != tovn->vn_fs) {
                ret = -EXDEV;
                goto done;
        }
#endif

        if (NULL == tovn->vn_ops->link) {
                ret = -ENOTDIR;
                goto done;
        }

        ret = tovn->vn_ops->link(fromvn, tovn, name, namelen);

done:
        if (likely(NULL != tovn)) {
                vput(tovn);
        }
        if (likely(NULL != fromvn)) {
                vput(fromvn);
        }

        return ret;
        /* VFS }}} */
        return -1;
}

/*      o link newname to oldname
 *      o unlink oldname
 *      o return the value of unlink, or an error
 *
 * Note that this does not provide the same behavior as the
 * Linux system call (if unlink fails then two links to the
 * file could exist).
 */
int
do_rename(const char *oldname, const char *newname)
{
        /* VFS {{{ */
        int ret;

        if (0 > (ret = do_link(oldname, newname))) {
                return ret;
        }

        return do_unlink(oldname);
        /* VFS }}} */
        return -1;
}

/* Make the named directory the current process's cwd (current working
 * directory).  Don't forget to down the refcount to the old cwd (vput()) and
 * up the refcount to the new cwd (open_namev() or vget()). Return 0 on
 * success.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o ENOENT
 *        path does not exist.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 *      o ENOTDIR
 *        A component of path is not a directory.
 */
int
do_chdir(const char *path)
{
        /* VFS {{{ */
        vnode_t *newcwd; int ret;

        if (0 > (ret = open_namev(path, O_RDONLY, &newcwd, curproc->p_cwd))) {
                return ret;
        }

        if (!S_ISDIR(newcwd->vn_mode)) {
                vput(newcwd);
                return -ENOTDIR;
        }

        vput(curproc->p_cwd);
        curproc->p_cwd = newcwd;

        return 0;
        /* VFS }}} */
        return -1;
}

/* Call the readdir f_op on the given fd, filling in the given dirent_t*.
 * If the readdir f_op is successful, it will return a positive value which
 * is the number of bytes copied to the dirent_t.  You need to increment the
 * file_t's f_pos by this amount.  As always, be aware of refcounts, check
 * the return value of the fget and the virtual function, and be sure the
 * virtual function exists (is not null) before calling it.
 *
 * Return either 0 or sizeof(dirent_t), or -errno.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        Invalid file descriptor fd.
 *      o ENOTDIR
 *        File descriptor does not refer to a directory.
 */
int
do_getdent(int fd, struct dirent *dirp)
{
        /* VFS {{{ */
        file_t *f;
        int ret;

        if ((0 > fd) || NULL == (f = fget(fd))) {
                return -EBADF;
        }

        if ((f->f_vnode->vn_ops->readdir == NULL) || (!S_ISDIR(f->f_vnode->vn_mode))) {
                fput(f);
                return -ENOTDIR;
        }

        if ((ret = f->f_vnode->vn_ops->readdir(f->f_vnode, f->f_pos, dirp)) <= 0) {
                fput(f);
                return ret;
        }

#ifdef __MOUNTING__
        /* We need to resolve from the inode of the filesystem that was readdired
           from into the inode of the actual file system that the user sees mounted
           at this point */
        if (strcmp(dirp->d_name, "..") == 0) {
                if (f->f_vnode == f->f_vnode->vn_fs->fs_root) {
                        vnode_t *mtpt = f->f_vnode->vn_fs->fs_mtpt;
                        vnode_t *par;
                        mtpt->vn_ops->lookup(mtpt, "..", 2, &par);
                        dirp->d_ino = par->vn_vno;
                        vput(par);
                }
        } else {
                vnode_t *node;
                node = vget(f->f_vnode->vn_fs, dirp->d_ino);
                dirp->d_ino = node->vn_mount->vn_vno;
                vput(node);
        }
#endif

        f->f_pos += ret;
        fput(f);

        return sizeof(dirent_t);
        /* VFS }}} */
        return -1;
}

/*
 * Modify f_pos according to offset and whence.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd is not an open file descriptor.
 *      o EINVAL
 *        whence is not one of SEEK_SET, SEEK_CUR, SEEK_END; or the resulting
 *        file offset would be negative.
 */
int
do_lseek(int fd, int offset, int whence)
{
        /* VFS {{{ */
        file_t *f;
        int newpos;

        if ((fd < 0) || ((f = fget(fd)) == NULL)) {
                return -EBADF;
        }

        switch (whence) {
                case SEEK_SET:
                        newpos = offset;
                        break;
                case SEEK_CUR:
                        newpos = f->f_pos + offset;
                        break;
                case SEEK_END:
                        newpos = f->f_vnode->vn_len + offset;
                        break;
                default:
                        fput(f);
                        return -EINVAL;
        }

        if (0 <= newpos) {
                f->f_pos = newpos;
        } else {
                newpos = -EINVAL;
        }

        fput(f);

        return newpos;
        /* VFS }}} */
        return -1;
}

/*
 * Find the vnode associated with the path, and call the stat() vnode operation.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o ENOENT
 *        A component of path does not exist.
 *      o ENOTDIR
 *        A component of the path prefix of path is not a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_stat(const char *path, struct stat *buf)
{
        /* VFS {{{ */
        vnode_t *res;
        int ret;

        if (0 > (ret = open_namev(path, O_RDONLY, &res, curproc->p_cwd))) {
                return ret;
        }

        KASSERT(res->vn_ops->stat);
        if (0 > (ret = res->vn_ops->stat(res, buf))) {
                vput(res);
                return ret;
        }

        vput(res);
        return 0;
        /* VFS }}} */
        return -1;
}

#ifdef __MOUNTING__
/*
 * Implementing this function is not required and strongly discouraged unless
 * you are absolutely sure your Weenix is perfect.
 *
 * This is the syscall entry point into vfs for mounting. You will need to
 * create the fs_t struct and populate its fs_dev and fs_type fields before
 * calling vfs's mountfunc(). mountfunc() will use the fields you populated
 * in order to determine which underlying filesystem's mount function should
 * be run, then it will finish setting up the fs_t struct. At this point you
 * have a fully functioning file system, however it is not mounted on the
 * virtual file system, you will need to call vfs_mount to do this.
 *
 * There are lots of things which can go wrong here. Make sure you have good
 * error handling. Remember the fs_dev and fs_type buffers have limited size
 * so you should not write arbitrary length strings to them.
 */
int
do_mount(const char *source, const char *target, const char *type)
{
        NOT_YET_IMPLEMENTED("MOUNTING: do_mount");
        return -EINVAL;
}

/*
 * Implementing this function is not required and strongly discouraged unless
 * you are absolutley sure your Weenix is perfect.
 *
 * This function delegates all of the real work to vfs_umount. You should not worry
 * about freeing the fs_t struct here, that is done in vfs_umount. All this function
 * does is figure out which file system to pass to vfs_umount and do good error
 * checking.
 */
int
do_umount(const char *target)
{
        NOT_YET_IMPLEMENTED("MOUNTING: do_umount");
        return -EINVAL;
}
#endif
