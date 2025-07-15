/* pcmds.c */
/* $Id: pcmds.c,v 1.5 1993/08/16 01:56:36 nils Exp $ */

#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include  "credits.h"

#include "db.h"
#include "config.h"
#include "externs.h"
#include "net.h"

char *base_date = BASE_DATE;
char *upgrade_date = UPGRADE_DATE;
int day_release = DAY_RELEASE;

extern time_t muse_up_time;
extern time_t muse_reboot_time;

static char *get_version()
{
  static char buf[1000];
  static int abs_day = 0;

  if (abs_day == 0)
  {
    abs_day = (atoi(&upgrade_date[6]) - 91) * 372;
    abs_day += (atoi(upgrade_date) - 1) * 31;
    abs_day += atoi(&upgrade_date[3]);

    abs_day -= (atoi(&base_date[6]) - 91) * 372;
    abs_day -= (atoi(base_date) - 1) * 31;
    abs_day -= atoi(&base_date[3]);
  }
  sprintf(buf, "%s.%d.%d%s%s", BASE_VERSION, abs_day, day_release - 1
#ifdef MODIFIED
	  ,"M",
#else
#ifdef BETA
	  ," beta",
#else
	  ,"",
#endif
#endif
    BASE_REVISION
    );
  return buf;
}
void do_version(player)
dbref player;
{

  notify(player, tprintf("%s Version Information:", muse_name));
  notify(player, tprintf("   Last Code Upgrade: %s", UPGRADE_DATE));

  notify(player, tprintf("   Version reference: %s",
			 get_version()));
  notify(player, tprintf("   DB Format Version: v%d", DB_VERSION));
}

void do_uptime(player)
dbref player;
{
  int index = 28;
  long a, b = 0, c = 0, d = 0, t;
  static char pattern[] = "%d days, %d hrs, %d min and %d sec";
  char format[50];

  /* get current time and total run time */
  a = now - muse_up_time;

  /* calc seconds */
  t = a / 60L;
  if (t > 0L)
  {
    b = a % 60L;
    a = t;
    if (a > 0L)
      index = 17;

    /* calc minutes */
    t = a / 60L;
    if (t > 0)
    {
      c = b;
      b = a % 60L;
      a = t;
      if (a > 0)
	index = 9;

      /* calc hours and days */
      t = a / 24L;
      if (t > 0)
      {
	d = c;
	c = b;
	b = a % 24L;
	a = t;
	if (a > 0)
	  index = 0;
      }
    }
  }

  /* rig up format for operation time output */
  sprintf(format, "%%s %s", &pattern[index]);

  notify(player, tprintf("%s runtime stats:", muse_name));
  notify(player, tprintf("    Muse boot time..: %s",
			 mktm(muse_up_time, "D", player)));
  notify(player, tprintf("    Last reload.....: %s",
			 mktm(muse_reboot_time, "D", player)));
  notify(player, tprintf("    Current time....: %s",
			 mktm(now, "D", player)));
  notify(player, tprintf(format, "    In operation for:",
			 (int)a, (int)b, (int)c, (int)d));
}

/* uptime stuff */
static int cpos = 0;
static int qcmdcnt[60 * 5];
static int pcmdcnt[60 * 5];
void inc_qcmdc()
{
  qcmdcnt[cpos]++;
  pcmdcnt[cpos]--;		/* we're gunna get this again in
				   process_command */
}
static void check_time()
{
  static struct timeval t, last;
  static struct timezone tz;

  gettimeofday(&t, &tz);
  while (t.tv_sec != last.tv_sec)
  {
    if (last.tv_sec - t.tv_sec > 60 * 5 || t.tv_sec - last.tv_sec > 60 * 5)
      last.tv_sec = t.tv_sec;
    else
      last.tv_sec++;
    cpos++;
    if (cpos >= 60 * 5)
      cpos = 0;
    qcmdcnt[cpos] = pcmdcnt[cpos];
  }
}
void inc_pcmdc()
{
  check_time();
  pcmdcnt[cpos]++;
}

void do_cmdav(player)
dbref player;
{
  int len;

  notify(player, "Seconds  Player cmds/s   Queue cmds/s    Tot cmds/s");
  for (len = 5; len != 0; len = ((len == 5) ? 30 : ((len == 30) ? (60 * 5) : 0)))
  {
    int i;
    int cnt;
    double pcmds = 0, qcmds = 0;
    char buf[1024];

    i = cpos - 1;
    cnt = len;
    while (cnt)
    {
      if (i <= 0)
	i = 60 * 5 - 1;
      pcmds += pcmdcnt[i];
      qcmds += qcmdcnt[i];
      i--;
      cnt--;
    }
    sprintf(buf, "%-8d %-14f  %-14f  %f", len, pcmds / len, qcmds / len, (pcmds + qcmds) / len);
    notify(player, buf);
  }
}

