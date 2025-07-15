/* cque.c */
/* $Id: cque.c,v 1.9 1993/08/16 01:57:18 nils Exp $ */

/* this file has been decently modified by me, wm, in order to build a more
 * robust and function command queue.  There are several differences.  First,
 * the command queue kinda-sorta handles priorities now.  Secondly, the wait
 * queue is built in to the main command queue now to make life simpler
 * rather than tracking 3 different damned queues.
 *
 * everything seems to be okay and functional.  @wait seems to work a lot 
 * better also with the new method.  i think the old way was based on micro
 * and other old muses being slow as shit.  
 *
 * anyway, enjoy.                                                      - wm
 */

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


#include "log.h"

extern char ccom[];
extern dbref cplr;

typedef struct bque BQUE;

BQUE *find_wait(BQUE *);


static void big_que P((dbref, char *, dbref,int, time_t));
void do_halt_player(dbref, char *);
void do_halt_process(dbref, int);
int get_pid(void);
void free_pid(int);
void init_pid(void);

struct bque
{
  BQUE *next;
  BQUE *prev;
  dbref player;			/* player who will do command */
  dbref cause;			/* player causing command (for %n) */
  char *env[10];		/* environment, from wild match */
  int pri;			/* Priority of command   -wm */
  time_t wait;			/* wait until "wait" before execution */
  int pid;			/* process ID. -wm */
};


static BQUE *qfirst = NULL, *qlast = NULL; 



void parse_que_pri(player, command, cause, pri)
dbref player;
char *command;
dbref cause;			/* cause is needed to determine priority */
int pri;                        /* priority within queue. */
{
  char *s, *r;

  s = tprintf("%s", command);
  while ((r = (char *)parse_up(&s, ';')))
  {
    big_que(player, r, cause, pri, 0);
  }
}

void parse_que(player, command, cause)
dbref player;
char *command;
dbref cause;			/* cause is needed to determine priority */
{
  int pri;
  char *p;

  p = tprintf("%s", atr_get(player, A_NICE));

  if (p && *p)
    pri = atoi(atr_get(player, A_NICE));
  else
  {
    if(Typeof(cause) == TYPE_PLAYER)
    {
      pri = 0;
    }
    else
      pri = 1;
  }

  parse_que_pri(player, command, cause, pri);
  
/*
  s = tprintf("%s", command);
  while ((r = (char *)parse_up(&s, ';')))
  {
    big_que(player, r, cause, pri, 0);
  }
*/
}

static int add_to(player, am)
dbref player;
int am;
{
  long num;
  char buff[200];

  player = db[player].owner;
  num = atol(atr_get(player, A_QUEUE));
  num += am;
  if (num)
    sprintf(buff, "%ld", num);
  else
    *buff = 0;
  atr_add(player, A_QUEUE, buff);
  return (num);
}

