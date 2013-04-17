#include "kernel.h"
#include "globals.h"
#include "types.h"
#include "errno.h"

#include "util/string.h"
#include "util/printf.h"
#include "util/debug.h"

#include "fs/dirent.h"
#include "fs/fcntl.h"
#include "fs/stat.h"
#include "fs/vfs.h"
#include "fs/vnode.h"

/* This takes a base 'dir', a 'name', its 'len', and a result vnode.
 * Most of the work should be done by the vnode's implementation
 * specific lookup() function, but you may want to special case
 * "." and/or ".." here depnding on your implementation.
 *
 * If dir has no lookup(), return -ENOTDIR.
 *
 * Note: returns with the vnode refcount on *result incremented.
 */
int
lookup(vnode_t *dir, const char *name, size_t len, vnode_t **result)
{
        /* VFS {{{ */
        KASSERT(NULL != dir);
        KASSERT(NULL != name);
        KASSERT(NULL != result);

        if (dir->vn_ops->lookup == NULL) {
                return -ENOTDIR;
        }

        if (0 == len) {
                vref(dir);
                *result = dir;
                return 0;
        }

#ifdef __MOUNTING__
        if (name_match("..", name, len)) {
                if (dir->vn_fs->fs_root == dir) {
                        vnode_t *mtpt = dir->vn_fs->fs_mtpt;
                        return mtpt->vn_ops->lookup(mtpt, name, len, result);
                }
        }
#endif

        return dir->vn_ops->lookup(dir, name, len, result);
        /* VFS }}} */
        return 0;
}

/* VFS BLANK {{{ */
/* Starting at '*search' go through the string looking for the next
 * file name. Updates '*search' to point to the file name and puts the
 * length of the result in '*len'. Returns true if there are more
 * tokens to be read. */
char *
namev_tokenize(char **search, int *len)
{
#define TOKENIZE_DELIM '/'

        char *begin;

        if (*search == NULL) {
                *len = 0;
                return NULL;
        }

        KASSERT(NULL != *search);

        while (TOKENIZE_DELIM == **search) {
                (*search)++;
        }

        begin = *search;
        *len = 0;
        while (TOKENIZE_DELIM != **search && '\0' != **search) {
                (*len)++;
                (*search)++;
        }

        if ('\0' == **search) {
                *search = NULL;
        }

        return begin;

}
/* VFS BLANK }}} */

/* When successful this function returns data in the following "out"-arguments:
 *  o res_vnode: the vnode of the parent directory of "name"
 *  o name: the `basename' (the element of the pathname)
 *  o namelen: the length of the basename
 *
 * For example: dir_namev("/s5fs/bin/ls", &namelen, &name, NULL,
 * &res_vnode) would put 2 in namelen, "ls" in name, and a pointer to the
 * vnode corresponding to "/s5fs/bin" in res_vnode.
 *
 * The "base" argument defines where we start resolving the path from:
 * A base value of NULL means to use the process's current working directory,
 * curproc->p_cwd.  If pathname[0] == '/', ignore base and start with
 * vfs_root_vn.  dir_namev() should call lookup() to take care of resolving each
 * piece of the pathname.
 *
 * Note: A successful call to this causes vnode refcount on *res_vnode to
 * be incremented.
 */
int
dir_namev(const char *pathname, size_t *namelen, const char **name,
          vnode_t *base, vnode_t **res_vnode)
{
        /* VFS {{{ */
        char *saveptr;
        int err;

        char *dirtok;
        int dirlen;

        char *nextok;
        int nextlen;

        vnode_t *curdir;
        vnode_t *nextdir;

        KASSERT(NULL != pathname);
        KASSERT(NULL != namelen);
        KASSERT(NULL != name);
        KASSERT(NULL != res_vnode);

        if ('\0' == pathname[0]) {
                return -EINVAL;
        }

        /* Choose base directory */
        if (pathname[0] == '/') {
                curdir = vfs_root_vn;
        } else if (base == NULL) {
                curdir = curproc->p_cwd;
        } else {
                curdir = base;
        }
        KASSERT(NULL != curdir);
        vref(curdir);

        /* Follow the path */
        saveptr = (char *) pathname;
        dirtok = namev_tokenize(&saveptr, &dirlen);
        nextok = namev_tokenize(&saveptr, &nextlen);
        while (nextlen != 0) {
                err = lookup(curdir, dirtok, dirlen, &nextdir);
                vput(curdir);

                if (0 != err) {
                        return err;
                }

                curdir = nextdir;

                dirtok = nextok;
                dirlen = nextlen;
                nextok = namev_tokenize(&saveptr, &nextlen);
        }

        *res_vnode = curdir;
        *name = dirtok;
        *namelen = dirlen;
        /* VFS }}} */
        return 0;
}

