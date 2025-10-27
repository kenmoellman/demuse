/* game.c */
/* $Id: game.c,v 1.40 1994/02/18 22:40:50 nils Exp $ */
 
int ndisrooms;

#if defined(SYSV) || defined(AIX_UNIX) || defined(SYSV_MAYBE)
#define vfork fork
#endif
#define my_exit exit

#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#ifdef XENIX
#include <sys/signal.h>
#else
#include <signal.h>
#include <sys/wait.h>
#endif /* xenix */

#include "db.h"
#include "config.h"
#include "interface.h"
#include "match.h"
#include "admin.h"
#include "externs.h"
#include "credits.h"

/* declarations */
char dumpfile[200];
long epoch = 0;
int depth = 0;			/* excessive recursion prevention */
extern dbref cplr;
int unsafe = 0;

static int atr_match P((dbref, dbref, int, char *));
static void no_dbdump P((void));
static int list_check P((dbref, dbref, int, char *));
static void notify_internal P((dbref, char *, int));

static void do_dump(player)
dbref player;
{
  if (power(player, POW_DB))
  {
    notify(player, "Database dumped.");
    fork_and_dump();
  }
  else
  {
    notify(player, perm_denied());
  }
}

extern char ccom[1024];

/* print out stuff into error file */
void report()
{
  char repbuf[5000];

  /* fprintf(stderr,"Command:%s depth:%d\n",ccom,depth);fflush(stderr); if
     ((cplr>0) && (cplr<db_top)) fprintf(stderr,"Player #%d location
     #%d\n",cplr,db[cplr].location); fflush(stderr); */
  log_error("*** Reporting position ***");
  sprintf(repbuf, "Depth: %d Command: %s", depth, ccom);
  log_error(repbuf);
  sprintf(repbuf, "Player: %ld location: %ld", cplr, db[cplr].location);
  log_error(repbuf);
  log_error("**************************");
}

static void do_purge(player)
dbref player;
{
  if (power(player, POW_DB))
  {
    fix_free_list();
    notify(player, "Purge complete.");
  }
  else
    notify(player, perm_denied());
}

/* this is where des_info points */
void dest_info(thing, tt)
dbref thing;
dbref tt;
{
  char buff[1024];

  if (thing == NOTHING)
  {
    if (db[tt].name)
    {
      sprintf(buff, "You own a disconnected room, %s(#%ld)", db[tt].name, tt);
      notify(db[tt].owner, buff);
    }
    else
    {
      report();
      log_error("No name for room.");
    }
    return;
  }
  switch (Typeof(thing))
  {
  case TYPE_ROOM:		/* Tell all players room has gone away */
    notify_in(thing, 0, tprintf("%s, %s",
				"The floor disappears under your feet",
				"You fall through NOTHINGness and then:"));
    break;
  case TYPE_PLAYER:		/* Show them where they arrived */
    enter_room(thing, HOME);
    break;
  }
}

dbref speaker = NOTHING;

static void notify_nopup(player, msg)
dbref player;
char *msg;
{
  static char buff[2148], *d;

  if (player < 0 || player >= db_top)
    return;
  if (depth++ > 7)
  {
    depth--;
    return;
  }
  if (db[player].owner != player)
  {
    strcpy(buff, msg);
    if (*(d = atr_get(player, A_LISTEN)) &&
	wild_match(d, buff))
    {
      if (speaker != player)
	did_it(speaker, player, 0, NULL, 0, NULL, A_AHEAR);
      else
	did_it(speaker, player, 0, NULL, 0, NULL, A_AMHEAR);
      did_it(speaker, player, 0, NULL, 0, NULL, A_AAHEAR);
      /* also pass the message on */
      /* Note: not telling player protects against two forms of recursion */
      /* player doesn't tell itself (as container) or as contents */
      /* using teleport it is possible to create a recursive loop */
      /* but this will be terminated when the depth variable exceeds 30 */
      if (db[speaker].location != player)
	notify_in(player, player, buff);
    }
    /* now check for multi listeners */
    if (speaker != player)
      atr_match(player, speaker, '!', msg);
  }
  depth--;
}

void notify_all(arg, exception, nowall)
  char *arg;
  dbref exception;
  int nowall;
{
  struct descriptor_data *d;
  char *buf;

  buf = tprintf("%s\n",arg);

  for (d = descriptor_list; d; d = d->next)
  {  
    if (d->state != CONNECTED)
      continue;
    if (Typeof(d->player) != TYPE_PLAYER)
      continue;
    if (nowall && (db[d->player].flags & PLAYER_NO_WALLS))
      continue;
    if (db[d->player].flags & PLAYER_NOBEEP)
    { 
      if (db[d->player].flags & PLAYER_ANSI)
        queue_string(d, parse_color_nobeep(buf, d->pueblo));
      else
        queue_string(d, strip_color_nobeep(buf));
    }
    else
    { 
      if (db[d->player].flags & PLAYER_ANSI)
        queue_string(d, parse_color(buf, d->pueblo));
      else
        queue_string(d, strip_color(buf));
    }
  }


/*  the old way
  dbref list[100];
  int i, ctr = 0;

  for (d = descriptor_list; d; d = d->next)
  {  
    if(d->state != CONNECTED || d->player == exception)
      continue;
    for(i = 0;i < ctr;++i)
      if(list[i] == d->player)
        break;
    if(i == ctr)
    { 
      notify(d->player, arg);
      list[ctr++] = d->player;
    }
  } 
*/
 
}

void notify(player, msg)
dbref player;
const char *msg;
{
  notify_internal(player, msg, 1);
}

void notify_noc(player, msg)
dbref player;
char *msg;
{
  notify_internal(player, msg, 0);
}

static void notify_internal(player, msg, color)
dbref player;
char *msg;
int color;
{
  static char buff[2148];

  if ((player < 0) || (player >= db_top))
    return;
  if (depth++ > 7)
  {
    depth--;
    return;
  }
  if (color)
    raw_notify(player, msg);
  else
    raw_notify_noc(player, msg);
  if (db[player].flags & PUPPET && db[player].owner != player)
  {
    sprintf(buff, "%s> %s", db[player].name, msg);
    if (color)
      raw_notify(db[player].owner, buff);
    else
      raw_notify_noc(db[player].owner, buff);
  }
  notify_nopup(player, msg);
  depth--;
}

/* special notify, if puppet && owner is in same room don't relay */
static void snotify(player, msg)
dbref player;
char *msg;
{
  if ((db[player].owner != player) && (db[player].flags & PUPPET)
      && (db[player].location == db[db[player].owner].location))
    notify_nopup(player, msg);
  else
    notify(player, msg);
}

static void notify_except P((dbref, dbref, char *));
static void notify_except(first, exception, msg)
dbref first;
dbref exception;
char *msg;
{
  if (first == NOTHING)
    return;
  DOLIST(first, first)
  {
    if (			/* ((db[first].flags & TYPE_MASK) ==
				   TYPE_PLAYER || ((db[first].flags & PUPPET) 

				   && (Typeof(first)==TYPE_THING))) && */ first != exception)
    {
      snotify(first, msg);
    }
  }
}

static void notify_except2 P((dbref, dbref, dbref, char *));
static void notify_except2(first, exception1, exception2, msg)
dbref first;
dbref exception1;
dbref exception2;
char *msg;
{
  if (first == NOTHING)
    return;
  DOLIST(first, first)
  {
    if ((first != exception1) && (first != exception2))
      snotify(first, msg);
  }
}

