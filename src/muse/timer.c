/* timer.c */
/* $Id: timer.c,v 1.9 1993/09/06 19:33:01 nils Exp $ */

/* Subroutines for timed events */
#include "copyright.h"

#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#ifdef XENIX
#include <sys/signal.h>
#else
#include <signal.h>
#endif /* xenix */

#include "db.h"
#include "config.h"
#include "interface.h"
#include "match.h"
#include "externs.h"
static int alarm_triggered = 0;
extern char ccom[1024];


extern void trig_atime()
{
  dbref thing;

  for(thing = 0;thing < db_top;++thing)
    if(*atr_get(thing, A_ATIME))
      did_it(thing, thing, NULL, NULL, NULL, NULL, A_ATIME);
}


static signal_type alarm_handler(i)
int i;
{
  alarm_triggered = 1;
  signal(SIGALRM, alarm_handler);
#ifdef void_signal_type
  return;
#else
  return 0;
#endif
}

void init_timer()
{
  signal(SIGALRM, alarm_handler);
  signal(SIGHUP, alarm_handler);
  alarm(1);
}

void dispatch()
{
  static int ticks = 0;
  ticks++;

  /* this routine can be used to poll from interface.c */
  if (!alarm_triggered)
    return;
  alarm_triggered = 0;
  do_second();
#ifdef RESOCK
  if (!(ticks % 300))
    resock();
#endif

  if (!(ticks % fixup_interval))
  {
    log_command("Dbcking...");

    strcpy(ccom, "dbck");
    do_dbck(root);
    log_command("...Done.");
  }
  /* Database dump routines */


  if (!(ticks % dump_interval))
  {
    log_command("Dumping.");
    strcpy(ccom, "dump");
    fork_and_dump();
  }


  {
    int i;

    for (i = 0; i < ((db_top / 300) + 1); i++)
      update_bytes();
  }


#ifdef PURGE_OLDMAIL
  {
    /* Stale mail deletion. */
    static int mticks = -1;
  
    if (mticks == -1) mticks = old_mail_interval;
  
    if (!mticks--) 
    {
      log_command("Deleting old mail.\n");
      mticks = old_mail_interval;
      strcpy(ccom, "mail");
      clear_old_mail();
      next_mail_clear = now + old_mail_interval;
    }
  }
#endif /* PURGE_OLDMAIL */


/* Trigger @atime */
  if (!(ticks % 300))
  {
    trig_atime();
  }


  if(!(ticks%60))
  {
    check_newday();
  }

/* Boot unidle guests and unconnected descriptors */
  trig_idle_boot();


  strcpy(ccom, "garbage");
  do_incremental();		/* handle incremental garbage collection */
  run_topology();
  /* reset alarm */
  alarm(1);
}