static void big_que(player, command, cause, pri, wait)
dbref player;
char *command;
dbref cause;
int pri;
time_t wait;
{
  int a, set, flag;
  BQUE *tmp, *dah;

  if (player == NOTHING) return;

  /* if (!big) big=nza_open(1); */
  if (db[player].flags & HAVEN)
    return;
  /* make sure player can afford to do it */
  if (!payfor(player, queue_cost + (((rand() & queue_loss) == 0) ? 1 : 0)))
  {
    notify(db[player].owner, "Not enough money to queue command.");
    return;
  }
  if (add_to(player, 1) > max_queue)
  {
    notify(db[player].owner,
	   tprintf("Run away object (%s), commands halted",
		   unparse_object(db[player].owner, player)));
    log_important(
	   tprintf("Run away object (%s), commands halted",
		   unparse_object(db[player].owner, player)));
    do_halt_player(db[player].owner, "");

    if(db[player].pows[0] != CLASS_DIR)
    {
      /* if the object is not a director's, we set it to */
      /* haven - also means no command execution allowed */
      db[player].flags |= HAVEN; 
    }

    return;
  }
  tmp = (BQUE *) malloc(sizeof(BQUE) + strlen(command) + 1);
  if (tmp==NULL) 
  {
    add_to(player,-1); /* no entry made so subtract 1 again */
    do_halt_player(player, "");
    log_io(tprintf("[%s] QUEUE: halted: %s (lack of memory).", unparse_object(db[player].owner,player)) );
    if(db[player].pows[0] != CLASS_DIR)
    {
      /* if the object is not a direcotrs, we set it to */
      /* haven -also means no command execution allowed */
      db[player].flags |= HAVEN; 
    }
    return; /* forget the rest as well */
  }

  strcpy(Astr(tmp), command);
  tmp->player = player;
  tmp->next = NULL;
  tmp->prev = NULL;
  tmp->cause = cause;
  tmp->pri = pri;
  tmp->wait = now + wait;
  tmp->pid = get_pid();
  for (a = 0; a < 10; a++)
  {
    if (!wptr[a])
    {
      tmp->env[a] = NULL;
    }
    else
    {
      char *p = (char *)malloc(strlen(wptr[a]) + 1);
      tmp->env[a]=p;
      if (p!=NULL) 
      {
        strcpy(tmp->env[a],wptr[a]);
      } 
      else 
      {
        do_halt_player(player, "");
        log_io(tprintf( "QUEUE: halted: %s (lack of memory).", unparse_object(db[player].owner,player)) );
        return; /* forget the rest as well */
      }
    }
  }

  if (qfirst)
  {
    BQUE *my_qlast = NULL; /* last 'dah' processed - use to reassemble queue. */

    for (dah = qfirst, set = 0; (flag != 1) && (set != 1); dah = dah->next)
    {
      if ((dah != qfirst) && (!dah->prev))
      {
        log_error("Ouch! We've got a broken queue! Run for cover, it's repair time.");
        if (my_qlast)
        {
          dah->prev = my_qlast;
        }
        else
        {
          qfirst = dah;
        }
      }
      if ((tmp->pri > dah->pri) && (tmp->wait < dah->wait))
      {
        if (dah == qfirst) 
        {
          tmp->next = qfirst;
          qfirst = tmp;
          set = 1;
        }
        else
        {
          (dah->prev)->next = tmp;
          tmp->next = dah->next;
          tmp->prev = dah->prev;
          dah->prev = tmp;
          set = 1;
        }
      }

      if (dah->next == NULL)
      {
        dah->next = tmp;
        tmp->prev = dah;
        dah = tmp;
        qlast = tmp;
        set = 1;
      }
      my_qlast = dah;
    }

    if (set == 0)
    {
      tmp->prev = qlast;
      qlast->next = tmp;
      qlast = tmp; 
      qlast->next = tmp;
    }
  } 
  else
  {
    qlast = qfirst = tmp;
  }

  do_jobs(-20);

}

void do_jobs(int pri)
{
  while (qfirst && (qfirst->pri <= pri) && do_top());
}

void wait_que(player, wait, command, cause)
dbref player;
int wait;
char *command;
dbref cause;
{

  int pri;
  char *p;

  /* make sure player can afford to do it */
  if (!payfor(player, queue_cost + (((rand() & queue_loss) == 0) ? 1 : 0)))
  {
    notify(player, "Not enough money to queue command.");
    return;
  }

  p = tprintf("%s", atr_get(player, A_NICE));

  if (p && *p)
  {
    pri = atoi(atr_get(player, A_NICE)) + 5;
    if (pri > 20)
    {
      pri = 20;
    }
  }
  else
  {
    if(Typeof(cause) == TYPE_PLAYER)
    { 
      pri = 5;
    }
    else
      pri = 6;
  }


  big_que(player, command, cause, pri, wait);

}

/* call every second to check for wait queue commands */
void do_second()
{
  do_top();

  return;
}


int test_top()
{
  return (qfirst ? 1 : 0);
}

