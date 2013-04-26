#include "commands.h"

#include "command.h"
#include "errno.h"
#include "priv.h"

#include "proc/proc.h"
#include "proc/kthread.h"

#ifdef __VFS__
#include "fs/fcntl.h"
#include "fs/file.h"
#include "fs/vfs_syscall.h"
#include "fs/vnode.h"
#include "fs/lseek.h"
#include "fs/s5fs/s5fs.h"
#endif

#include "test/kshell/io.h"

#include "util/debug.h"
#include "util/string.h"
#include "util/printf.h"

int kshell_help(kshell_t *ksh, int argc, char **argv)
{
        /* Print a list of available commands */
        int i;

        kshell_command_t *cmd;
        char spaces[KSH_CMD_NAME_LEN];
        memset(spaces, ' ', KSH_CMD_NAME_LEN);

        kprintf(ksh, "Available commands:\n");
        list_iterate_begin(&kshell_commands_list, cmd, kshell_command_t,
                           kc_commands_link) {
                KASSERT(NULL != cmd);
                int namelen = strnlen(cmd->kc_name, KSH_CMD_NAME_LEN);
                spaces[KSH_CMD_NAME_LEN - namelen] = '\0';
                kprintf(ksh, "%s%s%s\n", cmd->kc_name, spaces, cmd->kc_desc);
                spaces[KSH_CMD_NAME_LEN - namelen] = ' ';
        } list_iterate_end();

        return 0;
}

int kshell_exit(kshell_t *ksh, int argc, char **argv)
{
        panic("kshell: kshell_exit should NEVER be called");
        return 0;
}

int kshell_echo(kshell_t *ksh, int argc, char **argv)
{
        if (argc == 1) {
                kprintf(ksh, "\n");
        } else {
                int i;

                for (i = 1; i < argc - 1; ++i) {
                        kprintf(ksh, "%s ", argv[i]);
                }
                kprintf(ksh, "%s\n", argv[argc - 1]);
        }

        return 0;
}

#ifdef __VFS__
int kshell_cat(kshell_t *ksh, int argc, char **argv)
{
        if (argc < 2) {
                kprintf(ksh, "Usage: cat <files>\n");
                return 0;
        }

        char buf[KSH_BUF_SIZE];
        int i;
        for (i = 1; i < argc; ++i) {
                int fd, retval;

                if ((fd = do_open(argv[i], O_RDONLY)) < 0) {
                        kprintf(ksh, "Error opening file: %s\n", argv[i]);
                        continue;
                }

                while ((retval = do_read(fd, buf, KSH_BUF_SIZE)) > 0) {
                        retval = kshell_write_all(ksh, buf, retval);
                        if (retval < 0) break;
                }
                if (retval < 0) {
                        kprintf(ksh, "Error reading or writing %s: %d\n", argv[i], retval);
                }

                if ((retval = do_close(fd)) < 0) {
                        panic("kshell: Error closing file: %s\n", argv[i]);
                }
        }

        return 0;
}

int kshell_ls(kshell_t *ksh, int argc, char **argv)
{
        int arglen, ret, fd;
        dirent_t dirent;
        struct stat statbuf;
        char direntname[KSH_BUF_SIZE];

        memset(direntname, '\0', KSH_BUF_SIZE);

        if (argc > 3) {
                kprintf(ksh, "Usage: ls <directory>\n");
                return 0;
        } else if (argc == 2) {
                if ((ret = do_stat(argv[1], &statbuf)) < 0) {
                        if (ret == -ENOENT) {
                                kprintf(ksh, "%s does not exist\n", argv[1]);
                                return 0;
                        } else {
                                return ret;
                        }
                }
                if (!S_ISDIR(statbuf.st_mode)) {
                        kprintf(ksh, "%s is not a directory\n", argv[1]);
                        return 0;
                }

                if ((fd = do_open(argv[1], O_RDONLY)) < 0) {
                        kprintf(ksh, "Could not find directory: %s\n", argv[1]);
                        return 0;
                }
                arglen = strnlen(argv[1], KSH_BUF_SIZE);
        } else {
                KASSERT(argc == 1);
                if ((fd = do_open(".", O_RDONLY)) < 0) {
                        kprintf(ksh, "Could not find directory: .\n");
                        return 0;
                }
                arglen = 1;
        }

        if (argc == 2)
                memcpy(direntname, argv[1], arglen);
        else
                direntname[0] = '.';

        direntname[arglen] = '/';
        direntname[arglen + NAME_LEN + 1] = '\0';

        while ((ret = do_getdent(fd, &dirent)) == sizeof(dirent_t)) {
                memcpy(direntname + arglen + 1, dirent.d_name, NAME_LEN + 1);
                if ((ret = do_stat(direntname, &statbuf)) < 0) {
                        kprintf(ksh, "Error stat\'ing %s\n", dirent.d_name);
                        continue;
                }
                if (S_ISDIR(statbuf.st_mode)) {
                        kprintf(ksh, "%s/\n", dirent.d_name);
                } else {
                        kprintf(ksh, "%s\n", dirent.d_name);
                }
        }

        do_close(fd);
        return ret;
}

