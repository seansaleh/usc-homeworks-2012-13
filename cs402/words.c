#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

/* Replace the first newline character in with end of string ('\0') */
static inline void strip_nl(char *s) {
    char *nl = strchr(s, '\n');

    if (nl) *nl='\0';
}

/* Advance through s until a non-whitespace character is found and return a
 * pointer to it.  The end-of-string character ('\0') is not whitespace. */
static inline char *skip_white(char *s) {
    while(isspace(*s)) s++;
    return s;
}

/* Advance through s until a whitespace character or end-of-string character is
 * found and return a pointer to it.  */
static inline char *find_white(char *s) {
    while(*s != '\0' && !isspace(*s)) s++;
    return s;
}

/*Forward ref, see below */
void free_words(char **);

/* Append a copy of word to words.  Expand words if necessary.  cap points to
 * the current capacity of the words array.  It is updated if words is
 * expanded.  n is the number of words stored in it (not counting word).  If
 * there is an error, the existing word array is freed and a NULL pointer is
 * returned.  This will append a NULL pointer to words as well, but if such a
 * pointer is added, no more can be added after it, or free_words will not
 * release their memory.  */
static char **add_word(char **words, int *cap, int n, char *word) {
    if (n >= *cap )  {
	/* Add more space */
	int ncap = (*cap > 0) ? 2 * (*cap) : 2;
	char **old_words = words;
	int i;

	if ( !(words = (char **) realloc(words, ncap * sizeof(char *))) ) {
	    free_words(old_words);
	    return NULL;
	}
	for (i=n; i< ncap; i++) words[i] = NULL;
	*cap = ncap;
    }
    if (word) {
	/* Allocate space for the string and copy it in, otherwise a null
	 * pointer stays. */
	int wlen = strlen(word)+1;

	words[n] = (char *) malloc(wlen);
	strncpy(words[n], word, wlen);
    } 
    return words;
}

/* Break in_line into words and return an array of them.  A word is a string of
 * adjacent non-whitespace characters.  The resulting array is terminated with
 * a null pointer.  Calling free_words frees all the words and the array. */
char **split_words(char *in_line) {
    int linesize = strlen(in_line)+1;
    char *line = (char *) malloc(linesize);
    char *p = NULL;
    char *end;
    char **words = NULL;
    int nwords = 0;
    int wcap = 0;

    /* Copy the input line to play with it, and trim the newline, if any */
    strncpy(line, in_line, linesize);
    strip_nl(line);

    /* find the end of the line */
    for (end = line; *end != '\0'; end++)
	;

    /* Walk through the line putting islands of non-whitespace into words */
    p = line;
    while ( p != end) {
	char *q = NULL;

	p = skip_white(p);
	q = find_white(p);
	*q = '\0';
	if (!(words = add_word(words, &wcap, nwords, p))) {
	    free(line);
	    return NULL;
	}
	nwords++;
	/* Put p on the first letter of the next candidate word, unless q was
	 * the end of line */
	if ( (p = q) != end) p++;
    }

    /* Terminate with a NULL.  Note that if this fails, the main line does
     * exactly what the error handling would do.  */
    words = add_word(words, &wcap, nwords, NULL);
    free(line);
    return words;
}

/* Release an allocation of words from split_words. */
void free_words(char **words) {
    int i=0;

    for (i=0; words[i]; i++) 
	free(words[i]);
    free(words);
}


#ifdef DEBUG_WORDS
/* debugging scaffold.
 *
 * cc -g -Wall -DDEBUG_WORDS words.c
 *
 * will make a program that parses its args into words.
 */
int main(int argc, char **argv) {
    int i = 0;

    for (i =1; i < argc; i++) {
	char **words = split_words(argv[i]);
	char **p;

	printf("arg: %s\n", argv[i]);
	for (p = words; *p ; p++) 
	    printf("\t%s\n", *p);
	free_words(words);
    }
    exit(0);
}
#endif