/* execute one command off the top of the queue */
int do_top()
{
  int b=0; 
  BQUE *tmp;

  dbref player;

  tmp = qfirst;

  while ((tmp) && (tmp->wait > now))
  {
    tmp = tmp->next;
  }

  if (!tmp)
  {
    return 0;
  }

  if (valid_player(tmp->player) && !(db[tmp->player].flags & GOING))
  {
    giveto(tmp->player, queue_cost);
    cplr = tmp->player;
    strcpy(ccom, Astr(tmp));
    add_to(player = tmp->player, -1);
    tmp->player = NOTHING;
    if (!(db[player].flags & HAVEN))
    {
      char buff[1030];

      int a;

      for (a = 0; a < 10; a++)
	wptr[a] = tmp->env[a];

      log_command(tprintf("Queue processing: %s (pri: %d)", Astr(tmp), tmp->pri));

      func_zerolev();
      pronoun_substitute(buff, tmp->cause, Astr(tmp), player);
      inc_qcmdc();		/* increment command stats */
      process_command(player, buff + strlen(db[tmp->cause].name), tmp->cause);
    }
  }

  if ( (tmp == qfirst) && (tmp == qlast) )
  {
    qfirst = qlast = NULL;
  } 
  else
  {
    if (tmp == qfirst)
    {
      qfirst = qfirst->next;
      if (qfirst)
        qfirst->prev = NULL;
      tmp->next = NULL;
    }
    else if (tmp == qlast)
    {
      qlast = qlast->prev;
      tmp->prev = NULL;
      qlast->next = NULL;
    }
    else
    {
      tmp->prev->next = tmp->next;
      tmp->next->prev = tmp->prev;
      tmp->next = tmp->prev = NULL;
    }

  }

  free_pid(tmp->pid);

  for (b = 0; b < 10; b++)
  {
    if (tmp->env[b])
    {
      free(tmp->env[b]);
      tmp->env[b] = NULL;
    }
  }
  free(tmp);

  if (qfirst == NULL)
    qlast = NULL;
  return (1);
}

/* tell player what commands they have pending in the queue */
void do_queue(player)
dbref player;
{
  BQUE *tmp;
  int can_see = power(player, POW_QUEUE);

  if (qfirst)
  {
    notify(player, "PID   Player               Pr Wait  Command");
    for (tmp = qfirst; tmp != NULL; tmp = tmp->next)
    {
      if ((db[tmp->player].owner == db[player].owner) || can_see)
      {
        char mytmp[25];
  
        strncpy(mytmp, tprintf("[#%d %-20.20s", tmp->player, db[player].name), 20);
        mytmp[24] = '\0';
        if (strlen(mytmp) > 18)
          mytmp[19] = '\0';
        strcat(mytmp, "]");
        notify(player, tprintf("%5d %s %2d %5d %s", tmp->pid, mytmp, tmp->pri, (tmp->wait - now), Astr(tmp)));
  
      }
    }
  }
  else
    notify(player, "@ps: No processes in the queue at this time.");
  
/*  the old way.
  qwait = find_wait(qfirst);

  notify(player, "Immediate commands:");
  for (tmp = qfirst; tmp != qwait; tmp = tmp->next)
    if ((db[tmp->player].owner == db[player].owner) || can_see)
      notify(player, tprintf("%s:%s:%d", unparse_object(player, tmp->player), Astr(tmp), tmp->pri));

  notify(player, "@waited commands:");
  for (tmp = qwait; tmp; tmp = tmp->next)
    if ((db[tmp->player].owner == db[player].owner) || can_see)
      notify(player, tprintf("%s:%d:%s", unparse_object(player, tmp->player), (now - tmp->wait), Astr(tmp)));
*/
}

void do_haltall(player)         /* halt everything, period */
dbref player;
{
  BQUE *i, *next;
  int a;

  if (!db)
    return;

  if (!power(player, POW_SECURITY))
  {  
    notify(player, "You can't halt everything.");
    return;
  }  
  if (!qlast || !qfirst)
  {
    qfirst = qlast;
    log_error("broken queue. ouch,");
    return;
  }

  while (qfirst)
  {  
    next = qfirst->next;
    i = qfirst;
    for (a = 0; a < 10; a++)
    { 
      if (qfirst->env[a])
      { 
        free(qfirst->env[a]);
        qfirst->env[a] = NULL;
      }
    }
    giveto(qfirst->player, queue_cost);
    free_pid(qfirst->pid);
    qfirst = next;
    free(i);
  }  
     
  qfirst = NULL;
  qlast = NULL;
  notify(player, "@halt: Everything halted.");
}

void do_halt(dbref player, char *arg1, char *arg2)
{

/*   log_error(tprintf("arg1 = '%s' len = %d, arg2 = '%s'", arg1, strlen(arg1), arg2)); */
  if (!(arg1 && strlen(arg1)))
  {
    do_halt_player(player, arg2);
  }
  else if (!strcmp(arg1, "all"))
  {
    do_haltall(player);
  }
  else if ( (atoi(arg1) > 0) || (!strncmp(arg1, "0", 2)) )
  {
    do_halt_process(player, atoi(arg1));
  }
  else 
  {
    dbref lu;

    lu = lookup_player(arg1);

    if (lu > -1)
    {
      if (!power(player, POW_SECURITY))
      {
        notify(player, "@halt: You do not have the power.");
        return;
      }
      do_halt_player(lookup_player(arg1), arg2);
      notify(player, tprintf("@halt: Halted %s", unparse_object(player, lu)));
    }
    else
    {
      notify(player, "@halt: Invalid Syntax.");
    }
  }
}

