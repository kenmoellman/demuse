#include "externs.h"

/* From sock.c */
extern void open_sockets (void);
extern void close_sockets (void);
extern int make_socket (int);
extern void shutdownsock (struct descriptor_data *);
extern struct descriptor_data *initializesock (int, struct sockaddr_in *, char *, enum descriptor_state);
extern void make_nonblocking (int);
extern struct descriptor_data *new_connection (int);
extern void clearstrings (struct descriptor_data *);
extern void freeqs (struct descriptor_data *);
extern int check_lockout (struct descriptor_data *, char *, char *); 


extern int exit_status;
extern int ndescriptors;
extern int nologins;
extern int maxd;
extern int sock;



extern time_t muse_up_time;
extern time_t muse_reboot_time;
extern int sock;
extern int shutdown_flag;



//#define MALLOC(result, type, number) do { if (!((result) = (type *) malloc ((number) * sizeof (type)))) panic("Out of memory"); } while (0)

//#define FREE(x) (free((void *) x))

