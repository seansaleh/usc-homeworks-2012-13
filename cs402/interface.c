#define _WITH_GETLINE
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

/*
   Program to run in an xterm window to interact with the capital cities
   database program.
 */

/* if true, exit the main loop */
int gotint = 0;

/* Signal handler.  Called on SIGINT (ctrl-c), sets a flag to exit the main
 * loop. */
void sigint() { gotint = 1; }

/* Called is SIGTERM is recieved.  SIGTERM is sent by the server program when
 * the pipe used to send commands is closed. */
void terminate() {
    fprintf(stderr, "\nGoodbye\n");
    exit(0);
}

int main(int argc, const char *argv[]) {
    char *rbuf = NULL;		/* Buffer for responses (rcved from server) */
    char *qbuf = NULL;		/* Buffer for requests (sent to server) */
    size_t rlen=0;		/* Size of rbuf (managed by getline (3)) */
    size_t qlen=0;		/* Size of qbuf (managed by getline (3)) */
    FILE *ifd;			/* FILE used to receive responses from server */
    FILE *ofd;			/* FILE used to send requests to server */
    struct sigaction sa;	/* Action descriptor to set signal handler */
    sigset_t term_set;		/* Set of signals that sa affects */

    /* Signal routing (see sigaction (2)) */
    sigemptyset(&term_set);	
    sa.sa_handler = terminate;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, 0);
    sa.sa_handler = sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, 0);

    /* Check the args */
    if (argc != 3) {
	fprintf(stderr, "Usage: interface infile outfile\n");
	exit(1);
    }


    /* Open the named pipes for talking to the server.  We need to open these
     * in the same order as the server so the two programs do not deadlock */
    if (!(ofd = fopen(argv[1], "w"))) {
	fprintf(stderr, "%s", argv[1]);
	perror("open ofifo");
	sleep(10);
	exit(1);
    }

    if (!(ifd = fopen(argv[2], "r"))) {
	fprintf(stderr, "%s", argv[2]);
	perror("open ififo");
	sleep(10);
	exit(1);
    }

    /* Loop until this program is terminated, passing commands and getting
     * responses */
    for (;;) {
	printf(">> ");
	fflush(stdout);

	if ((getline(&qbuf, &qlen, stdin) == -1) || gotint) {
	    fprintf(stderr, "EOF\n");
	    /* EOF means to terminate */
	    fclose(ofd);
	    /* Go to sleep and await the TERM from the server */
	    for (;;) 
		sigsuspend(&term_set);
	} else {
	    fprintf(ofd, "%s", qbuf);
	    fflush(ofd);
	}
	if (getline(&rbuf, &rlen, ifd) == -1) {
	    perror("read");
	    sleep(10);
	    exit(1);
	}
	/* print the response and the prompt */
	printf("%s", rbuf);
	fflush(stdout);
    }
}