int kshell_cd(kshell_t *ksh, int argc, char **argv)
{
        KASSERT(NULL != ksh);

        int ret;

        if (argc < 2) {
                kprintf(ksh, "Usage: cd <directory>\n");
                return 0;
        }

        if ((ret = do_chdir(argv[1])) < 0) {
                kprintf(ksh, "Error cd\'ing into %s\n", argv[1]);
        }
        return 0;
}

int kshell_rm(kshell_t *ksh, int argc, char **argv)
{
        KASSERT(NULL != ksh);

        int ret;

        if (argc < 2) {
                kprintf(ksh, "Usage: rm <file>\n");
                return 0;
        }

        if ((ret = do_unlink(argv[1])) < 0) {
                kprintf(ksh, "Error unlinking %s\n", argv[1]);
        }

        return 0;
}

int kshell_link(kshell_t *ksh, int argc, char **argv)
{
        KASSERT(NULL != ksh);

        int ret;

        if (argc < 3) {
                kprintf(ksh, "Usage: link <src> <dst>\n");
                return 0;
        }

        if ((ret = do_link(argv[1], argv[2])) < 0) {
                kprintf(ksh, "Error linking %s to %s: %d\n", argv[1], argv[2], ret);
        }

        return 0;
}

int kshell_rmdir(kshell_t *ksh, int argc, char **argv)
{
        KASSERT(NULL != ksh);
        KASSERT(NULL != argv);

        int i;
        int exit_val;
        int ret;

        if (argc < 2) {
                kprintf(ksh, "Usage: rmdir DIRECTORY...\n");
                return 1;
        }

        exit_val = 0;
        for (i = 1; i < argc; ++i) {
                if ((ret = do_rmdir(argv[i])) < 0) {
                        char *errstr = strerror(-ret);
                        kprintf(ksh, "rmdir: failed to remove `%s': %s\n",
                                argv[i], errstr);
                        exit_val = 1;
                }
        }

        return exit_val;
}

int kshell_mkdir(kshell_t *ksh, int argc, char **argv)
{
        KASSERT(NULL != ksh);
        KASSERT(NULL != argv);

        int i;
        int exit_val;
        int ret;

        if (argc < 2) {
                kprintf(ksh, "Usage: mkdir DIRECTORY...\n");
                return 1;
        }

        exit_val = 0;
        for (i = 1; i < argc; ++i) {
                if ((ret = do_mkdir(argv[i])) < 0) {
                        char *errstr = strerror(-ret);
                        kprintf(ksh,
                                "mkdir: cannot create directory `%s': %s\n",
                                argv[i], errstr);
                        exit_val = 1;
                }
        }

        return exit_val;
}

static const char *get_file_type_str(int mode)
{
        if (S_ISCHR(mode)) {
                return "character special file";
        } else if (S_ISDIR(mode)) {
                return "directory";
        } else if (S_ISBLK(mode)) {
                return "block special file";
        } else if (S_ISREG(mode)) {
                return "regular file";
        } else if (S_ISLNK(mode)) {
                return "symbolic link";
        } else {
                return "unknown";
        }
}

