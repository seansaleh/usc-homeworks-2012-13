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
#include <stdbool.h>

/* the encapsulation of a client thread, i.e., the thread that handles
 * commands from clients */
typedef struct Client {
	pthread_t thread;
	window_t *win;
	bool delete; //Flag to tell server to delete, only gets written by client while locked out of server reading it, or during intilization
} client_t;

typedef enum INPUT_STATE { //enum for different states of the server
	RUNNING,
	WAITING_FOR_TERMINATIONS,
	TERMINATED
	} INPUT_STATE;
	
typedef struct NodeLinked { //Linkedlist for keeping track of clients
	struct NodeLinked *next;
	client_t* self;
} node_l;

/* initilize linked list */
node_l* ll_head = NULL;

/*variable and mutex for knowing how many items to delete from linked list*/
int to_delete = 0; // Global variable that is always write locked
pthread_mutex_t mutex_to_delete = PTHREAD_MUTEX_INITIALIZER;

/*Mutex and condition for pausing all and going*/
pthread_mutex_t mutex_waiting = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_all_wait = PTHREAD_COND_INITIALIZER;
bool wait_all = false;

int started = 0;	  /* Number of clients started, just for naming purposes */

/* Interface with a client: get requests, carry them out and report results */
void *client_run(void *);
/* Interface to the db routines.  Pass a command, get a result */
int handle_command(char *, char *, int len);

/* 
 * add an item to a linked list
 */
void push(client_t* self) {
node_l *this = (node_l *) malloc(sizeof(node_l));
this->self = self;
this->next = ll_head;
ll_head = this;
 }

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
	new_Client->delete = false;
	


    /* Creates a window and set up a communication channel with it */
    if (!(new_Client->win = window_create(title))) { //If I don't get anything
		free(new_Client);
		return NULL;
    }
	
	/*Create thread*/
	pthread_t thread;
	new_Client->thread = thread;
	if (!pthread_create(&thread, NULL, client_run, new_Client)) {
		pthread_detach(thread);
		return new_Client;
	}
	else { //there were errors
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

	new_Client->delete = false;
	
	/* Creates a window and set up a communication channel with it */
    if (!(new_Client->win = nowindow_create(in, outf))) { //If I don't get anything
		free(new_Client);
		return NULL;
    }
	
	/*Create thread*/
	pthread_t thread;
	new_Client->thread = thread;
	if (!pthread_create(&thread, NULL, client_run, new_Client)) {
		pthread_detach(thread);
		return new_Client;
	}
	else { //there were errors
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
		if (wait_all) {
			pthread_mutex_lock(&mutex_waiting);
			pthread_cond_wait(&cond_all_wait, &mutex_waiting);
			pthread_mutex_unlock(&mutex_waiting);
		}
	    handle_command(command, response, sizeof(response));
	}
	//Code to tell server to delete me
	pthread_mutex_lock(&mutex_to_delete);
	client->delete = true;
	to_delete++;
	pthread_mutex_unlock(&mutex_to_delete);
	puts("Press enter to close any extra windows & threads");
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


INPUT_STATE handle_input() {
	// main loop of the server: fetch commands from main terminal,interpret and handle them, return results to main terminal.
	char *command = 0;
	char **words = NULL;
	client_t *c = NULL;
	size_t clen = 0;
	/* response must be empty for the first call to serve */
	if ( getline(&command, &clen, stdin)!= -1) {
		if (command[0] == EOF) {
			return TERMINATED;
		}
		words = split_words(command);
		int i = -1;
		while (words[++i] != NULL)
		{
			if (!strcmp(words[i],"e")) {
				if ((c = client_create(started++)))
				{
					printf("Just created a windowed client\n");
					push(c);
				}
			}
			else if (!strcmp(words[i],"E")) {
				
				if ((c = client_create_no_window(words[i+1], words[i+2])))
				{
					printf("Just created a nonwindowed client\n");
					push(c);
				}
				if (words[i+1] != NULL&&words[i+2] != NULL)
					i+=2;
				else
					break;
			}
			else if (!strcmp(words[i], "s")) {
				pthread_mutex_lock(&mutex_waiting);
				wait_all = true;
				pthread_mutex_unlock(&mutex_waiting);
			}
			else if (!strcmp(words[i], "g")) {
				pthread_mutex_lock(&mutex_waiting);
				if (wait_all) {
					wait_all = false;
					pthread_cond_broadcast(&cond_all_wait);
				}
				pthread_mutex_unlock(&mutex_waiting);
			}
			else if (!strcmp(words[i], "w")) {
				pthread_mutex_lock(&mutex_waiting);
				if (wait_all) {
					wait_all = false;
					pthread_cond_broadcast(&cond_all_wait);
				}
				pthread_mutex_unlock(&mutex_waiting);
				return WAITING_FOR_TERMINATIONS;
			}
		}
		free_words(words);
	}
	else 
		return TERMINATED; //If getline returns control-D
	return RUNNING; // Else still running
}


int main(int argc, char *argv[]) {
    //client_t *c = NULL;	    /* A client to serve */
	INPUT_STATE my_state = RUNNING;

    if (argc != 1) {
	fprintf(stderr, "Usage: server\n");
	exit(1);
    }


	for (;;) {
		if (to_delete) // If there is a thread to cleanup
		{
			node_l** prev_ref = &ll_head;
			node_l* current = ll_head;
			pthread_mutex_lock(&mutex_to_delete);
			while (to_delete>0 && current)
			{
				if (current->self->delete)
				{
					*prev_ref = current->next;
					//No need to join here, since this deletes the thread and its memory
					client_destroy(current->self);
					//puts("Just destroyed a windowed client");
					to_delete--;
					current = *prev_ref;
				}
				else 
				{
					prev_ref = &current->next;
					current = current->next;
				}
			}
			//This is an error state
			if (current == NULL && to_delete>0) {
				to_delete = 0;
				puts("Error! to_delete was not 0 when trying to delete objects, indicates race condition");
			}
			//Cleanup
			pthread_mutex_unlock(&mutex_to_delete);
		}
		else if (my_state==WAITING_FOR_TERMINATIONS)
		{
			if (!ll_head) // If all threads are terminated
				my_state = RUNNING;
		}
		else if (my_state==RUNNING)
		{
			my_state = handle_input();
		}
		else if (my_state==TERMINATED&&!ll_head) // If there are no running threads
		{
			break;
		}
		else
			sleep (0);
	}
	
	/*DEPRECATED
    if ((c = client_create(started++)) )  {
	//client_run(c);
	client_destroy(c);
    }
	/*/
	
    fprintf(stderr, "Terminating.");
    /* Clean up the window data */
    window_cleanup();
	/* Clean up mutex */
	pthread_mutex_destroy(&mutex_to_delete);
	pthread_mutex_destroy(&mutex_waiting);
	pthread_cond_destroy(&cond_all_wait);
    return 0;
}
