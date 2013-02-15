//#include "db.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

typedef struct Node {
	char *name;
	char *value;
	struct Node *lchild;
	struct Node *rchild;
	pthread_rwlock_t rwlock_node;
} node_t;

node_t head;

void interpret_command(char *, char *, int);

/* Forward declaration */
node_t *search(char *, node_t *, node_t **);

node_t head = { "", "", 0, 0 };
/*
 * Allocate a new node with the given key, value and children. Includes a rw lock.
 */
node_t *node_create(char *arg_name, char *arg_value, node_t * arg_left,
	node_t * arg_right) {
    node_t *new_node;

    new_node = (node_t *) malloc(sizeof(node_t));
    if (!new_node) return NULL;

    if (!(new_node->name = (char *)malloc(strlen(arg_name) + 1))) {
	free(new_node);
	return NULL;
    }

    if (!(new_node->value = (char *)malloc(strlen(arg_value) + 1))) {
	free(new_node->name);
	free(new_node);
	return NULL;
    }
	
	if(pthread_rwlock_init(&new_node->rwlock_node, NULL) !=0)
		{
		printf("rwlock init error, exiting \n");
		exit(1);
		}
    strcpy(new_node->name, arg_name);
    strcpy(new_node->value, arg_value);
    new_node->lchild = arg_left;
    new_node->rchild = arg_right;
    
    return new_node;
}

/* Free the data structures in node and the node itself. */
void node_destroy(node_t * node) {
    /* Clearing name and value after they are freed is defensive programming in
     * case the node_destroy is called again. */
    if (node->name) {free(node->name); node->name = NULL; }
    if (node->value) { free(node->value); node->value = NULL; }
	if (&node->rwlock_node) {pthread_rwlock_destroy(&node->rwlock_node);}
    free(node);
}

/* Find the node with key name and return a result or error string in result.
 * Result must have space for len characters. */
void query(char *name, char *result, int len) {
    node_t *target;

    target = search(name, &head, NULL); //This will read lock the target

    if (!target) {
	strncpy(result, "not found", len - 1);
	return;
    } else {
	strncpy(result, target->value, len - 1);
	pthread_rwlock_unlock(&target->rwlock_node); //unlock the returned target
	return;
    }
}

/* Insert a node with name and value into the proper place in the DB rooted at
 * head. */
int add(char *name, char *value) {
	node_t *parent;	    /* The new node will be the child of this node */
	node_t *target;	    /* The existing node with key name if any */
	node_t *newnode;    /* The new node to add */

	if ((target = search(name, &head, &parent))) { //Locks target
	    /* There is already a node with this key in the tree */
		pthread_rwlock_unlock(&target->rwlock_node); //Unlock target
	    return 0;
	} //Otherwise there is no taget, it is null, no need to unlock a nonexistant lock

	/* No idea how this could happen, but... */
	if (!parent) return 0;

	/* make the new node and attach it to parent */
	newnode = node_create(name, value, 0, 0);
	
	pthread_rwlock_wrlock(&parent->rwlock_node); //Lock parent before writing to it
	if (strcmp(name, parent->name) < 0) parent->lchild = newnode;
	else parent->rchild = newnode;
	pthread_rwlock_unlock(&parent->rwlock_node); //Unlock parent after writing to it

	return 1;
}

/*
 * When deleting a node with 2 children, we swap the contents leftmost child of
 * its right subtree with the node to be deleted.  This is used to swap those
 * content pointers without copying the data, which is unsafe if the
 * allocations are different sizes (copying "alamorgodo" into "ny" for
 * example).
 */
static inline void swap_pointers(char **a, char **b) {
    char *tmp = *b;
    *b = *a;
    *a = tmp;
}

/* Remove the node with key name from the tree if it is there.  See inline
 * comments for algorithmic details.  Return true if something was deleted. */
 //Note: lock parent before child, otherwise race condition
