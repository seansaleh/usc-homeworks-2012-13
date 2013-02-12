#define _WITH_GETLINE
#include <stdio.h>
#include <sys/types.h>
#include <sys/param.h>
#include <unistd.h>
#include <sys/uio.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <dirent.h>
#include "window.h"

#define FNLEN 256

/* Number of windows created so far.  Used to keep fifo names distinct */
int window_count = 0;
/* Name of the directory that holds fifos */
char *tmpdir= NULL;
/* A template for mkdtemp (3) to create the temp dir */
static char *template = "/tmp/serverXXXXXX";

/* Create the temporary directory that holds this server's named pipes (fifos).
 * That directory is stored in the global tmpdir variable.  This is not
 * thread-safe. The function is just a direct call of mkdtemp (3) with error
 * handling. Return true if all works out. */
static int make_tempdir() {
    if (! tmpdir) {
	if ( !(tmpdir = (char *) malloc(strlen(template) +1)))
	    return 0;
	strcpy(tmpdir, template);
	if ( !mkdtemp(tmpdir)) {
	    free(tmpdir);
	    tmpdir = NULL;
	    perror("mkdtemp");
	    return 0;
	}
    }
    return 1;
}

/* Create fifos for the child process connected to this window to use.  These
 * are called inputX and outputX where the X's are a monotonically increasing
 * integer.  The function just directly allocates memory to hold the names in
 * the window data structure and created the fifo.  If anything fails, false is
 * returned.  This is not a thread-safe function. */
static int create_fifos(window_t *new_window) {
    /* Make a tempdir if we don't have one. */
    if (! tmpdir && !make_tempdir()) return 0;
    /* Save the fifo names */
    if (!(new_window->ififo = (char *) malloc(FNLEN))) return 0;
    if (!(new_window->ofifo = (char *) malloc(FNLEN))) return 0;
    snprintf(new_window->ififo, FNLEN, "%s/input%d", tmpdir, window_count);
    snprintf(new_window->ofifo, FNLEN, "%s/output%d", tmpdir, window_count);
    /* create the fifos */
    if (mkfifo(new_window->ififo, 0600) == -1) return 0;
    if (mkfifo(new_window->ofifo, 0600) == -1) return 0;
    return 1;
}

/* Create a window to communicate with an interface process (see interface.c)
 * running under an xterm (which this function also starts).  If anything
 * fails, return a NULL pointer.
 */
window_t *window_create(char *label) {
    window_t *new_window = (window_t *) malloc(sizeof(window_t));

    /* Init ALL THE fields! */
    if (!new_window) return 0;
    new_window->ififo = NULL;
    new_window->ofifo = NULL;
    new_window->in = 0;
    new_window->out = 0;
    new_window->pid = -1;
    new_window->echo = 0;

    if (!create_fifos(new_window)) goto fail;
    window_count++;

    /* Start an interface process and connect to it through the fifos */
    new_window->pid = fork();
    if (new_window->pid == -1) {
	/* Fork failed.  There is no child and we're giving up */
	fprintf(stderr, "could not create process for new window\n");
	goto fail;
    } else if (new_window->pid == 0) {
	/* This is the child.  Run xterm in this address space */
	if (execlp("xterm", "xterm", "-T", label, "-n", label, "-ut",
		   "-geometry", "35x20",
		   "-e", "./interface",
		   new_window->ififo, new_window->ofifo, 
		   (char *)NULL) == -1) {
		perror("exec of xterm failed");
		exit(1);
	}
	perror("exec somehow failed");
	exit(1);
    } else {
	/* This is the parent. Open the input/output streams and fail if we
	 * cannot. NB: fail includes killing the child. 
	 *
	 * The fifo opens are also a little interesting in that the fopen for
	 * reading will block until the other process opens for writing and
	 * vice versa.  interface and this code handshake this way. It's
	 * possible for this to go awry, but for this application our fallback
	 * is a frustrated student terminating the process.
	 */
	if (!(new_window->in = fopen(new_window->ififo, "r"))) goto fail;
	if (!(new_window->out = fopen(new_window->ofifo, "w"))) goto fail;
    }
    return new_window;

fail:
    /* Generic failure routine.  Clean up any partially constructed window
     * and return a failure.  Goto is "bad," but this is a poor person's
     * exception handler for the complex allocation above. */
    window_destroy(new_window);
    return NULL;
}

/* Create a window that is just a link to an input and output file. These kind
 * of windows echo the input file to the output with the results of the
 * commands interspersed.*/
window_t *nowindow_create(char *infn, char *outfn) {
    window_t *new_window = (window_t *) malloc(sizeof(window_t));

    if (!new_window) return 0;
    new_window->ififo = NULL;
    new_window->ofifo = NULL;
    new_window->pid = -1;
    new_window->echo = 1;
    if ( !(new_window->in = fopen(infn, "r")) || 
	    !(new_window->out = fopen(outfn, "w"))) {
	window_destroy(new_window);
	return NULL;
    }
    window_count++;
    return new_window;
}

/*
 * Release window resources.  If fifos were created, delete them, if a process
 * was created, terminate it, close open files.  Release memory, including win.
 */
void window_destroy(window_t * win) {
    if (!win) return;
    if (win->pid != -1 ) { kill(win->pid, SIGTERM); }
    if (win->ififo) { unlink(win->ififo); free(win->ififo);win->ififo = NULL; }
    if (win->ofifo) { unlink(win->ofifo); free(win->ofifo);win->ofifo = NULL; }
    if (win->in) { fclose(win->in); win->in = NULL; }
    if (win->out) { fclose(win->out); win->out = NULL; }
    free(win);
}

/* The main interface for the server to interact with a window.  If query
 * points to a string (a non-NULL char *) and the window has echo set, print
 * that query to the output connection.  If response is longer than zero, print
 * that too.  Then wait for the next command.  getline will manage memory for
 * arbitrary sized queries.  This is safe to call from a thread *if* that is
 * the only thread with access to window and the other parameters.
 *
 * The function returns the return code of getline - notably -1 on end of input.
 */
int serve(window_t * window, char *response, char **query, size_t *qlen) {
    if ( window->echo && *query) 
	fprintf(window->out, ">> %s", *query);
    if (strlen(response) > 0 ) 
	fprintf(window->out, "%s\n", response);
    fflush(window->out);
    return getline(query, qlen, window->in);
}

/* Cleanup the tmp dir.  Remove all the fifos in it and then remove tmpdir.
 * It's an implementation of rm -rf tmpdir.  The server should call this on
 * exit to clean up the temporary directory. It is not thread safe. */
void window_cleanup() {
    DIR *td = NULL;
    struct dirent *d = NULL;
    char path[MAXPATHLEN+1];

    if (!tmpdir) return;
    if (!(td = opendir(tmpdir))) return;
    while ( (d = readdir(td))) {
	/* Skip . and .. */
	if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0) 
	    continue;
	snprintf(path, MAXPATHLEN, "%s/%s", tmpdir, d->d_name);
	path[MAXPATHLEN] = '\0';
	unlink(path);
    }
    rmdir(tmpdir);
    tmpdir = NULL;
    return;
}