int kshell_stat(kshell_t *ksh, int argc, char **argv)
{
        KASSERT(NULL != ksh);
        KASSERT(NULL != argv);

        int i;
        int exit_val = 0;
        int ret;
        struct stat buf;

        if (argc < 2) {
                kprintf(ksh, "Usage: stat FILE...\n");
                return 1;
        }

        for (i = 1; i < argc; ++i) {
                if ((ret = do_stat(argv[i], &buf)) < 0) {
                        char *errstr = strerror(-ret);
                        kprintf(ksh, "Cannot state `%s': %s\n",
                                argv[i], errstr);
                        exit_val = 1;
                } else {
                        const char *file_type_str =
                                get_file_type_str(buf.st_mode);
                        kprintf(ksh, "File: `%s'\n", argv[i]);
                        kprintf(ksh, "Size: %d\n", buf.st_size);
                        kprintf(ksh, "Blocks: %d\n", buf.st_blocks);
                        kprintf(ksh, "IO Block: %d\n", buf.st_blksize);
                        kprintf(ksh, "%s\n", file_type_str);
                        kprintf(ksh, "Inode: %d\n", buf.st_ino);
                        kprintf(ksh, "Links: %d\n", buf.st_nlink);
                }
        }

        return exit_val;
}

#define FILE_CONTENT "look!\n"
#define CONTENT_LEN 6
#define TESTBUFLEN 256

/* Write a sparse file, specifically one that has FILE_CONTENT starting at byte
 * 50000 and no other data.  The size will be 50000+CONTENT_LEN, but only one
 * block is allocated to the file.
 */
int kshell_sparse_test(kshell_t *ksh, int argc, char **argv) {
        KASSERT(NULL != ksh);
        KASSERT(NULL != argv);

	int f = -1;	/* file descriptor */
	int rv = 0;	/* Return value */

	/* Open the file, and create it if not present */
	if ( (f = do_open("/sparse", O_WRONLY | O_CREAT)) < 0) {
	    kprintf(ksh, "Couldn't open /sparse\n");
	    return -1;
	}

	/* Seek out 50000 bytes (the seek pointer is in the VFS layer, so the
	 * s5fs layer will just see a write at byte 50000 later) */
	if ( (rv = do_lseek(f, 50000, SEEK_END)) != 50000 ) {
	    do_close(f);
	    kprintf(ksh, "seek failed %d\n", rv);
	    return -1;
	}

	/* Write the visible bytes.  Here's where the first memory/disk block
	 * is allocated. */
	if ( do_write(f, FILE_CONTENT, CONTENT_LEN) != CONTENT_LEN) {
	    do_close(f);
	    kprintf(ksh, "File write failed?\n");
	    return -1;
	}

	/* Close up and go home */
	do_close(f);
	kprintf(ksh, "Created sparse file \"/sparse\"\n");
	return 0;
}

static char block[S5_BLOCK_SIZE];

/* Create as large a file as the file system structures will allow.  The disk
 * that comes with weenix will not support a file this size, so it will fail in
 * the do_write calls.
 */
int kshell_space_test(kshell_t *ksh, int argc, char **argv) {
        KASSERT(NULL != ksh);
	int f = -1;	/* File descriptor */
	int rv = 0;	/* Return value */
	int i = 0;	/* Scratch */

	/* Create the file */
	if ( (f = do_open("/space", O_WRONLY | O_CREAT)) < 0) {
	    kprintf(ksh, "Couldn't open /space (%d) %s\n", f, strerror(-f));
	    return -1;
	}
	/* Write data until the either the max size file is written or the
	 * filesystem fills.  The filesystem should fill.
	 */
	for ( i = 0; i < (int) S5_MAX_FILE_BLOCKS; i++ ) {
	    if ( (rv = do_write(f, block, S5_BLOCK_SIZE)) < 0 ) {
		/* Close the file or we keep it forever... */
		do_close(f);
		kprintf(ksh, "Error writing block %d of /space (%d) %s\n", 
			i, rv, strerror(-rv));
		return rv;
	    }
	}
	rv = 0;
	return 0;
}

/* 
 * A function executed by a thread that creates a directory called /dir00n
 * (where n is arg1) and writes 50 files into it called /dir00n/test000 through
 * /dir00n/test049.  If any fail, exit with their return code.  Threads running
 * this are created by both kshell_thread_test and kshell_directory_test.
 */
