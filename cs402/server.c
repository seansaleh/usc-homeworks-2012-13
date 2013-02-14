#include <assert.h>
/* FreeBSD */
#define _WITH_GETLINE
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "window.h"
#include "db.h"
#include "words.h"
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>

/* the encapsulation of a client thread, i.e., the thread that handles
 * commands from clients */
typedef struct Client {
	pthread_t thread;
	window_t *win;
} client_t;

typedef enum INPUT_STATE {
	RUNNING,
	WAITING_FOR_TERMINATIONS,
	TERMINATED
	} INPUT_STATE;
	

/* Interface with a client: get requests, carry them out and report results */
void *client_run(void *);
/* Interface to the db routines.  Pass a command, get a result */
int handle_command(char *, char *, int len);

/*
 * Create an interactive client - one with its own window.  This routine
 * creates the window (which starts the xterm and a process under it.  The
 * window is labelled with the ID passsed in.  On error, a NULL pointer is
 * returned and no process started.  The client data structure returned must be
 * destroyed using client_destroy()
 */
client_t *client_create(int ID) {
    client_t *new_Client = (client_t *) malloc(sizeof(client_t));
    char title[16];

    if (!new_Client) return NULL;

    sprintf(title, "Client %d", ID);

    /* Creates a window and set up a communication channel with it */
    if ((new_Client->win = window_create(title))) return new_Client;
    else {
	free(new_Client);
	return NULL;
    }
}

/*
 * Create a client that reads cmmands from a file and writes output to a file.
 * in and out are the filenames.  If out is NULL then /dev/stdout (the main
 * process's standard output) is used.  On error a NULL pointer is returned.
 * The returned client must be disposed of using client_destroy.
 */
client_t *client_create_no_window(char *in, char *out) {
    char *outf = (out) ? out : "/dev/stdout";
    client_t *new_Client = (client_t *) malloc(sizeof(client_t));
    if (!new_Client) return NULL;

    /* Creates a window and set up a communication channel with it */
    if( (new_Client->win = nowindow_create(in, outf))) return new_Client;
    else {
	free(new_Client);
	return NULL;
    }
}

/*
 * Destroy a client created with either client_create or
 * client_create_no_window.  The cient data structure, the underlying window
 * (if any) and process (if any) are all destroyed and freed, and any open
 * files are closed.  Do not access client after calling this function.
 */
void client_destroy(client_t *client) {
	/* Remove the window */
	window_destroy(client->win);
	free(client);
}

/* Code executed by the client */
void *client_run(void *arg)
{
	client_t *client = (client_t *) arg;

	/* main loop of the client: fetch commands from window, interpret
	 * and handle them, return results to window. */
	char *command = 0;
	size_t clen = 0;
	/* response must be empty for the first call to serve */
	char response[256] = { 0 };

	/* Serve until the other side closes the pipe */
	while (serve(client->win, response, &command, &clen) != -1) {
	    handle_command(command, response, sizeof(response));
	}
	return 0;
}

int handle_command(char *command, char *response, int len) {
    if (command[0] == EOF) {
	strncpy(response, "all done", len - 1);
	return 0;
    }
    interpret_command(command, response, len);
    return 1;
}

void handle_server_command(char *command) {
	puts(command);
return;
}

INPUT_STATE handle_input() {
	// main loop of the server: fetch commands from main terminal,interpret and handle them, return results to main terminal.
	char *command = 0;
	char **words = NULL;
	size_t clen = 0;
	/* response must be empty for the first call to serve */
	if ( getline(&command, &clen, stdin)!= -1) {
		if (command[0] == EOF) {
			return TERMINATED;
		}
		words = split_words(command);
		int i = -1;
		while (words[++i] != NULL)
			handle_server_command(words[i]);
		free_words(words);
	}
	else 
		return TERMINATED; //If getline returns control-D
	return RUNNING; // Else still running
}


int main(int argc, char *argv[]) {
    client_t *c = NULL;	    /* A client to serve */
    int started = 0;	    /* Number of clients started */
	INPUT_STATE my_state = RUNNING;

    if (argc != 1) {
	fprintf(stderr, "Usage: server\n");
	exit(1);
    }

	for (;;) {
		if (0) // If There is a thread to cleanup
		{
			//Cleanup
		}
		else if (my_state==WAITING_FOR_TERMINATIONS)
		{
			if (0) // If all threads are terminated
				my_state = RUNNING;
		}
		else if (my_state==RUNNING)
		{
			my_state = handle_input();
		}
		else if (my_state==TERMINATED&&started==0) // If there are no running threads
		{
			break;
		}
	}
	
	/*DEPRECATED
    if ((c = client_create(started++)) )  {
	client_run(c);
	client_destroy(c);
    }
	*/
	
    fprintf(stderr, "Terminating.");
    /* Clean up the window data */
    window_cleanup();
    return 0;
}
