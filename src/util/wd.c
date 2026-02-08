/* wd.c - watchdog */
/* $Id: wd.c,v 1.7 1993/10/18 02:29:24 nils Exp $ */

#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "config.h"
#include "credits.h"
#include "externs.h"
#include "config.c"

/* #if defined(SYSV) || defined(HPUX) || defined(SYSV_MAYBE) */
#define  mklinebuf(fp)	setvbuf(fp, NULL, _IOLBF, 0)
/* #else */
/* #   define  mklinebuf(fp)    setlinebuf(fp) */
/* #endif */

#define  DEF_MODE	0644

#define  MUSE_PROGRAM	"../bin/netmuse"
#include <stdlib.h>
int main(int, char **);
void wd_init_signals(void);
void shutdown_wd(void);
void wd_init_io(void);
void restart_loop(char **);
void analyze(int, int);
void wd_sig_handler(int);
int muse_pid;

/* void remove_temp_dbs(void); */

int main(int argc, char *argv[])
{
  (void)argc;  /* unused */
  switch (fork())
  {
  case 0:
    setpgrp();
    break;
  case -1:
    perror("fork");
    break;
  default:
    exit(0);
    return 0;
  }
  wd_init_io();
  printf("------------------------------------\n");
  printf("Watchdog (wd) online (pid=%d). woof.\n", getpid());

  wd_init_signals();

  restart_loop(argv);

  exit(0);
}

void wd_init_signals(void)
{
  signal(SIGHUP, wd_sig_handler);
  signal(SIGTERM, wd_sig_handler);
  signal(SIGUSR1, wd_sig_handler);
}

void wd_init_io(void)
{
#ifndef fileno
  /* sometimes stdio.h #defines this */
  extern int fileno (FILE *);

#endif
  int fd;

  /* close standard input */
  fclose(stdin);

  /* open a link to the log file */
  fd = open(wd_logfile, O_WRONLY | O_CREAT | O_APPEND, DEF_MODE);
  if (fd < 0)
  {
    perror("open()");
    fprintf(stderr, "Can't open %s for writing.\n", wd_logfile);
    exit(1);
  }

  /* attempt to convert standard output to logfile */
  close(fileno(stdout));
  if (dup2(fd, fileno(stdout)) == -1)
  {
    perror("dup2()");
    fprintf(stderr, "Error converting standard output to logfile.\n");
  }
  mklinebuf(stdout);

  /* attempt to convert standard error to logfile */
  close(fileno(stderr));
  if (dup2(fd, fileno(stderr)) == -1)
  {
    perror("dup2()");
    printf("Error converting standard error to logfile.\n");
  }
  mklinebuf(stderr);

  /* this logfile reference is no longer needed */
  close(fd);
}

void restart_loop(char *argv[])
{
  int pid, status;
  struct stat statbuf;
  int now, lasttime = 0;

  for (;;)
  {
    now = time(NULL);
    if (lasttime + 300 > now)
      sleep(300);
    lasttime = now;
    if (stat("logs", &statbuf) < 0)
    {
      puts("Creating logs directory.");
      mkdir("logs", 0755);
    }
    else if ((statbuf.st_mode & S_IFMT) != S_IFDIR)
    {
      puts("'logs' isn't a directory.");
      shutdown_wd();
    }
    printf("Batching off command logs. Spying is fun. >:)\n");
    unlink("cmd_crash");
    system("(echo --;date) >cmd_crash");
    system("cat logs/commands* >> cmd_crash");
/*     system("cat logs/commands~ logs/commands >> cmd_crash"); */
#ifdef TECH_EMAIL
    /* TODO: Replace this with the email mechanism in comm/email.c
     * instead of shelling out to sendmail directly. Also fix sprintf
     * to snprintf when this is reworked. */
    {
      char buf[1024];

      snprintf(buf, sizeof(buf), "/usr/lib/sendmail %s <cmd_crash", TECH_EMAIL);
      system(buf);
    }
#endif
/*    system("/usr/lib/sendmail newmark <cmd_crash");  */

    printf("Attempting to startup the MUSE\n");
    system("date");
    /* spawn a new process */
    muse_pid = fork();
    if (muse_pid < 0)
    {
      perror("fork()");
      fprintf(stderr, "Error spawning muse program\n");
      shutdown_wd();
    }

    /* child process executes this if-loop */
    if (muse_pid == 0)
    {
      argv[0] = MUSE_PROGRAM;
      /* execute the MUSE program */
      if (execvp(MUSE_PROGRAM, argv) < 0)
      {
	perror("execvp()");
	fprintf(stderr, "Error spawning muse program\n");
	shutdown_wd();
      }
      /* this should NEVER happen */
      /* there is NO return from execvp except failure */
      exit(0);
    }

    /* parent process continues here... */
    printf("Sucessful startup of MUSE.  muse-pid=%d\n", muse_pid);

    /* wait for MUSE to stop executing */
    pid = wait(&status);
    if (pid < 0)
    {
      perror("wait()");
      fprintf(stderr, "Error during call to wait()\n");
      shutdown_wd();
    }

    /* analyze reason for death of MUSE */
    analyze(pid, status);
  }
}

void analyze(int pid, int status)
{
  int sig;

  if (pid != muse_pid)
  {
    fprintf(stderr, "wait() returned information on the wrong process\n");
    fprintf(stderr, "Return pid=%d, Expected pid=%d\n", pid, muse_pid);
    shutdown_wd();
  }
  else if ((status & 0377) == 0177)
  {
    sig = (status >> 8) & 0377;
    fprintf(stderr, "Error, MUSE program suspended by signal %d\n", sig);
    shutdown_wd();
  }
  else if ((status & 0377) != 0)
  {
    sig = status & 0177;
    printf("MUSE program terminated by signal %d\n", sig);
  }
  else
  {
    status = (status >> 8) & 0377;
    printf("MUSE program terminated due to exit() with status=%d\n",
	   status);
    if (status != 1)
      shutdown_wd();
  }
}

void wd_sig_handler(int sig)
{
  printf("Recieved signal %d.\n", sig);
  shutdown_wd();
}

void shutdown_wd(void)
{
  printf("Shutting down watchdog program...\n");
  exit(0);
}
void setlinebuf(FILE *f)
{
  setbuf(f, NULL);
}