int xremove(char *name) {
	node_t *parent;	    /* Parent of the node to delete */
	node_t *dnode;	    /* Node to delete */
	node_t *next;	    /* used to find leftmost child of right subtree */
	node_t **pnext;	    /* A pointer in the tree that points to next so we
			       can change that nodes children (see below). */

	/* first, find the node to be removed */
	if (!(dnode = search(name, &head, &parent))) {//dnode is now read locked from msearch
	    /* it's not there */
	    return 0;
	}
	//Write lock parent, this way no one tries to add anything to mess with us
	//(Currently can't add since dnode is read locked)
	pthread_rwlock_wrlock(&parent->rwlock_node);
	//now can unlock the dnode since it can't be write locked
	pthread_rwlock_unlock(&dnode->rwlock_node);
	//Now try to write lock dnode
	pthread_rwlock_wrlock(&dnode->rwlock_node);
	//Now we have the write lock on the parent and the dnode, as is neccesary
	//We still need the write lock on whoever replaces it and its parent, if we have to do a complicated switcharoo
	
	/* we found it.  Now check out the easy cases.  If the node has no
	 * right child, then we can merely replace its parent's pointer to
	 * it with the node's left child. */
	if (dnode->rchild == 0) {
	    if (strcmp(dnode->name, parent->name) < 0)
		parent->lchild = dnode->lchild;
	    else
		parent->rchild = dnode->lchild;

	    /* done with dnode */
		pthread_rwlock_unlock(&dnode->rwlock_node); // Just to be safe
	    node_destroy(dnode);
	} else if (dnode->lchild == 0) {
	    /* ditto if the node had no left child */
	    if (strcmp(dnode->name, parent->name) < 0)
		parent->lchild = dnode->rchild;
	    else
		parent->rchild = dnode->rchild;

	    /* done with dnode */
		pthread_rwlock_unlock(&dnode->rwlock_node); // Just to be safe
	    node_destroy(dnode);
	} else {
	    /* So much for the easy cases ...
	     * We know that all nodes in a node's right subtree have
	     * lexicographically greater names than the node does, and all
	     * nodes in a node's left subtree have lexicographically smaller
	     * names than the node does. So, we find the lexicographically
	     * smallest node in the right subtree and replace the node to be
	     * deleted with that node. This new node thus is lexicographically
	     * smaller than all nodes in its right subtree, and greater than
	     * all nodes in its left subtree. Thus the modified tree is well
	     * formed. */

	    /* pnext is the address of the pointer which points to next (either
	     * parent's lchild or rchild) */
	    pnext = &dnode->rchild;
	    next = *pnext;
		/* Is it neccesary to read lock these? not sure, but there could be a race condition where a thread starts to switch while we are dong this switch?
		*  Or will that deadlock, hmmmm... hopefully testing will answer these questions */
		pthread_rwlock_rdlock(&next->rwlock_node); //Read lock as we go down looking for the correct node
	    while (next->lchild != 0) {
		    /* work our way down the lchild chain, finding the smallest
		     * node in the subtree. */
		    pnext = &next->lchild;
			pthread_rwlock_rdlock(&(*pnext)->rwlock_node);//Lock next
			pthread_rwlock_unlock(&next->rwlock_node); //Unlock was was locked
		    next = *pnext;
	    }
		//Next is already read locked, as is dnode
		pthread_rwlock_unlock(&next->rwlock_node); //read unlock
		/*Right here what happens if there is a context switch? Look here for race condition bug maybe?*/
		pthread_rwlock_wrlock(&next->rwlock_node); //write lock
	    swap_pointers(&dnode->name, &next->name);
	    swap_pointers(&dnode->value, &next->value);
	    *pnext = next->rchild;
		pthread_rwlock_unlock(&next->rwlock_node); //Unlock next
		pthread_rwlock_unlock(&dnode->rwlock_node); //Unlock Dnode
	    node_destroy(next);
    }
	
	pthread_rwlock_unlock(&parent->rwlock_node); // Unlock the parent

    return 1;
}

