#include "externs.h"

/* From sock.c */
extern void open_sockets P((void));
extern void close_sockets P((void));
extern int make_socket P((int));
extern void shutdownsock P((struct descriptor_data *));
extern struct descriptor_data *initializesock P((int, struct sockaddr_in *, char *, enum descriptor_state));
extern void make_nonblocking P((int));
extern struct descriptor_data *new_connection P((int));
extern void clearstrings P((struct descriptor_data *));
extern void freeqs P((struct descriptor_data *));
extern int check_lockout P((struct descriptor_data *, char *, char *)); 


extern int exit_status;
extern int ndescriptors;
extern int nologins;
extern int maxd;


extern time_t muse_up_time;
extern time_t muse_reboot_time;
extern int sock;
extern int shutdown_flag;



#define MALLOC(result, type, number) do { if (!((result) = (type *) malloc ((number) * sizeof (type)))) panic("Out of memory"); } while (0)

#define FREE(x) (free((void *) x))