void do_halt_process(dbref player, int pid)
{
  BQUE *point;
  

  for (point = qfirst; point; point = point->next)
  {
    if (point->pid == pid)
    {
      if ( (point->player == player) || (real_owner(point->player) == real_owner(player)) || (power(player, POW_SECURITY)) )
      {
        int a;

        if (point == qfirst)
        {
          qfirst = point->next;
          if (qfirst == NULL)
          {
            qlast = NULL;
          }
          else  
          {
            qfirst->prev = NULL;
          }
        } 
        else if (point == qlast)
        {
          qlast = qlast->prev;
          if (qlast)
            qlast->next = NULL;
        }
        else 
        {
          (point->next)->prev = point->prev;
          (point->prev)->next = point->next;
        }
    
        for (a = 0; a < 10; a++)
        {
    	if (point->env[a])
          {
    	  free(point->env[a]);
            point->env[a] = NULL;
          }
        }
        point->player = NOTHING;
        free_pid(point->pid);
        free(point);
        notify(player, tprintf("@halt: Terminated process %d", pid));
        return;
      }
      else
      {
        notify(player, "@halt: Sorry. You don't control that process.");
        return;
      }
    }
  }
  notify(player, "@halt: Sorry. That process ID wasn't found.");
}

/* remove all queued commands from a certain player */
void do_halt_player(dbref player, char *ncom)
{
  BQUE *trail = NULL, *point, *next;
  int num = 0;

  if (!(db[player].flags & QUIET))
    if (player == db[player].owner)
      notify(db[player].owner, "@halt: Player halted.");
    else if ( (!(db[db[player].owner].flags & QUIET)) && *ncom)
      notify(db[player].owner, tprintf("@halt: %s halted.",
				 unparse_object(db[player].owner, player)));
  /* remove stuff */
  for (point = qfirst; point; point = next)
  {
    if ((point->player == player) || (real_owner(point->player) == player))
    {
      int a;

      num--;
      giveto(point->player, queue_cost);
      next = point->next;

/*
      if (trail)
	trail->next = next = point->next;
      else
	qfirst = next = point->next;
*/

      if (point == qfirst)
      {
        qfirst = point->next;
        if (qfirst == NULL)
        {
          qlast = NULL;
        }
        else  
        {
          qfirst->prev = NULL;
        }
      } 
      else if (point == qlast)
      {
        qlast = qlast->prev;
        if (qlast)
          qlast->next = NULL;
      }
      else 
      {
        (point->next)->prev = point->prev;
        (point->prev)->next = point->next;
      }

      for (a = 0; a < 10; a++)
      {
	if (point->env[a])
        {
	  free(point->env[a]);
          point->env[a] = NULL;
        }
      }
      point->player = NOTHING;
      free_pid(point->pid);
      free(point);
    }
    else
      next = (trail = point)->next;
  }


  if (db[player].owner == player)
    atr_add(player, A_QUEUE, "");
  else
    add_to(player, num);
  if (*ncom)
    parse_que(player, ncom, player);
}


BQUE *find_wait(BQUE *queue)
{
  BQUE *tmp;

  for (tmp = queue; tmp != NULL; tmp=tmp->next)
  {
    if (now < tmp->wait)
    {
      break;
    }
  }

  return tmp;
}

static char pid_list[32768]; /* static buffer list. */

int get_pid(void)
{
  static char *p; /* static pointer to next pid to be used. */

  if (p == NULL)  /* if p's undefined then it's the first time through. */
  {
    init_pid();
    p = pid_list;
  }

  *p = ' ';
  p = p + strlen(p);

  if ((p - pid_list - 1) > 32767)
  {
    p = pid_list;
    p = p + strlen(p);
    if ((p - pid_list - 1) > 32767)
    {
      log_error("OUT OF PIDS! By God, someone will pay.");
      return (-1);
    }
  }
  return (p - pid_list - 1);
}

void init_pid(void)
{
  int x;

  for (x = 0; x < 32768; x++)
  {
    pid_list[x] = '\0';
  }
  return;
}

void free_pid(int pid)
{
  if (pid > -1)
    pid_list[pid] = '\0';
/*   log_error(tprintf("freed pid %d", pid)); */
}