/* This returns in res_vnode the vnode requested by the other parameters.
 * It makes use of dir_namev and lookup to find the specified vnode (if it
 * exists).  flag is right out of the parameters to open(2); see
 * <weenix/fnctl.h>.  If the O_CREAT flag is specified, and the file does
 * not exist call create() in the parent directory vnode.
 *
 * Note: Increments vnode refcount on *res_vnode.
 */
int
open_namev(const char *pathname, int flag, vnode_t **res_vnode, vnode_t *base)
{
        /* VFS {{{ */
        int ret;

        const char *name;
        size_t namelen;

        vnode_t *dir;
        vnode_t *file = NULL;

        if ((ret = dir_namev(pathname, &namelen, &name, base, &dir)) < 0) {
                /* the directory itself doesn't exist */
                return ret;
        }

        /*kthread_cleanup_push(vput,dir);*/

        if ((ret = lookup(dir, name, namelen, &file)) == -ENOENT) {
                /* file doesn't exist.  create it if need be */
                if (flag & O_CREAT) {
                        int ret2;
                        KASSERT(NULL != dir->vn_ops->create);
                        /* We're making a file, check name length */
                        if (namelen >= NAME_LEN) {
                                vput(dir);
                                return -ENAMETOOLONG;
                        }
                        if ((ret2 = dir->vn_ops->create(dir, name, namelen, &file)) < 0) {
                                /* if we can't create it just give up */
                                /*kthread_cleanup_pop(1);*/
                                vput(dir);
                                return ret2;
                        } else {
                                /*kthread_cleanup_pop(1);*/
                                vput(dir);
                                *res_vnode = file;
                                return 0;
                        }
                } else {
                        /* otherwise return error. */
                        /*kthread_cleanup_pop(1);*/
                        vput(dir);
                        return ret;
                }
        } else {
                /* note that we may be in error here, it don't matter */
                *res_vnode = file;
                /*kthread_cleanup_pop(1);*/
                vput(dir);
                return ret;
        }
        /* VFS }}} */
        return 0;
}

#ifdef __GETCWD__
/* Finds the name of 'entry' in the directory 'dir'. The name is writen
 * to the given buffer. On success 0 is returned. If 'dir' does not
 * contain 'entry' then -ENOENT is returned. If the given buffer cannot
 * hold the result then it is filled with as many characters as possible
 * and a null terminator, -ERANGE is returned.
 *
 * Files can be uniquely identified within a file system by their
 * inode numbers. */
int
lookup_name(vnode_t *dir, vnode_t *entry, char *buf, size_t size)
{
        NOT_YET_IMPLEMENTED("GETCWD: lookup_name");
        return -ENOENT;
}


/* Used to find the absolute path of the directory 'dir'. Since
 * directories cannot have more than one link there is always
 * a unique solution. The path is writen to the given buffer.
 * On success 0 is returned. On error this function returns a
 * negative error code. See the man page for getcwd(3) for
 * possible errors. Even if an error code is returned the buffer
 * will be filled with a valid string which has some partial
 * information about the wanted path. */
ssize_t
lookup_dirpath(vnode_t *dir, char *buf, size_t osize)
{
        NOT_YET_IMPLEMENTED("GETCWD: lookup_dirpath");

        return -ENOENT;
}
#endif /* __GETCWD__ */
