/* io/io_globals.c - Storage allocation ONLY, no initialization */

#include "config.h"
#include "externs.h"
#include <time.h>

/* Socket file descriptors */
int sock;
int reserved;
int maxd;

/* Descriptor tracking */
int ndescriptors;

/* Shutdown coordination */
int shutdown_flag;
int exit_status;
int sig_caught;

/* Login restrictions */
int nologins;
int restrict_connect_class;

/* Timing */
time_t muse_up_time;
time_t muse_reboot_time;
time_t now;

/* WHO display globals */
char motd[2048];
char motd_who[11];

/* Command buffer for logging */
char ccom[1024];
dbref cplr;

void init_io_globals(void)
{
  sock = -1;
  reserved = -1;
  maxd = 0;
  ndescriptors = 0;
  shutdown_flag = 0;
  exit_status = 136;
  sig_caught = 0;
  nologins = 0;
  restrict_connect_class = 0;
  cplr = NOTHING;
  ccom[0] = '\0';
  motd[0] = '\0';
  motd_who[0] = '\0';
}