static void *make_dir_thread(int arg1, void *arg2) {
	char dir[TESTBUFLEN];	/* A buffer for the directory name */
	char file[TESTBUFLEN];	/* A buffer for the full file pathname */
	int rv = 0;		/* return values */
	int i = 0;		/* Scratch */

	/* Make the directory name and the directory.  Snprintf is safe - it
	 * always zero-terminates and never overflows the buffer. */
	snprintf(dir, TESTBUFLEN, "/dir%03d", arg1);
	do_mkdir(dir);
	for (i = 0; i < 50 ; i++ ) {
	    int f= 0;	/* File descriptor */

	    snprintf(file, TESTBUFLEN, "%s/test%03d", dir, i);
	    /* Open a file (creating it if it's not there) */
	    if ( (f = do_open(file, O_WRONLY | O_CREAT)) < 0 ) {
		rv = f;
		goto fail;
	    }
	    /* Write the same content to every file. */
	    if ( (rv = do_write(f, FILE_CONTENT, CONTENT_LEN)) != CONTENT_LEN) {
		do_close(f);
		goto fail;
	    }
	    do_close(f);
	}
	rv = 0;
fail:
	do_exit(rv);
	return NULL;
}

/* 
 * A function executed by a thread that creates a directory called /dir00n
 * (where n is arg1) and unlinks 50 files from it called /dir00n/test000 through
 * /dir00n/test049.  Ignore the return codes (files may not be there yet).
 * Threads running this are created by kshell_directory_test.  Structurally
 * similar to make_dir_thread. 
 */
static void *rm_dir_thread(int arg1, void *arg2) {
	char dir[TESTBUFLEN];	/* Directory pathname */
	char file[TESTBUFLEN];	/* Each file's pathname */
	int rv = 0;		/* Return value */
	int i = 0;		/* Scratch */

	/* Make the directory */
	snprintf(dir, TESTBUFLEN, "/dir%03d", arg1);
	do_mkdir(dir);

	/* Unlink the files */
	for (i = 0; i < 50 ; i++ ) {
	    snprintf(file, TESTBUFLEN, "%s/test%03d", dir, i);
	    do_unlink(file);
	}
	do_exit(rv);
	return NULL;
}

/*
 * Run multiple threads, each running make_dir_thread with a different
 * parameter.  The calling format (from the kshell) is thread_test <num>, where
 * num is an integer.  The number is parsed into an int and threads with
 * parameter 0..num are created.  This code then waits for them and prints each
 * exit value.  Fewer than 4 copies should exit safely, but more than that will
 * run out of inodes.
 */
int kshell_thread_test(kshell_t *ksh, int argc, char **argv) {
        KASSERT(NULL != ksh);

        proc_t *p = NULL;	    /* A process to run in */
        kthread_t *thr = NULL;	    /* A thread to run in the process */
	char tname[TESTBUFLEN];	    /* The thread name */
	int pid = 0;		    /* Process ID returned by do_waitpid */
	int lim = 1;		    /* Number of processes to start */
	int rv = 0;		    /* Return value */
	int i = 0;		    /* Scratch */

        KASSERT(NULL != ksh);
	if ( argc > 1) {
	    /* Oh, barf, sscanf makes me ill.*/
	    if ( sscanf(argv[1], "%d", &lim) != 1)
		lim = 1;
	    /* A little bounds checking */
	    if ( lim < 1 || lim > 255 )
		lim = 1;
	}

	/* Start the children */
	for ( i = 0; i< lim; i++) {
	    snprintf(tname, TESTBUFLEN, "thread%03d", i);
	    p = proc_create(tname);
	    KASSERT(NULL != p);
	    thr = kthread_create(p, make_dir_thread, i, NULL);
	    KASSERT(NULL != thr);

	    sched_make_runnable(thr);
	}

	/* Wait for children and report errors in their return values */
	while ( ( pid = do_waitpid(-1, 0 , &rv) ) != -ECHILD) 
	    if ( rv < 0 ) 
		kprintf(ksh, "Child %d: %d %s\n", pid, rv, strerror(-rv));
	    else
		kprintf(ksh, "Child %d: %d\n", pid, rv);

	return rv;
}

/*
 * Run multiple pairs of threads, each running make_dir_thread or rm_dir_thread
 * with a different parameter.  One will be creating a directory full of files,
 * and the other will be deleting them.  They do not move in lock step, so for
 * a few threads, say 4 or 5, the system is likely to run out of i-nodes.
 *
 * The calling format (from the kshell) is
 * directory_test <num>, where num is an integer.  The number is parsed into an
 * int and pairs of threads with parameter 0..num are created.  This code then
 * waits for them and prints each exit value.
 */
