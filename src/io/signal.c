#include <signal.h>
#include <sys/signal.h>
#include <wait.h>
#include "config.h"
#include "externs.h"

static signal_type dump_status P((int));
static signal_type do_sig_reboot P((int));
static signal_type do_sig_shutdown P((int));
static signal_type bailout P((int));




#ifdef SIGEMT
int floating_x = 0;
static void handle_exception()
{
  floating_x = 1;
}
#endif

static signal_type reaper(i)
int i;
{
  int stat;

  while (wait3(&stat, WNOHANG, 0) > 0) ;
  com_send(dbinfo_chan, "|Y!+*| Database dump complete.");
  return;
}

void set_signals()
{
//  struct sigaction *act = malloc(sizeof(struct sigaction));
  struct sigaction *act;
  SAFE_MALLOC(act, struct sigaction, 1);

  /* we don't care about SIGPIPE, we notice it in select() and write() */
  signal(SIGPIPE, SIG_IGN);

  /* nasty signal handling nonsense for getting rid of zombies. */
  sigaction(SIGCHLD, NULL, act);
  act->sa_handler = reaper;
  act->sa_flags = SA_RESTART;
  sigaction(SIGCHLD, act, NULL);
  SAFE_FREE(act);
  signal(SIGCHLD, SIG_IGN);

  /* standard termination signals */
  signal(SIGINT, bailout);

#ifdef DEBUG
  /* catch these because we might as well */
     signal (SIGQUIT, bailout); 
     signal (SIGILL, bailout); 
     signal (SIGTRAP, bailout); 
     signal (SIGIOT, bailout); 
     signal (SIGEMT, bailout); 
     signal (SIGFPE, bailout); 
     signal (SIGBUS, bailout); 
     signal (SIGSEGV, bailout); 
     signal (SIGSYS, bailout); 
     signal (SIGTERM, bailout); 
     signal (SIGXCPU, bailout); 
     signal (SIGXFSZ, bailout); 
     signal (SIGVTALRM, bailout); 
     signal (SIGUSR2, bailout); 
#endif

  /* status dumper (predates "WHO" command) */
  signal(SIGUSR1, dump_status);

  signal(SIGHUP, do_sig_reboot);
  signal(SIGTERM, do_sig_shutdown);
#ifdef SIGEMT
  signal(SIGEMT, handle_exception);
#endif
}

static signal_type do_sig_reboot(i)
int i;
{
  log_sensitive("REBOOT: by external source");
  log_important("REBOOT: by external source");
  exit_status = 1;
  shutdown_flag = 1;
#ifdef void_signal_type
  return;
#else
  return 0;
#endif
}

static signal_type do_sig_shutdown(i)
int i;
{
  log_sensitive("SHUTDOWN: by external source");
  log_important("SHUTDOWN: by external source");
  exit_status = 0;
  shutdown_flag = 1;
#ifdef void_signal_type
  return;
#else
  return 0;
#endif
}

static signal_type bailout(sig)
int sig;
{
  char message[1024];

  sprintf(message, "BAILOUT: caught signal %d", sig);
  panic(message);
  exit_nicely(136);
#ifdef void_signal_type
  return;
#else
  return 0;
#endif
}

static signal_type dump_status(i)
int i;
{
  struct descriptor_data *d;
  long now;

  now = time(NULL);
  fprintf(stderr, "STATUS REPORT:\n");
  for (d = descriptor_list; d; d = d->next)
  {
    if (d->state == CONNECTED)
    {
      fprintf(stderr, "PLAYING descriptor %d player %s(%ld)",
	      d->descriptor, db[d->player].name, d->player);

      if (d->last_time)
	fprintf(stderr, " idle %ld seconds\n",
		now - d->last_time);
      else
	fprintf(stderr, " never used\n");
    }
    else
    {
      fprintf(stderr, "CONNECTING descriptor %d", d->descriptor);
      if (d->last_time)
	fprintf(stderr, " idle %ld seconds\n",
		now - d->last_time);
      else
	fprintf(stderr, " never used\n");
    }
  }
#ifdef void_signal_type
  return;
#else
  return 0;
#endif
}