void notify_in(room, exception, msg)
dbref room, exception;
char *msg;
{
  dbref z;

  DOZONE(z, room)
    notify(z, msg);
  if (room == NOTHING)
    return;
  if (room != exception)
    snotify(room, msg);
  notify_except(db[room].contents, exception, msg);
  notify_except(db[room].exits, exception, msg);
}

void notify_in2(room, exception1, exception2, msg)
dbref room, exception1, exception2;
char *msg;
{
  dbref z;

  DOZONE(z, room);
  notify(z, msg);
  if (room == NOTHING)
    return;
  if ((room != exception1) && (room != exception2))
    snotify(room, msg);
  notify_except2(db[room].contents, exception1, exception2, msg);
  notify_except2(db[room].exits, exception1, exception2, msg);
}

static void do_shutdown(player, arg1)
dbref player;
char *arg1;
{
  extern int exit_status;

  if (strcmp(arg1, muse_name))
  {
    if (!*arg1)
      notify(player, "You must specify the name of the muse you wish to shutdown.");
    else
      notify(player, tprintf("This is %s, not %s.", muse_name, arg1));
    return;
  }
  log_important(tprintf("|R+Shutdown attempt| by %s", unparse_object(player, player)));
  if (power(player, POW_SHUTDOWN))
  {
    log_important(tprintf("|Y!+SHUTDOWN|: by %s", unparse_object(player, player)));
    shutdown_flag = 1;
    exit_status = 0;
  }
  else
  {
    notify(player, "@shutdown is a restricted command.");
  }
}

static void do_reload(player, arg1)
dbref player;
char *arg1;
{
  extern int exit_status;

  if (strcmp(arg1, muse_name))
  {
    if (!*arg1)
      notify(player, "You must specify the name of the muse you wish to reboot.");
    else
      notify(player, tprintf("This is %s, not %s.", muse_name, arg1));
    return;
  }

  if (power(player, POW_SHUTDOWN))
  {
    log_important(tprintf("%s executed: @reload %s", unparse_object_a(player, player), arg1));
    shutdown_flag = 1;
    exit_status = 1;
  }
  else
  {
    log_important(tprintf("%s failed to: @reload %s", unparse_object_a(player, player), arg1));
    notify(player, "@reload is a restricted command.");
  }
}

#ifdef XENIX
/* rename hack!!! */
void rename(s1, s2)
char *s1;
char *s2;
{
  char buff[300];

  sprintf(buff, "mv %s %s", s1, s2);
  system(buff);
}
#endif

static void dump_database_internal()
{
  char tmpfile[2048];
  FILE *f;

  sprintf(tmpfile, "%s.#%ld#", dumpfile, epoch - 3);
  unlink(tmpfile);		/* nuke our predecessor */
  sprintf(tmpfile, "%s.#%ld#", dumpfile, epoch);
#ifdef DBCOMP
  if ((f = popen(tprintf("gzip >%s", tmpfile), "w")) != NULL)
  {
    db_write(f);
    if ((pclose(f) == 123) ||
	(link(tmpfile, dumpfile) < 0))
    {
      no_dbdump();
      perror(tmpfile);
    }
    sync();
  }
  else
  {
    no_dbdump();
    perror(tmpfile);
  }
#else
  if ((f = fopen(tmpfile, "w")) != NULL)
  {
    db_write(f);
    fclose(f);
    unlink(dumpfile);
    if (link(tmpfile, dumpfile) < 0)
    {
      perror(tmpfile);
      no_dbdump();
    }
    sync();
  }
  else
  {
    no_dbdump();
    perror(tmpfile);
  }
#endif
}

void panic(message)
char *message;
{
  char panicommand_loge[2048];
  FILE *f;
  int i;

  sprintf(panicommand_loge, "PANIC!! %s", message);	/* kludge! */
  log_error(panicommand_loge);	/* reuse panicommand_loge later! yay! */

  report();
  /* turn off signals */
  for (i = 0; i < NSIG; i++)
  {
    signal(i, SIG_IGN);
  }

  /* shut down interface */
  emergency_shutdown();

  /* dump panic file */
  sprintf(panicommand_loge, "%s.PANIC", dumpfile);
  if ((f = fopen(panicommand_loge, "w")) == NULL)
  {				/* sucks to be you */
    perror("CANNOT OPEN PANIC FILE, YOU LOSE");
    exit_nicely(136);
  }
  else
  {
    log_io(tprintf("DUMPING: %s", panicommand_loge));
    db_write(f);
    fclose(f);
    log_io(tprintf("DUMPING: %s (done)", panicommand_loge));
    exit_nicely(136);
  }
}

void dump_database()
{
  epoch++;

  log_io(tprintf("DUMPING: %s.#%ld#", dumpfile, epoch));
  dump_database_internal();
  log_io(tprintf("DUMPING: %s.#%ld# (done)", dumpfile, epoch));
}

void free_database()
{
  /* free all the objects, and everything. for malloc debugging. */
  int i;

  for (i = 0; i < db_top; i++)
  {
    SAFE_FREE(db[i].name);
    if (db[i].parents)
      SAFE_FREE(db[i].parents);
    if (db[i].children)
      SAFE_FREE(db[i].children);
    if (db[i].pows)
      SAFE_FREE(db[i].pows);
    if (db[i].atrdefs)
    {
      struct atrdef *j, *next = NULL;

      for (j = db[i].atrdefs; j; j = next)
      {
	next = j->next;
	SAFE_FREE(j->a.name);
	SAFE_FREE(j);
      }
    }
    if (db[i].list)
    {
      ALIST *j, *next = NULL;

      for (j = db[i].list; j; j = next)
      {
	next = AL_NEXT(j);
	SAFE_FREE(j);
      }
    }
  }
  // free(db - 5);
  struct object *temp = db - 5;
  SAFE_FREE(temp);
  db=NULL;
}

void fork_and_dump()
{
  int child;

#ifdef USE_VFORK
  static char buf[100] = "";

  /* first time through only, setup dump message */
  if (*buf == '\0')
    sprintf(buf, "%s Database saved. Sorry for the lag.", muse_name);
#endif

  epoch++;

  log_io(tprintf("CHECKPOINTING: %s.#%ld#", dumpfile, epoch));
#ifdef USE_VFORK
  for (i = 0; i < db_top; i++)
    if (Typeof(i) == TYPE_PLAYER && !(db[i].flags & PLAYER_NO_WALLS))
      notify(i, buf);
#endif

#ifdef USE_VFORK
  child = vfork();
#else /* USE_VFORK */
  child = fork();
  if (child == -1)
    child = vfork();		/* not enough memory! or something.. */
#endif /* USE_VFORK */

  if (child == 0)
  {
    /* in the child */
    close(reserved);		/* get that file descriptor back */
    dump_database_internal();
    write_loginstats(epoch);
#ifdef USE_COMBAT
    dump_skills();
#endif
    _exit(0);
  }
  else if (child < 0)
  {
    perror("fork_and_dump: fork()");
    no_dbdump();
  }
}

static void no_dbdump()
{
  do_broadcast(root, "Database save failed. Please take appropriate precautions.", "");
}

int init_game(infile, outfile)
char *infile;
char *outfile;
{
  FILE *f;
  int a;

  depth = 0;

  for (a = 0; a < 10; a++)
    wptr[a] = NULL;
  /* reserved=open("/dev/null",O_RDWR);  #JMS#  */

#ifdef DBCOMP
  if ((f = popen(tprintf("gunzip <%s", infile), "r")) == NULL)
    return -1;
#else
  if ((f = fopen(infile, "r")) == NULL)
    return -1;
#endif

  remove_temp_dbs();

  /* ok, read it in */
  log_important(tprintf("LOADING: %s", infile));

  fflush(stdout);
  db_set_read(f);
  log_important(tprintf("LOADING: %s (done)", infile));
  /* everything ok */

  /* initialize random number generator */
  srandom(getpid());
  /* set up dumper */
  strcpy(dumpfile, outfile);
  init_timer();

  return 0;
}