int kshell_directory_test(kshell_t *ksh, int argc, char **argv) {
        KASSERT(NULL != ksh);

        proc_t *p = NULL;	/* A process to run in */
        kthread_t *thr = NULL;	/* A thread to run in it */
	char tname[TESTBUFLEN];	/* A thread name */
	int pid = 0;		/* Process ID from do_waitpid (who exited?) */
	int lim = 1;		/* Number of processes to run */
	int rv = 0;		/* Return values */
	int i = 0;		/* Scratch */

        KASSERT(NULL != ksh);
	if ( argc > 1) {
	    /* Oh, barf */
	    if ( sscanf(argv[1], "%d", &lim) != 1)
		lim = 1;
	    /* A little bounds checking */
	    if ( lim < 1 || lim > 255 )
		lim = 1;
	}

	/*  Start pairs of processes */
	for ( i = 0; i< lim; i++) {

	    /* The maker process */
	    snprintf(tname, TESTBUFLEN, "thread%03d", i);
	    p = proc_create(tname);
	    KASSERT(NULL != p);
	    thr = kthread_create(p, make_dir_thread, i, NULL);
	    KASSERT(NULL != thr);

	    sched_make_runnable(thr);

	    /* The deleter process */
	    snprintf(tname, TESTBUFLEN, "rmthread%03d", i);
	    p = proc_create(tname);
	    KASSERT(NULL != p);
	    thr = kthread_create(p, rm_dir_thread, i, NULL);
	    KASSERT(NULL != thr);

	    sched_make_runnable(thr);
	}

	/* Wait for children and report their error codes */
	while ( ( pid = do_waitpid(-1, 0 , &rv) ) != -ECHILD) 
	    if ( rv < 0 ) 
		kprintf(ksh, "Child %d: %d %s\n", pid, rv, strerror(-rv));
	    else
		kprintf(ksh, "Child %d: %d\n", pid, rv);

	return rv;
}

/* 
 * A little convenience command to clear the files from a directory (though not
 * the directories).  Called from the kshell as cleardir <name>.
 */
int kshell_cleardir(kshell_t *ksh, int argc, char **argv) {
        KASSERT(NULL != ksh);

	struct dirent d;	/* The current directory entry */
	char buf[TESTBUFLEN];	/* A buffer used to assemble the pathname */
	int f = -1;		/* File descriptor (dirs must be opened) */
	int rv = 0;		/* Return values */
	int got_one = 1;	/* True if this iteration deleted a file */

	if ( argc < 2 ) {
	    kprintf(ksh, "Usage: cleardir dir\n");
	    return -1;
	}

	/* Because unlinking a file changes the order of the directory entries
	 * - specifically, it moves a directory entry into the spot where the
	 *   unlinked file's directory entry was - this loop starts over every
	 *   time a file is unlinked.  The pseudocode is:
	 *
	 *  repeat
	 *	open the directory.
	 *	find the first directory entry that is neither . nor ..
	 *	unlink it
	 *	close the directory
	 *  until only . and .. are left
	 */
	while ( got_one ) {
	    got_one = 0;
	    /* Open up the directory */
	    if ( (f = do_open(argv[1], O_RDONLY)) < 0 ) {
		kprintf(ksh, "Open failed on %s: %d %s\n", argv[1], f, 
			strerror(-f));
		return -1;
	    }

	    while ( ( rv = do_getdent(f, &d)) > 0 ) {
		/* Keep looking if d contains . or .. */
		if ( strncmp(".", d.d_name, NAME_LEN) == 0 || 
			strncmp("..", d.d_name, NAME_LEN) == 0 ) 
		    continue;
		/* Found a name to delete.  Construct the path and delete it */
		snprintf(buf, TESTBUFLEN, "%s/%s", argv[1], d.d_name);
		kprintf(ksh, "unlinking %s\n", buf);
		if ( (rv = do_unlink(buf)) < 0 ) {
		    /* Something went wrong- d probably points to a directory.
		     * Report the error, close f, and terminate the command. */
		    kprintf(ksh, "Unlink failed on %s: %d %s\n", buf, rv, 
			    strerror(-rv));
		    do_close(f);
		    return rv;
		}
		got_one = 1;
		/* CLose the directory and restart (because it set got_one) */
		break;
	    }
	    do_close(f);
	    /* This branch will be taken if do_getdent fails */
	    if ( rv < 0 ) {
		kprintf(ksh, "get_dent failed on %s: %d %s\n", argv[1], rv, 
			strerror(-rv));
		return rv;
	    }
	}
	/* OK, deleted everything we could. */
	return rv;
}

#endif