/* Search the tree, starting at parent, for a node containing name (the "target
 * node").  Return a pointer to the node, if found, otherwise return 0.  If
 * parentpp is not 0, then it points to a location at which the address of the
 * parent of the target node is stored.  If the target node is not found, the
 * location pointed to by parentpp is set to what would be the the address of
 * the parent of the target node, if it were there.
 * 
 * Lock logic: If next is null then it doesn't matter that we locked (error)
 * if result was next then it is up to function that gets return to unlock
 * if result was not it then when we exit our recursion we unlock
 * parent does not stay locked
 *
 * Assumptions:
 * parent is not null and it does not contain name */
node_t *search(char *name, node_t * parent, node_t ** parentpp) {

    node_t *next;
    node_t *result;
	pthread_rwlock_rdlock(&parent->rwlock_node); //Lock parent before reading it

    if (strcmp(name, parent->name) < 0) next = parent->lchild;
    else next = parent->rchild;
	pthread_rwlock_unlock(&parent->rwlock_node); //Unlock parent after reading it
	
    if (next == NULL) {
	result = NULL;
    } else {
	pthread_rwlock_rdlock(&next->rwlock_node); //Lock next before reading it
	if (strcmp(name, next->name) == 0) {
	    /* Note that this falls through to the if (parentpp .. ) statement
	     * below. */
	    result = next;
	} else {
	    /* "We have to go deeper!" This recurses and returns from here
	     * after the recursion has returned result and set parentpp */
	    result = search(name, next, parentpp);
		pthread_rwlock_unlock(&next->rwlock_node); //Unlock next since it is not our final
	    return result;
	}
    }

    /* record a parent if we are looking for one */
    if (parentpp != 0) *parentpp = parent;

    return (result);
}

/*
 * Parse the command in command, execute it on the DB rooted at head and return
 * a string describing the results.  Response must be a writable string that
 * can hold len characters.  The response is stored in response.
 */
void interpret_command(char *command, char *response, int len)
{
    char value[256];
    char ibuf[256];
    char name[256];

    if (strlen(command) <= 1) {
	strncpy(response, "ill-formed command", len - 1);
	return;
    }

    switch (command[0]) {
    case 'q':
	/* Query */
	sscanf(&command[1], "%255s", name);
	if (strlen(name) == 0) {
	    strncpy(response, "ill-formed command", len - 1);
	    return;
	}

	query(name, response, len);
	if (strlen(response) == 0) {
	    strncpy(response, "not found", len - 1);
	}

	return;

    case 'a':
	/* Add to the database */
	sscanf(&command[1], "%255s %255s", name, value);
	if ((strlen(name) == 0) || (strlen(value) == 0)) {
	    strncpy(response, "ill-formed command", len - 1);
	    return;
	}

	if (add(name, value)) {
	    strncpy(response, "added", len - 1);
	} else {
	    strncpy(response, "already in database", len - 1);
	}

	return;

    case 'd':
	/* Delete from the database */
	sscanf(&command[1], "%255s", name);
	if (strlen(name) == 0) {
	    strncpy(response, "ill-formed command", len - 1);
	    return;
	}

	if (xremove(name)) {
	    strncpy(response, "removed", len - 1);
	} else {
	    strncpy(response, "not in database", len - 1);
	}

	    return;

    case 'f':
	/* process the commands in a file (silently) */
	sscanf(&command[1], "%255s", name);
	if (name[0] == '\0') {
	    strncpy(response, "ill-formed command", len - 1);
	    return;
	}

	{
	    FILE *finput = fopen(name, "r");
	    if (!finput) {
		strncpy(response, "bad file name", len - 1);
		return;
	    }
	    while (fgets(ibuf, sizeof(ibuf), finput) != 0) {
		interpret_command(ibuf, response, len);
	    }
	    fclose(finput);
	}
	strncpy(response, "file processed", len - 1);
	return;

    default:
	strncpy(response, "ill-formed command", len - 1);
	return;
    }
}