#ifdef ALLOW_EXEC
void do_exec(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  dbref thing;
  int fd;
  int mypipe[2];
  int forkno;

  char buf[1024];
  char *bufptr;			/* for seperating arg2 by ;'s */
  char *curptr;

  FILE *f;
  char fbuf[1024];		/* for reading in the @exec perms file */

  return;
  thing = match_thing(player, arg1);
  if (thing == NOTHING)
    return;

  if (!power(player, POW_EXEC))
  {
    notify(player, perm_denied());
    return;
  }

  strcpy(bufptr = buf, arg2);
  while ((curptr = parse_up(&bufptr, ';')))
  {
    FILE *f;

    f = fopen(EXEC_CONFIG, "r");
    while (fgets(fbuf, 1000, f))
    {
      if (*fbuf != 'g' || fbuf[1] != ' ')
	continue;
      fbuf[strlen(fbuf) - 1] = '\0';
      if (wild_match(fbuf + 2, curptr))
	goto okmatch;
    }
    fclose(f);
    notify(player, "Permission denied: illegal command");
    return;
  okmatch:
    fclose(f);
  }

  pipe(mypipe);
  forkno = vfork();
  if (forkno == -1)
  {
    notify(player, "Fork failed.");
    return;
  }
  if (forkno > 0)
  {				/* parent */
    outgoing_setupfd(thing, mypipe[0]);
    close(mypipe[1]);
    return;
  }
  /* child */
  {
    char *envptr[2];
    char envbuf[1010];

    *envbuf = '\0';
    envptr[0] = envptr[1] = NULL;

    f = fopen(EXEC_CONFIG, "r");
    while (fgets(fbuf, 1000, f))
    {
      if (*fbuf != 'p' || fbuf[1] != ' ')
	continue;
      fbuf[strlen(fbuf) - 1] = '\0';
      sprintf(envbuf, "PATH=%s", fbuf + 2);
    }
    fclose(f);

    fd = open("/dev/null", O_RDONLY, 0);
    dup2(fd, 0);		/* we don't have a stdin. */
    dup2(mypipe[1], 1);		/* stdout and */
    dup2(mypipe[1], 2);		/* stderr go back through to the executer. */
    if (*envbuf)
      envptr[0] = envbuf;
    execle(EXEC_SHELL, EXEC_SHELLAV0, "-c", arg2, NULL, envptr);
    write(1, "Couldn't get sh!!\n", sizeof("Couldn't get sh!!\n"));
    _exit(1);
  }
}
#endif /* ALLOW_EXEC */

void do_at(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  dbref newloc, oldloc;
  static int depth = 0;

  oldloc = db[player].location;
  if ((Typeof(player) != TYPE_PLAYER && Typeof(player) != TYPE_THING)
      || oldloc == NOTHING || depth > 10)
  {
    notify(player, perm_denied());
    return;
  }
  newloc = match_controlled(player, arg1, POW_TELEPORT);
  if (newloc == NOTHING)
    return;
  db[oldloc].contents = remove_first(db[oldloc].contents, player);
  PUSH(player, db[newloc].contents);
  db[player].location = newloc;
  depth++;
  process_command(player, arg2, player);
  depth--;
  /* the world has changed, possibly. */
  newloc = db[player].location;
  db[newloc].contents = remove_first(db[newloc].contents, player);
  PUSH(player, db[oldloc].contents);
  db[player].location = oldloc;
}

dbref as_from = NOTHING;
dbref as_to = NOTHING;

void do_as(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  dbref who;
  static int depth = 0;

  who = match_controlled(player, arg1, POW_MODIFY);
  if (who == NOTHING)
    return;

  if (depth > 0)
  {
    notify(player, perm_denied());
    return;
  }

  if ((db[who].owner != db[player].owner))
    log_force(tprintf("%s uses @as on %s to execute: %s",
         unparse_object_a(player, player), unparse_object_a(who, who),
                      arg2));

  as_from = who;
  as_to = player;
  depth++;
  process_command(who, arg2, player);
  depth--;
  as_to = as_from = NOTHING;
}
