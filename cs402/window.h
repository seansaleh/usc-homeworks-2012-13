typedef struct window {
	FILE *in;
	FILE *out;
	int pid;
	char *ififo;
	char *ofifo;
	int echo;
} window_t;

window_t *window_create(char *);
window_t *nowindow_create(char *, char *);
void window_destroy(window_t *);
int serve(window_t *, char *, char **, size_t*);
void window_cleanup();