/* use this only in process

   _command */
#define Matched(string) { if(!string_prefix((string), command)) goto bad; }

/* the two versions of argument parsing */
static char *do_argtwo(player, rest, cause, buff)
dbref player;
char *rest;
dbref cause;
char *buff;
{
  museexec(&rest, buff, player, cause, 0);
  return (buff);
}

static char **do_argbee(player, rest, cause, arge, buff)
dbref player;
char *rest;
dbref cause;
char *arge[];
char *buff;
{
  int a;

  for (a = 1; a < MAX_ARG; a++)
    arge[a] = (char *)parse_up(&rest, ',');
  /* rest of delimiters are ,'s */

  for (a = 1; a < MAX_ARG; a++)
    if (arge[a])
    {
      museexec(&arge[a], buff, player, cause, 0);
      strcpy(arge[a] = (char *)stack_em(strlen(buff) + 1), buff);
    }
  return (arge);
}

#define arg2 do_argtwo(player,rest,cause,buff)
#define argv do_argbee(player,rest,cause,arge,buff)

void process_command(player, command, cause)
dbref player;
char *command;
dbref cause;
{
  char *arg1;
  char *q;			/* utility */
  char *p;			/* utility */

  /* char args[MAX_ARG][1 */
  char buff[1024], buff2[1024];
  char *arge[MAX_ARG];		/* pointers to arguments (null for empty) */
  char unp[1024];		/* unparsed command */
  char pure[1024];		/* totally unparsed command */
  char pure2[1024];
  char *rest, *temp;
  int match, z = NOTHING;
  dbref zon;

  /* general form command arg0=arg1,arg2...arg10 */
  int slave = IS(player, TYPE_PLAYER, PLAYER_SLAVE);
  int is_direct = 0;

  if (is_pasting(player))
  {
    add_more_paste(player, command);
    return;
  }

  if (IS(player, TYPE_PLAYER, PLAYER_SUSPECT))
     suspectlog(player, command);

  if (player == root)
  {
    if (cause == NOTHING)
      log_sensitive(tprintf("(direct) %s", command));
    else
      log_sensitive(tprintf("(cause %ld) %s", cause, command));
  }
  else
  {
    if (cause == NOTHING)
      log_command(tprintf("%s in %s directly executes: %s",
			  unparse_object_a(player, player),
      unparse_object_a(db[player].location, db[player].location), command));
    else
      log_command(tprintf("Caused by %s, %s in %s executes:%s",
	   unparse_object_a(cause, cause), unparse_object_a(player, player),
      unparse_object_a(db[player].location, db[player].location), command));
  }

  if (cause == NOTHING)
  {
    is_direct = 1;
    cause = player;
  };

#ifdef USE_INCOMING
  if (is_direct && Typeof(player) != TYPE_PLAYER && *atr_get(player, A_INCOMING))
  {
    wptr[0] = command;
    did_it(player, player, NULL, NULL, NULL, NULL, A_INCOMING);
    atr_match(player, player, '^', command);
    wptr[0] = 0;
    return;
  }
#endif

  inc_pcmdc();			/* increment command stats */

  temp = command;
  /* skip leading space */
  while (*temp && (*temp == ' '))
    temp++;
  /* skip firsts word */
  while (*temp && (*temp != ' '))
    temp++;
  /* skip leading space */
  if (*temp)
    temp++;
  strcpy(pure, temp);
  while (*temp && (*temp != '='))
    temp++;
  if (*temp)
    temp++;
  strcpy(pure2, temp);
  func_zerolev();
  depth = 0;
  if (command == 0)
    abort();
  /* Access the player */
  if (player == root && cause != root)
    return;
  /* robustify player */
  if ((player < 0) || (player >= db_top)
    )
  {
    log_error(tprintf("process_command: Bad player %ld", player));
    return;
  }
  speaker = player;

  if ((db[player].flags & PUPPET) && (db[player].flags & DARK) &&
      !(db[player].flags & TYPE_PLAYER))
  {
    char buf[2000];

    sprintf(buf, "%s>> %s", db[player].name, command);
    raw_notify(db[player].owner, buf);
  }
  /* eat leading whitespace */
  while (*command && isspace(*command))
    command++;
  /* eat extra white space */
  q = p = command;
  while (*p)
  {
    /* scan over word */
    while (*p && !isspace(*p))
      *q++ = *p++;
    /* smash spaces */
    while (*p && isspace(*++p)) ;
    if (*p)
      *q++ = ' ';		/* add a space to separate next word */
  }
  /* terminate */
  *q = '\0';

  /* important home checking comes first! */
  if (strcmp(command, "home") == 0)
  {
    do_move(player, command);
    return;
  }

  if (!slave && try_force(player, command))	/* ||
						   ((Typeof(player)!=TYPE_PLAYER) 

						   &&
						   (Typeof(player)!=TYPE_THING))) 

						 */
  {
    return;
  }
  /* check for single-character commands */
  if (*command == SAY_TOKEN)
  {
    do_say(player, command + 1, NULL);
  }
  else if (*command == POSE_TOKEN)
  {
    do_pose(player, command + 1, NULL, 0);
  }
  else if (*command == NOSP_POSE)
  {
    do_pose(player, command + 1, NULL, 1);
  }
  else if (*command == COM_TOKEN)
  {
    do_com(player, "", command + 1);
  }
  else if (*command == TO_TOKEN)
  {
    do_to(player, command + 1, NULL);
  }
  else if (*command == THINK_TOKEN)
  {
    do_think(player, command + 1, NULL);
  }
  else if (can_move(player, command))
  {
    /* command is an exact match for an exit */
    do_move(player, command);
  }
  else
  {
    strcpy(unp, command);
    /* parse arguments */

    /* find arg1 */
    /* move over command word */
    for (arg1 = command; *arg1 && !isspace(*arg1); arg1++) ;
    /* truncate command */
    if (*arg1)
      *arg1++ = '\0';

    /* move over spaces */
    while (*arg1 && isspace(*arg1))
      arg1++;

    arge[0] = (char *)parse_up(&arg1, '=');	/* first delimiter is ='s */
    rest = arg1;		/* either arg2 or argv */
    if (arge[0])
      museexec(&arge[0], buff2, player, cause, 0);
    arg1 = (arge[0]) ? buff2 : "";
    if (slave)
      switch (command[0])
      {
      case 'l':
	Matched("look");
	do_look_at(player, arg1);
	break;
      }
    else
      switch (command[0])
      {
      case '+':
	switch (command[1])
	{
	case 'a':
	case 'A':
	  Matched("+away");
	  do_away(player, arg1);
	  break;
	case 'b':
	case 'B':
          switch (command[2])
          {
          case 'a':
          case 'A':
	    Matched("+ban");
	    do_ban(player, arg1, arg2);
	    break;
          case '\0':
          case 'o':
          case 'O':
	    Matched("+board");
	    do_board(player, arg1, arg2);
	    break;
          default:
            goto bad;
          }
          break;
	case 'c':
	case 'C':
	  switch (command[2])
	  {
	  case 'o':
	  case 'O':
	  case '\0':
	    Matched("+com");
	    do_com(player, arg1, arg2);
	    break;
	  case 'm':
	  case 'M':
	    Matched("+cmdav");
	    do_cmdav(player);
	    break;
	  case 'h':
	  case 'H':
	    Matched("+channel");
	    do_channel(player, arg1, arg2);
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 'h':
	case 'H':
	  Matched("+haven");
	  do_haven(player, arg1);
	  break;
	case 'i':
	case 'I':
	  Matched("+idle");
	  do_idle(player, arg1);
	  break;
	case 'l':
	case 'L':
	  switch (command[2])
	  {
	  case 'a':
	  case 'A':
	    Matched("+laston");
	    do_laston(player, arg1);
	    break;
	  case 'o':
	  case 'O':
	    Matched("+loginstats");
	    do_loginstats(player);
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 'm':
	case 'M':
	  switch (command[2])
	  {
	  case 'a':
	  case 'A':
	    Matched("+mail");
	    do_mail(player, arg1, arg2);
	    break;
	  case 'o':
	  case 'O':
	    Matched("+motd");
	    do_plusmotd(player, arg1, arg2);
	    break;
	  default:
	    goto bad;
	  }
	  break;
#ifdef USE_COMBAT_TM97
	case 's':
	case 'S':
	  switch (command[2])
	  {
	  case 'k':
	  case 'K':
	    Matched("+skills");
	    do_skills(player, arg1, arg2);
	    break;
	  case 't':
	  case 'T':
	    Matched("+status");
	    do_status(player, arg1);
	    break;
	  default:
	    goto bad;
	  }
	  break;
#endif /* USE_COMBAT_TM97 */
	case 'u':
	case 'U':
	  switch (command[2])
	  {
	  case 'n':
	  case 'N':
	    Matched("+unban");
	    do_unban(player, arg1, arg2);
	    break;
	  case 'p':
	  case 'P':
	    Matched("+uptime");
	    do_uptime(player);
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 'v':
	case 'V':
	  Matched("+version");
	  do_version(player);
	  break;
	default:
	  goto bad;
	}
	break;
      case '@':
	switch (command[1])
	{
	case 'a':
	case 'A':
	  switch (command[2])
	  {
	  case 'd':
	  case 'D':
	    Matched("@addparent");
	    do_addparent(player, arg1, arg2);
	    break;
	  case 'l':
	  case 'L':
	    Matched("@allquota");
	    if (!is_direct)
	      goto bad;
	    do_allquota(player, arg1);
	    break;
	  case 'n':
	  case 'N':
	    Matched("@announce");
	    do_announce(player, arg1, arg2);
	    break;
	  case 't':
	  case 'T':
	    Matched("@at");
	    do_at(player, arg1, arg2);
	    break;
	  case 's':
	  case 'S':
	    Matched("@as");
	    do_as(player, arg1, arg2);
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 'b':
	case 'B':
	  switch (command[2])
	  {
	  case 'r':
	  case 'R':
	    Matched("@broadcast");
	    do_broadcast(player, arg1, arg2);
	    break;
	  case 'o':
	  case 'O':
	    Matched("@boot");
	    do_boot(player, arg1, arg2);
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 'c':
	case 'C':
	  /* chown, create */
	  switch (command[2])
	  {
	  case 'b':
	  case 'B':
	    Matched("@cboot");
	    do_cboot(player, arg1);
	    break;
	  case 'e':
	  case 'E':
	    Matched("@cemit");
	    do_cemit(player, arg1, arg2);
	    break;
	  case 'h':
	  case 'H':
	    if (string_compare(command, "@chownall") == 0)
	    {
	      do_chownall(player, arg1, arg2);
	    }
	    else
	    {
	      switch (command[3])
	      {
	      case 'o':
	      case 'O':
		Matched("@chown");
		do_chown(player, arg1, arg2);
		break;
	      case 'e':
	      case 'E':
		switch (command[4])
		{
		case 'c':
		case 'C':
		  Matched("@check");
		  do_check(player, arg1);
		  break;
		case 'm':
		case 'M':
		  Matched("@chemit");
		  do_chemit(player, arg1, arg2);
		  break;
		default:
		  goto bad;
		}
		break;
	      default:
		goto bad;
	      }
	    }
	    break;
	  case 'l':
	  case 'L':
	    switch (command[3])
	    {
	    case 'a':
	    case 'A':
	      Matched("@class");
	      /* if (!is_direct) goto bad;  *//* keep wizbugs from allowing @class */
	      do_class(player, arg1, arg2);
	      break;
	    case 'o':
	    case 'O':
	      Matched("@clone");
	      do_clone(player, arg1, arg2);
	      break;
	    default:
	      goto bad;
	    }
	    break;
	  case 'n':
	  case 'N':
	    Matched("@cname");
	    do_cname(player, arg1, arg2);
	    break;
	  case 'o':
	  case 'O':
	    Matched("@config");
	    do_config(player, arg1, arg2);
	    break;
	  case 'p':
	  case 'P':
	    Matched("@cpaste");
	    notify(player,"WARNING: @cpaste antiquated. Use '@paste channel=<channel>'");
            do_paste(player, "channel", arg1);
/*	    do_cpaste(player, arg1); */
	    break;
	  case 'r':
	  case 'R':
	    Matched("@create");
	    do_create(player, arg1, atol(arg2));
	    break;
	  case 's':
	  case 'S':
	    Matched("@cset");
	    do_set(player, arg1, arg2, 1);
	    break;
	  case 't':
	  case 'T':
	    Matched("@ctrace");
	    do_ctrace(player);
	    break;
	  case 'y':
	  case 'Y':
	    Matched("@cycle");
	    do_cycle(player, arg1, argv);
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 'd':
	case 'D':
	  /* describe, dig, or dump */
	  switch (command[2])
	  {
	  case 'b':
	  case 'B':
	    switch (command[3])
	    {
	    case 'c':
	    case 'C':
	      Matched("@dbck");
	      do_dbck(player);
	      break;
	    case 't':
	    case 'T':
	      Matched("@dbtop");
	      do_dbtop(player, arg1);
	      break;
	    default:
	      goto bad;
	    }
	    break;
	  case 'e':
	  case 'E':
	    switch (command[3])
	    {
	    case 'c':
	    case 'C':
	      Matched("@decompile");
	      do_decompile(player, arg1, arg2);
	      break;
	    case 's':
	    case 'S':
	      switch (command[4])
	      {
	      case 'c':
	      case 'C':
		Matched("@describe");
		do_describe(player, arg1, arg2);
		break;
	      case 't':
	      case 'T':
		Matched("@destroy");
		do_destroy(player, arg1);
		break;
	      default:
		goto bad;
	      }
	      break;
	    case 'f':
	    case 'F':
	      Matched("@defattr");
	      do_defattr(player, arg1, arg2);
	      break;
	    case 'l':
	    case 'L':
	      Matched("@delparent");
	      do_delparent(player, arg1, arg2);
	      break;
	    default:
	      goto bad;
	    }
	    break;
	  case 'i':
	  case 'I':
	    Matched("@dig");
	    do_dig(player, arg1, argv);
	    break;
	  case 'u':
	  case 'U':
	    Matched("@dump");
	    do_dump(player);
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 'f':
	case 'F':
	  switch (command[2])
	  {
	  case 'i':
	  case 'I':
	    switch (command[3])
	    {
	    case 'n':
	    case 'N':
	      Matched("@find");
	      do_find(player, arg1);
	      break;
	      /* case 'x': case 'X': Matched("@fixquota");
	         do_fixquota(player, arg1); break; */
	    default:
	      goto bad;
	    }
	    break;
	  case 'o':
	  case 'O':
	    if (string_prefix("@foreach", command) && strlen(command) > 4)
	    {
	      Matched("@foreach");
	      do_foreach(player, arg1, arg2, cause);
	      break;
	    }
	    else
	    {
	      Matched("@force");
	      do_force(player, arg1, arg2);
	      break;
	    }
	  default:
	    goto bad;
	  }
	  break;
	case 'e':
	case 'E':
	  switch (command[2])
	  {
	  case 'c':
	  case 'C':
	    Matched("@echo");
	    do_echo(player, arg1, arg2, 0);
	    break;
	  case 'd':
	  case 'D':
	    Matched("@edit");
	    do_edit(player, arg1, argv);
	    break;
	  case 'm':
	  case 'M':
	    switch (command[3])
	    {
	    case 'i':
	    case 'I':
	      Matched("@emit");
	      do_emit(player, arg1, arg2, 0);
	      break;
	    case 'p':
	    case 'P':
	      Matched("@empower");
	      if (!is_direct)
		goto bad;
	      do_empower(player, arg1, arg2);
	      break;
	    default:
	      goto bad;
	    }
	    break;
#ifdef ALLOW_EXEC
	  case 'x':
	  case 'X':
	    Matched("@exec");
	    do_exec(player, arg1, arg2);
	    break;
#endif /* ALLOW_EXEC */
	  default:
	    goto bad;
	  }
	  break;
	case 'g':
	case 'G':
	  switch (command[2])
	  {
	  case 'i':
	  case 'I':
	    Matched("@giveto");
	    do_giveto(player, arg1, arg2);
	    break;
#ifdef USE_UNIV
	  case 'u':
	  case 'U':
	    Matched("@guniverse");
	    do_guniverse(player, arg1);
	    break;
#endif
	  case 'z':
	  case 'Z':
	    Matched("@gzone");
	    do_gzone(player, arg1);
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 'h':
	case 'H':		/* removes all queued commands by your
				   objects */
	  /* halt or hide */
	  switch (command[2])
	  {
	  case 'a':
	  case 'A':
	    Matched("@halt");
	    do_halt(player, arg1, arg2);
	    break;
	  case 'i':		/* hides player name from WHO */
	  case 'I':
	    Matched("@hide");
	    do_hide(player);
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 'i':
	case 'I':
	  Matched("@info");
	  do_info(player, arg1);
	  break;
	case 'l':
	case 'L':
	  /* lock or link */
	  switch (command[2])
	  {
	  case 'i':
	  case 'I':
            switch (command[3]) 
            {
#ifdef USE_COMBAT
            case 's':
            case 'S':
              Matched("@listarea");
              do_listarea(player, arg1);
              break;
#endif
            default:
	      Matched("@link");
              if (Guest(player)) 
              {
                notify(player,perm_denied()); 
              }
              else
	        do_link(player, arg1, arg2);
	      break;
            }
            break;
	  case 'o':
	  case 'O':
	    switch (command[5])
	    {
	    case 'o':
	    case 'O':
	      Matched("@lockout");
	      if (!is_direct)
		goto bad;
	      do_lockout(player, arg1);
	      break;
	    default:
	      goto bad;
	    }
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 'm':
	case 'M':
	  switch (command[2])
	  {
	  case 'i':
	  case 'I':
	    Matched("@misc");	/* miscelanious functions */
	    do_misc(player, arg1, arg2);
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 'n':
	case 'N':
	  switch (command[2])
	  {
	  case 'a':
	  case 'A':
	    Matched("@name");
	    do_name(player, arg1, arg2, is_direct);
	    break;
	  case 'c':
	  case 'C':
	    Matched("@ncset");
	    do_set(player, arg1, pure2, 1);
	    break;
	  case 'e':
	  case 'E':
	    switch (command[3])
	    {
	    case 'c':
	    case 'C':
	      Matched("@necho");
	      do_echo(player, pure, NULL, 1);
	      break;
	    case 'w':
	      if (strcmp(command, "@newpassword"))
		goto bad;
	      if (!is_direct)
		goto bad;
	      do_newpassword(player, arg1, arg2);
	      break;
	    case 'm':
	    case 'M':
	      Matched("@nemit");
	      do_emit(player, pure, NULL, 1);
	      break;
	    default:
	      goto bad;
	    }
	    break;
	  case 'o':
	  case 'O':
	    switch (command[3])
	    {
	    case 'o':
	    case 'O':
	      Matched("@noop");
	      break;
	    case 'l':
	    case 'L':
	      Matched("@nologins");
	      if (!is_direct)
		goto bad;
	      do_nologins(player, arg1);
	      break;
	    case 'p':
	    case 'P':
	      Matched("@nopow_class");
	      if (!is_direct)
		goto bad;
	      do_nopow_class(player, arg1, arg2);
	      break;
	    default:
	      goto bad;
	    }
	    break;
	  case 'p':
	  case 'P':
	    switch (command[3])
	    {
	    case 'e':
	    case 'E':
	      Matched("@npemit");
	      do_general_emit(player, arg1, pure, 4);
	      break;
	    case 'a':
	    case 'A':
              switch (command[4])
              {
              case 'g':
              case 'G':
	        Matched("@npage");
	        do_page(player, arg1, pure2);
	        break;
              case 's':
              case 'S':
                Matched("@npaste");
                do_pastecode(player, arg1, arg2);
                break;
              default:
                goto bad;
              }
              break;
	    default:
	      goto bad;
	    }
	    break;
	  case 's':
	  case 'S':
	    Matched("@nset");
	    do_set(player, arg1, pure2, is_direct);
	    break;
	  case 'u':
	  case 'U':
	    Matched("@nuke");
	    if (!is_direct)
	      goto bad;
	    do_nuke(player, arg1);
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 'o':
	case 'O':
	  switch (command[2])
	  {
	  case 'e':
	  case 'E':
	    Matched("@oemit");
	    do_general_emit(player, arg1, arg2, 2);
	    break;
	  case 'p':
	  case 'P':
	    Matched("@open");
	    do_open(player, arg1, arg2, NOTHING);
	    break;
	  case 'u':
	  case 'U':
	    Matched("@outgoing");
#ifdef USE_OUTGOING
	    do_outgoing(player, arg1, arg2);
#else
	    sprintf(buff, "@outgoing disabled - ");
	    goto bad;
#endif
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 'p':
	case 'P':
	  switch (command[2])
	  {
	  case 'a':
	  case 'A':
	    switch (command[3])
	    {
	    case 's':
	    case 'S':
	      switch (command[4])
	      {
	      case 's':
	      case 'S':
		Matched("@password");
		do_password(player, arg1, arg2);
		break;
	      case 't':
	      case 'T':
		switch (command[6])
		{
		case '\0':
		  Matched("@paste");
		  do_paste(player, arg1, arg2);
		  break;
                case 'c':
                case 'C':
                  Matched("@pastecode");
                  do_pastecode(player, arg1, arg2);
                  break;
		case 's':
		case 'S':
		  Matched("@pastestats");
		  do_pastestats(player, arg1);
		  break;
		default:
		  goto bad;
		}
		break;
	      default:
		goto bad;
	      }
	      break;
	    default:
	      goto bad;
	    }
	    break;
	  case 'b':
	  case 'B':
	    Matched("@pbreak");
	    do_pstats(player, arg1);
	    break;
	  case 'C':
	  case 'c':
	    Matched("@pcreate");
	    do_pcreate(player, arg1, arg2);
	    break;
	  case 'e':
	  case 'E':
	    Matched("@pemit");
	    do_general_emit(player, arg1, arg2, 0);
	    break;
	  case 'O':
	  case 'o':
	    switch (command[3])
	    {
	    case 'o':
	    case 'O':
	      Matched("@Poor");
	      if (!is_direct)
		goto bad;
	      do_poor(player, arg1);
	      break;
	    case 'w':
	    case 'W':
	      Matched("@powers");
	      do_powers(player, arg1);
	      break;
	    default:
	      goto bad;
	    }
	    break;
	  case 'S':
	  case 's':
	    Matched("@ps");
	    do_queue(player);
	    break;
	  case 'u':
	  case 'U':		/* force room destruction */
	    Matched("@purge");
	    do_purge(player);
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 'q':
	case 'Q':
	  Matched("@quota");
	  do_quota(player, arg1, arg2);
	  break;
	case 'r':
	case 'R':
	  switch (command[2])
	  {
#ifdef USE_COMBAT
          case 'a':
          case 'A':
            Matched("@racelist");
            do_racelist(player, arg1);
            break;
#endif /* USE_COMBAT */
	  case 'e':
	  case 'E':
	    switch (command[3])
	    {
	    case 'b':
	    case 'B':
	      Matched("@reboot");
              notify(player, "It's no longer @reboot. It's @reload.");
	      do_reload(player, arg1);
	      break;
	    case 'l':
	    case 'L':
	      Matched("@reload");
	      do_reload(player, arg1);
	      break;
	    case 'm':
	    case 'M':
	      Matched("@remit");
	      do_general_emit(player, arg1, arg2, 1);
	      break;
	    default:
	      goto bad;
	    }
	    break;
	  case 'O':
	  case 'o':
	    Matched("@robot");
	    do_robot(player, arg1, arg2);
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 's':
	case 'S':
	  switch (command[2])
	  {
	  case 'e':
	  case 'E':
	    switch (command[3])
	    {
	    case 'a':
	    case 'A':
	      Matched("@search");
              if (Guest(player)) 
              {
                notify(player,perm_denied()); 
              }
              else
	        do_search(player, arg1, arg2);
	      break;
	    case 't':
	    case 'T':
              switch (command[4]) {
              case '\0':
	        Matched("@set");
                if (Guest(player)) 
                {
                  notify(player,perm_denied()); 
                }
                else
	          do_set(player, arg1, arg2, is_direct);
	        break;
#ifdef USE_COMBAT
              case 'b':
              case 'B':
                Matched("@setbit")
                do_setbit(player, arg1, arg2);
                break;
#endif /* USE_COMBAT */
              default:
                goto bad;
              }
              break;
	    default:
	      goto bad;
	    }
	    break;
	  case 'h':
	  case 'H':
	    switch (command[3])
	    {
#ifdef SHRINK_DB
	    case 'r':
              Matched("@shrink");
	      do_shrinkdbuse(player, arg1);
	      break;
#endif
	    case 'u':
	      if (strcmp(command, "@shutdown"))
		goto bad;
	      do_shutdown(player, arg1);
	      break;
	    case 'o':
	    case 'O':
	      Matched("@showhash");
	      do_showhash(player, arg1);
	      break;
	    default:
	      goto bad;
	    }
	    break;
#ifdef USE_COMBAT_TM97
	  case 'k':
	  case 'K':
	    Matched("@skillset");
	    do_skillset(player, arg1, arg2);
	    break;
#endif /* USE_COMBAT_TM97 */
#ifdef USE_COMBAT
          case 'p':
          case 'P':
            switch (command[3]) {
            case 'a':
            case 'A':
              Matched("@spawn");
              do_spawn(player, arg1, arg2);
              break;
            default:
              goto bad;
            }
            break;
#endif /* USE_COMBAT */
	  case 't':
	  case 'T':
	    Matched("@stats");
	    do_stats(player, arg1);
	    break;
	  case 'u':
	  case 'U':
	    Matched("@su");
	    do_su(player, arg1, arg2, cause);
	    break;
	  case 'w':
	  case 'W':
	    switch (command[3])
	    {
	    case 'a':
	    case 'A':
	      Matched("@swap");
	      do_swap(player, arg1, arg2);
	      break;
	    case 'e':
	    case 'E':
	      Matched("@sweep");
	      do_sweep(player, pure);
	      break;
	    case 'i':
	    case 'I':
	      Matched("@switch");
	      do_switch(player, arg1, argv, cause);
	      break;
	    default:
	      goto bad;
	    }
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 't':
	case 'T':
	  switch (command[2])
	  {
	  case 'e':
	  case 'E':
	    switch (command[3])
	    {
	    case 'l':
	    case 'L':
	    case '\0':
	      Matched("@teleport");
	      do_teleport(player, arg1, arg2);
	      break;
/*
	    case 's':
	    case 'S':
	      Matched("@test");
	      give_allowances();
	      break;
*/
	    case 'x':
	    case 'X':
	      Matched("@text");
	      do_text(player, arg1, arg2, NULL);
	      break;
	    default:
	      goto bad;
	    }
	    break;
	  case 'r':
	  case 'R':
	    switch (command[3])
	    {
	    case 'i':
	    case 'I':
	    case '\0':
	      Matched("@trigger");
	      do_trigger(player, arg1, argv);
	      break;
	    case '_':
	      Matched("@tr_as");
	      do_trigger_as(player, arg1, argv);
	      break;
	    default:
	      goto bad;
	    };
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 'u':
	case 'U':
	  switch (command[2])
	  {
#ifdef USE_UNIV
	  case 'c':
	  case 'C':
	    switch (command[3])
	    {
	    case 'o':
	    case 'O':
	      Matched("@uconfig");
	      do_uconfig(player, arg1, arg2);
	      break;
	    case 'r':
	    case 'R':
	      Matched("@ucreate");
	      do_ucreate(player, arg1, atol(arg2));
	      break;
	    default:
	      goto bad;
	    }
	    break;
	  case 'i':
	  case 'I':
	    Matched("@uinfo");
	    do_uinfo(player, arg1);
	    break;
	  case 'l':
	  case 'L':
	    Matched("@ulink");
	    do_ulink(player, arg1, arg2);
	    break;
#endif
	  case 'n':
	  case 'N':
	    switch (command[3])
	    {
	    case 'd':
	    case 'D':
	      switch (command[4])
	      {
	      case 'e':
	      case 'E':
		switch (command[5])
		{
		case 's':
		case 'S':
		  Matched("@undestroy");
		  do_undestroy(player, arg1);
		  break;
		case 'f':
		case 'F':
		  Matched("@undefattr");
		  do_undefattr(player, arg1);
		  break;
		default:
		  goto bad;
		}
		break;
	      default:
		goto bad;
	      }
	      break;
	    case 'l':
	    case 'L':
	      switch (command[4])
	      {
	      case 'i':
	      case 'I':
		Matched("@unlink");
		do_unlink(player, arg1);
		break;
	      case 'o':
	      case 'O':
		Matched("@unlock");
		do_unlock(player, arg1);
		break;
	      default:
		goto bad;
	      }
	      break;
	    case 'h':
	    case 'H':
	      Matched("@unhide");
	      do_unhide(player);
	      break;
#ifdef USE_UNIV
	    case 'u':
	    case 'U':
	      Matched("@unulink");
	      do_unulink(player, arg1);
	      break;
#endif
	    case 'z':
	    case 'Z':
	      Matched("@unzlink");
	      do_unzlink(player, arg1);
	      break;
	    default:
	      goto bad;
	    }
	    break;
	  case 'p':
	  case 'P':
	    Matched("@upfront");
	    do_upfront(player, arg1);
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 'w':
	case 'W':
	  switch (command[2])
	  {
	  case 'a':
	  case 'A':
	    Matched("@wait");
	    wait_que(player, atoi(arg1), arg2, cause);
	    break;
	  case 'e':
	  case 'E':
	    Matched("@wemit");
	    do_wemit(player, arg1, arg2);
	    break;
	  case 'h':
	  case 'H':
	    Matched("@whereis");
	    do_whereis(player, arg1);
	    break;
	  case 'i':
	  case 'I':
	    Matched("@wipeout");
	    if (!is_direct)
	      goto bad;
	    do_wipeout(player, arg1, arg2);
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 'z':
	case 'Z':
	  switch (command[2])
	  {
	  case 'e':
	  case 'E':
	    Matched("@zemit");
	    do_general_emit(player, arg1, arg2, 3);
	    break;
	  case 'l':
	  case 'L':
	    Matched("@zlink");
	    do_zlink(player, arg1, arg2);
	    break;
	  default:
	    goto bad;
	  }
	  break;
	default:
	  goto bad;
	}
	break;
      case 'd':
      case 'D':
	Matched("drop");
	do_drop(player, arg1);
	break;
      case 'e':
      case 'E':
	switch (command[1])
	{
	case 'X':
	case 'x':
	  Matched("examine");
	  do_examine(player, arg1, arg2);
	  break;
	case 'N':
	case 'n':
	  Matched("enter");
	  do_enter(player, arg1);
	  break;
#ifdef USE_COMBAT
        case 'q':
        case 'Q':
          Matched("equip");
          do_equip(player, player, arg1);
          break;
#endif /* USE_COMBAT */
	default:
	  goto bad;
	}
	break;
#ifdef USE_COMBAT_TM97
      case 'f':
      case 'F':
	Matched("fight");
	do_fight(player, arg1);
	break;
#endif
#ifdef USE_COMBAT
      case 'f':
      case 'F':
        switch (command[1])
        {
        case 'i':
        case 'I':
        case '\0':
          Matched("fight");
          do_fight(player, arg1, arg2);
          break;
        case 'l':
        case 'L':
          Matched("flee");
          do_flee(player);
          break;
        default:
          goto bad;
        }
        break;
#endif /* USE_COMBAT */

      case 'g':
      case 'G':
	/* get, give, go, or gripe */
	switch (command[1])
	{
	case 'e':
	case 'E':
	  Matched("get");
	  do_get(player, arg1);
	  break;
	case 'i':
	case 'I':
	  Matched("give");
	  do_give(player, arg1, arg2);
	  break;
	case 'o':
	case 'O':
	  Matched("goto");
	  do_move(player, arg1);
	  break;
	case 'r':
	case 'R':
	  Matched("gripe");
	  do_gripe(player, arg1, arg2);
	  break;
	default:
	  goto bad;
	}
	break;
      case 'h':
      case 'H':
	Matched("help");
	do_text(player, "help", arg1, NULL);
	/* do_help(player, arg1, "help", HELPINDX, HELPTEXT, NULL); */
	break;
      case 'i':
      case 'I':
        switch(command[1]) {
        case 'd':
        case 'D':
          Matched("idle");
            set_idle_command(player, arg1, arg2);
          break;
        default:
	  Matched("inventory");
	  do_inventory(player);
	  break;
        }
        break;
      case 'j':
      case 'J':
	Matched("join");
	do_join(player, arg1);
	break;
      case 'l':
      case 'L':
	switch (command[1])
	{
	case 'o':
	case 'O':
	case '\0':		/* patch allow 'l' command to do a look */
	  Matched("look");
	  do_look_at(player, arg1);
	  break;
	case 'E':
	case 'e':
	  Matched("leave");
	  do_leave(player);
	  break;
	default:
	  goto bad;
	}
	break;
      case 'm':
      case 'M':
	switch (command[1])
	{
	case 'o':
	case 'O':
	  switch (command[2])
	  {
	  case 'n':
	  case 'N':
	    Matched("money");
	    do_money(player, arg1, arg2);
	    break;
	  case 't':
	  case 'T':
	    Matched("motd");
	    /* do_text(player, "motd", arg1, NULL); */
	    do_motd(player);
	    break;
	  case 'v':
	  case 'V':
	    Matched("move");
	    do_move(player, arg1);
	    break;
	  default:
	    goto bad;
	  }
	  break;
	default:
	  goto bad;
	}
	break;
      case 'n':
      case 'N':
	/* news */
	/* remember, string_compare returns 0 when strings are equal */
	if (string_compare(command, "news"))
	  goto bad;
	/* do_news(player, arg1); */
	/* do_help(player, arg1, "news", NEWSINDX, NEWSTEXT, A_ANEWS); */
	do_text(player, "news", arg1, A_ANEWS);
	break;
      case 'p':
      case 'P':
	switch (command[1])
	{
	case 'a':
	case 'A':
	case '\0':
	  Matched("page");
	  do_page(player, arg1, arg2);
	  break;
	case 'o':
	case 'O':
	  Matched("pose");
	  do_pose(player, arg1, arg2, 0);
	  break;
	case 'r':
	case 'R':
	  Matched("pray");
	  do_pray(player, arg1, arg2);
	  break;
	default:
	  goto bad;
	}
	break;
      case 'r':
      case 'R':
        switch (command[1])
        {
        case 'e':
        case 'E':
	  switch (command[2])
	  {
	  case 'a':
	  case 'A':
	    Matched("read");	/* undocumented alias for look at */
	    do_look_at(player, arg1);
	    break;
#ifdef USE_COMBAT_TM97
	  case 'm':
	  case 'M':
	    Matched("remove");
	    do_remove(player);
	    break;
#endif /* USE_COMBAT_TM97 */
	  default:
	    goto bad;
	  }
          break;
#ifdef USE_RLPAGE
        case 'l':
        case 'L':
          Matched("rlpage");
          do_rlpage(player,arg1,arg2);
          break;
#endif
	default:
	  goto bad;
	}
	break;
      case 's':
      case 'S':
	switch (command[1])
	{
	case 'a':
	case 'A':
	  Matched("say");
	  do_say(player, arg1, arg2);
	  break;
/*
   case 'c':
   case 'C':
   Matched("score");
	   *//* notify(player, "Note: This command will soon dissapear.");
	     notify(player, "      Please start using the command:
	     'money'."); notify(player, " "); *//* 
	     do_score(player); notify(player, "Warning: this command may
	     dissapear in the future."); notify(player,"Please start using
	     the 'money' command."); break; */
#ifdef USE_COMBAT_TM97
	case 'l':
	case 'L':
	  Matched("slay");
	  do_slay(player, arg1);
	  break;
#endif /* USE_COMBAT_TM97 */
	case 'u':
	case 'U':
	  Matched("summon");
	  do_summon(player, arg1);
	  break;
	default:
	  goto bad;
	}
	break;
      case 't':
      case 'T':
	switch (command[1])
	{
	case 'a':
	case 'A':
	  Matched("take");
	  do_get(player, arg1);
	  break;
	case 'h':
	case 'H':
	  switch (command[2])
	  {
	  case 'r':
	  case 'R':
	    Matched("throw");
	    do_drop(player, arg1);
	    break;
	  case 'i':
	  case 'I':
	    Matched("think");
	    do_think(player, arg1, arg2);
	    break;
	  default:
	    goto bad;
	  }
	  break;
	case 'o':
	case 'O':
	  Matched("to");
	  do_to(player, arg1, arg2);
	  break;
	default:
	  goto bad;
	}
	break;
      case 'u':
      case 'U':
	switch (command[1])
	{
#ifdef USE_COMBAT_TM97
	case 'n':
	case 'N':
	  Matched("unwield");
	  do_unwield(player);
	  break;
#endif /* USE_COMBAT_TM97 */
	case 's':
	case 'S':
	  Matched("use");
	  do_use(player, arg1);
	  break;
	default:
	  goto bad;
	}
	break;
      case 'w':
      case 'W':
	switch (command[1])
	{
	case 'h':
	case 'H':
	  switch (command[2])
	  {
	  case 'i':
	  case 'I':
	  case '\0':
	    Matched("whisper");
	    do_whisper(player, arg1, arg2); 
	    break;
	  case 'o':
	  case 'O':
	    Matched("who");
	    dump_users(player, arg1, arg2, NULL);
	    break;
	  default:
	    goto bad;
	  }
	  break;
#ifdef USE_COMBAT_TM97
	case 'e':
	case 'E':
	  Matched("wear");
	  do_wear(player, arg1);
	  break;
	case 'i':
	case 'I':
	  Matched("wield");
	  do_wield(player, arg1);
	  break;
#endif /* USE_COMBAT_TM97 */
	case '\0':
	  if (*arg1) {
	    do_whisper(player, arg1, arg2); 
	  }
	  else
	    dump_users(player, arg1, arg2, NULL);
	  break;
	default:
	  goto bad;
	}
	break;
      default:
	goto bad;
      bad:
	if (!slave && test_set(player, command, arg1, arg2, is_direct))
	{
	  return;
	}
	/* try matching user defined functions before chopping */
	match = list_check(db[db[player].location].contents, player, '$', unp)
	  || list_check(db[player].contents, player, '$', unp)
	  || atr_match(db[player].location, player, '$', unp)
	  || list_check(db[db[player].location].exits, player, '$', unp);
	DOZONE(zon, player)
	  match = list_check(z = zon, player, '$', unp) || match;

	if (!match)
	{
          if (is_channel_alias(player, command) != NULL)
          {
            channel_talk(player, command, arg1, arg2);
          }
          else
	    notify(player, "Huh?  (Type \"help\" for help.)");

	}
	break;
      }
  }
  {
    int a;

    for (a = 0; a < 10; a++)
      wptr[a] = NULL;
  }
}

/* get a player's or object's zone */
dbref get_zone_first(player)
dbref player;
{
  int depth = 10;
  dbref location;

  for (location = player; depth && location != NOTHING; depth--, location = db[location].location)
  {
    if (db[location].zone == NOTHING && (Typeof(location) == TYPE_THING
					 || Typeof(location) == TYPE_ROOM)
	&& location != 0 && location != db[0].zone)
      db[location].zone = db[0].zone;
    if (location == db[0].zone)
      return db[0].zone;
    else if (db[location].zone != NOTHING)
      return db[location].zone;
  }
  return db[0].zone;
}

dbref get_zone_next(player)
dbref player;
{
  if (valid_player(player))
  {
    if (db[player].zone == NOTHING && player != db[0].zone)
    {
      return db[0].zone;
    }
    else
    {
      return db[player].zone;
    }
  }
  return -1;
}

static int list_check(thing, player, type, str)
dbref thing, player;
int type;
char *str;
{
  int match = 0;

  while (thing != NOTHING)
  {
    /* only a player can match on him/herself */
    if (((thing == player) && (Typeof(thing) != TYPE_PLAYER)) ||
	((thing != player) && (Typeof(thing) == TYPE_PLAYER)))
    {
      thing = db[thing].next;
    }
    else
    {
      if (atr_match(thing, player, type, str))
	match = 1;
      thing = db[thing].next;
    }
  }
  return (match);
}

/* routine to check attribute list for wild card matches of certain
   type and queue them */
static int atr_match(thing, player, type, str)
dbref thing, player;
int type;			/* must be symbol not affected by compress */
char *str;			/* string to match */
{
  struct all_atr_list *ptr;
  int match = 0;

  for (ptr = all_attributes(thing); ptr; ptr = ptr->next)
    if ((ptr->type != 0) && !(ptr->type->flags & AF_LOCK) &&
	(*ptr->value == type))
    {
      /* decode it */
      char buff[1024];
      char *s, *p;

      strncpy(buff, (ptr->value), 1024);
      /* search for first un escaped : */
      for (s = buff + 1; *s && (*s != ':'); s++) ;
      if (!*s)
	continue;
      *s++ = 0;
      if (*s == '/')
      {				/* locked attribute */
	p = ++s;
	while (*s && (*s != '/'))
	{
	  if (*s == '[')
	    while (*s && (*s != ']'))
	      s++;
	  s++;
	}
	if (!*s)
	  continue;
	*s++ = '\0';
	if (!eval_boolexp(player, thing, p, get_zone_first(player)))
	  continue;
      }
      if (wild_match(buff + 1, str))
      {
	if (ptr->type->flags & AF_HAVEN)
	  return 0;
	else
	  match = 1;
	if (!eval_boolexp(player, thing, atr_get(thing, A_ULOCK), get_zone_first(player)))
	  did_it(player, thing, A_UFAIL, NULL, A_OUFAIL, NULL, A_AUFAIL);
	else
	  parse_que(thing, s, player);
      }
    }
  return (match);
}

int Live_Player(thing)
dbref thing;
{
  return (db[thing].flags & CONNECT);
}

int Live_Puppet(thing)
dbref thing;
{
  if (db[thing].flags & PUPPET && (Typeof(thing) != TYPE_PLAYER) &&
      (db[thing].flags & CONNECT || db[db[thing].owner].flags & CONNECT))
  {
    return (1);
  }
  else
  {
    return (0);
  }
}

int Listener(thing)
dbref thing;
{
  /* ALIST *ptr; */
  struct all_atr_list *ptr;
  ATTR *type;
  char *s;

  if (Typeof(thing) == TYPE_PLAYER)
    return 0;			/* players don't hear. */

  for (ptr = all_attributes(thing); ptr; ptr = ptr->next)
  {
    type = ptr->type;
    s = ptr->value;
    if (!type)
      continue;
    if (type == A_LISTEN)
      return (1);
    if ((*s == '!') && !(type->flags & AF_LOCK))
      return (1);
  }
  return (0);
}

int Commer(thing)
dbref thing;
{
  /* ALIST *ptr; */
  struct all_atr_list *ptr;
  ATTR *type;
  char *s;

  for (ptr = all_attributes(thing); ptr; ptr = ptr->next)
  {
    /* type=AL_TYPE(ptr); s=AL_STR(ptr); */
    type = ptr->type;
    s = ptr->value;
    if (!type)
      continue;
    if (*s == '$')
      return (1);
  }
  return (0);
}

int Hearer(thing)
dbref thing;
{
/*  if (Live_Player(thing) || Live_Puppet(thing) || Listener(thing)) { */
  if (Live_Puppet(thing) || Listener(thing) || Live_Player(thing))
  {
    return (1);
  }
  else
  {
    return (0);
  }
}

#undef Matched

void exit_nicely(i)
int i;
{
  /* remove_muse_pid(); */
#ifdef MALLOCDEBUG
  mnem_writestats();
#endif
  exit(i);
}
/* End game.c */
