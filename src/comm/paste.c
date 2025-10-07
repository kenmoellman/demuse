/**********************************************************
 *
 *  paste.c  --  MAZE prerelease code  --  (c) 1998  itsme
 *
 *    This code was stolen from MAZE.  It works perfectly,
 *  unlike the @paste in tm97.  I believe itsme wrote this
 *  code and will give him credit unless I hear otherwise.
 *
 *    This code is used with permission from itsme and use
 *  of this code in anything other than this release of
 *  MUSE should first be cleared by itsme.  Thank you  -wm
 *
 *  Revisions:
 *	12/07/1998	fixed problem with channel pasting
 *                         existed only in tm97 comsys.
 *********************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "db.h"
#include "externs.h"

char *strip_lead(char *);
void do_paste_int(dbref, char *, char *, int);

/*
char *name(int);

char *name(int temp)
{
  return db[temp].cname;
}
*/

struct pastey
{
  char *str;
  struct pastey *next;
};

struct paste_struct
{
  dbref player;
  dbref who;
  int flag; /* 0 = ignore, 1 = @paste mail */
  int code;
  struct pastey *paste;
  ATTR *attr;
  struct paste_struct *next;
};

typedef struct paste_struct PASTE;

PASTE *paste_stack = NULL;

static PASTE *find_paste(player)
dbref player;
{
  PASTE *p;

  for (p = paste_stack; p; p = p->next)
    if (p->player == player)
      return (p);

  return (NULL);
}

char is_pasting(player)
dbref player;
{
  PASTE *p;

  for (p = paste_stack; p; p = p->next)
    if (p->player == player)
      return (1);

  return (0);
}

static void add_to_stack(dbref player, dbref who, ATTR *attr, int code, int flag)
{
  PASTE *new;

  new = (PASTE *) malloc(sizeof(PASTE));

  if (!new)
  {
    log_error("Out of memory!");
    exit_nicely(1);
  }

  new->next = paste_stack;
  paste_stack = new;

  new->player = player;
  new->who = who;
  new->paste = NULL;
  new->attr = attr;
  new->code = code;
  new->flag = flag;
}

void remove_paste(player)
dbref player;
{
  PASTE *p, *pprev = NULL;
  struct pastey *q, *qnext;

  for (p = paste_stack; p; p = p->next)
  {
    if (p->player == player)
      break;
    pprev = p;
  }

  if (!p)
    return;

  for (q = p->paste; q; q = qnext)
  {
    qnext = q->next;
    free(q);
  }

  if (!pprev)
    paste_stack = p->next;
  else
    pprev->next = p->next;

  if (p == paste_stack)
    paste_stack = NULL;

  free(p);
}

void do_paste(dbref player, char *arg1, char *arg2)
{
  do_paste_int(player, arg1, arg2, (int)0);
}

void do_pastecode(dbref player, char *arg1, char *arg2)
{
  do_paste_int(player, arg1, arg2, (int)1);
}

void do_paste_int(dbref player, char *arg1, char *arg2, int code)
{
  dbref who;
  ATTR *attr = NULL;
  int flag = 0;

  if (is_pasting(player))
  {
    notify(player, "Clearing old paste, starting fresh.");
    remove_paste(player);
  }

  if (!*arg1)
    who = db[player].location;
  else
  {
/* See if they're pasting to a channel or +mail */
    if (*arg2)
    {
      if (string_prefix("channel", arg1))
      {
        who = lookup_channel(arg2);

        if (who == NOTHING)
        {
          notify(player, "@paste channel: Channel doesn't exist.");
          return;
        }

	if (is_on_channel(player, db[who].name) < 0)
	{
	  notify(player,"@paste channel: You're not on that channel!");
	  return;
	}
      }
      else if (string_prefix("mail", arg1))
      {
/* temporary. *
	notify(player, "@paste mail feature not yet enabled.");
	return;
*/
        init_match(player, arg2, NOTYPE);
        match_me();
        match_here();
        match_neighbor();
        match_possession();
        match_exit();
        match_absolute();
        match_player(NOTHING, NULL);
  
        if ((who = noisy_match_result()) == NOTHING)
        {
	  notify(player, "Unknown target");
	  return;
        }
  
        if (!could_doit(player, who, A_LPAGE))
        {
  	  notify(player, "Sorry, that player is page-locked against you.");
  	  return;
        }
  #ifdef USE_BLACKLIST
        if ( ((!strlen(atr_get(real_owner(who), A_BLACKLIST))) && (!strlen(atr_get(real_owner(player), A_BLACKLIST)))) ||
             !((could_doit(real_owner(player), real_owner(who), A_BLACKLIST)) && (could_doit(real_owner(who), real_owner(player), A_BLACKLIST))) )
  #endif /* USE_BLACKLIST */
  
        {
          flag = 1;
        }
#ifdef USE_BLACKLIST
        else
        {
	  notify(player, "Sorry, there's an @blacklist in effect.");
	  return;
        }
#endif /* USE_BLACKLIST */

/* is this crap needed anymore?
        strcpy(arg1,"channel");
        strcpy(tmp_var,arg2);
        strcpy(arg2,"***Mymail***:");
        strcat(arg2,tmp_var);
        return;
 */
      }
      else
      {
	notify(player, "Illegal syntax!");
	return;
      }
    }
    else
    {
      char *p;

/* Check if they're pasting to an attribute */
      if ((p = strchr(arg1, '/')))
	*p++ = '\0';

      init_match(player, arg1, NOTYPE);
      match_me();
      match_here();
      match_neighbor();
      match_possession();
      match_exit();
      match_absolute();
      match_player(NOTHING, NULL);

      if ((who = noisy_match_result()) == NOTHING)
      {
	notify(player, "1Unknown target");
	return;
      }

      if (!could_doit(player, who, A_LPAGE))
      {
	notify(player, "Sorry, that player is page-locked against you.");
	return;
      }
#ifdef USE_BLACKLIST
      if ( ((!strlen(atr_get(real_owner(who), A_BLACKLIST))) && (!strlen(atr_get(real_owner(player), A_BLACKLIST)))) ||
           !((could_doit(real_owner(player), real_owner(who), A_BLACKLIST)) && (could_doit(real_owner(who), real_owner(player), A_BLACKLIST))) )
#endif /* USE_BLACKLIST */

      {

        if (p)
        {
	  if (!controls(player, who, POW_MODIFY) && p)
	  {
	    notify(player, perm_denied());
	    return;
	  }

	  if (!(attr = atr_str(player, who, p)))
	  {
	    notify(player, "No match.");
	    return;
	  }
        }
      }
#ifdef USE_BLACKLIST
      else
      {
	notify(player, "Sorry, there's an @blacklist in effect.");
	return;
      }
#endif /* USE_BLACKLIST */
    }
  }

  add_to_stack(player, who, attr, code, flag);

  if (!flag)
    notify(player, "Enter lines to be pasted. End input with a period (.) or type '@pasteabort'.");
  else
    notify(player, "Enter +mail message. End input with a period (.) or type '@pasteabort'.");
}

static void do_end_paste(player)
dbref player;
{
  PASTE *p;
  struct pastey *q;
  char buf[1024];

  if (!(p = find_paste(player)))
    return;

  if (p->attr)
  {
    if (!p->paste)
      atr_clr(p->who, p->attr);
    else
      atr_add(p->who, p->attr, p->paste->str);

    notify(player, tprintf("%s - Set.", db[p->who].cname));
    return;
  }

  if (!p->flag)
  {
    sprintf(buf, "|W+----- ||C!+Begin @paste text from |%s |W+-----|",
  	  db[player].cname);
  
    if (Typeof(p->who) == TYPE_CHANNEL)
      com_send_as(db[p->who].name, buf, player);
    else if (Typeof(p->who) == TYPE_ROOM)
    {
      notify(player, buf);
      notify_in(p->who, player, buf);
    }
    else if (Typeof(p->who) == TYPE_PLAYER)
      notify(p->who, buf);
    else
      notify(player, "@paste: invalid target.");
  
    for (q = p->paste; q; q = q->next)
    {
      char *temp;
   
      if (p->code != 1)
      {
        temp = strip_lead(q->str);
      }
      else
      {
        temp = q->str;
      }
  
      if(temp && *temp)
      {
        if (Typeof(p->who) == TYPE_CHANNEL)
          com_send_as(db[p->who].name, temp, player);
        else if (Typeof(p->who) == TYPE_ROOM)
        {
          notify(player, temp);
          notify_in(p->who, player, temp);
        }
        else if (Typeof(p->who) == TYPE_PLAYER)
          notify(p->who, temp);
      }
    }
  
    sprintf(buf, "|W+----- ||C!+End @paste text from |%s |W+-----|",
  	  name(player));
  
    if (Typeof(p->who) == TYPE_CHANNEL)
      com_send_as(db[p->who].name, buf, player);
    else if (Typeof(p->who) == TYPE_ROOM)
    {
      notify(player, buf);
      notify_in(p->who, player, buf);
    }
    else if (Typeof(p->who) == TYPE_PLAYER)
    {
      notify(p->who, buf);
      notify(player, tprintf("@paste text sent to %s.",
  			   unparse_object(player, p->who)));
    }
  }
  else  /* +mail paste! */
  {
    char newmsg[65535] = "";
    
    for (q = p->paste; q; q = q->next)
    {
      char *temp;
  
      if (p->code != 1)
      {
        temp = strip_lead(q->str);
      }
      else
      {
        temp = q->str;
      }
  
      if(temp && *temp)
      {
        strncat(newmsg, q->str, (65530 - strlen(newmsg)));
        if (q->next)
          strcat(newmsg, "\n");
      }
    }
    do_mail(player, tprintf("#%d", p->who), newmsg);
  }
}


char *strip_lead(char *input)
{
  char *newptr;

  newptr = input;

  while ( (newptr && *newptr) && (*newptr == ' ') )
  {
    newptr++;
  }

  return (newptr);
}
  

void add_more_paste(player, str)
dbref player;
char *str;
{
  PASTE *p;
  struct pastey *q, *qnew, *qprev = NULL;

  if (!strcmp(str, "."))
  {
    do_end_paste(player);
    remove_paste(player);
    return;
  }

  if (!string_compare("@pasteabort", str))
  {
    remove_paste(player);
    notify(player, "@paste aborted.");
    return;
  }

  if (!(p = find_paste(player)))
    return;

  if (p->attr)
  {
    if (!p->paste)
    {
      p->paste = (struct pastey *)malloc(sizeof(struct pastey));

      p->paste->str = (char *)malloc(strlen(str) + 1);
      *p->paste->str = '\0';
      p->paste->next = NULL;
    }
    else
      p->paste->str = (char *)realloc(p->paste->str, strlen(p->paste->str) + strlen(str) + 1);

    strcpy(p->paste->str + strlen(p->paste->str), str);

    return;
  }

  qnew = (struct pastey *)malloc(sizeof(struct pastey));

  if (!qnew)
  {
    log_error("Out of memory!");
    exit_nicely(1);
  }

  qnew->str = (char *)malloc(strlen(str) + 1);

  if (!qnew->str)
  {
    log_error("Out of memory!");
    exit_nicely(1);
  }

  strcpy(qnew->str, str);
  qnew->next = NULL;

  for (q = p->paste; q; q = q->next)
    qprev = q;

  if (!qprev)
    p->paste = qnew;
  else
    qprev->next = qnew;
}

void do_pastestats(player, arg)
dbref player;
char *arg;
{
  PASTE *p;
  struct pastey *q;
  int i, size;
  char buf[1024];
  int num_pastes = 0;

  if (!power(player, POW_REMOTE))
  {
    notify(player, perm_denied());
    return;
  }

  if (!paste_stack)
  {
    notify(player, "There are no @paste texts being created.");
    return;
  }

  if (!*arg)
  {
    for (p = paste_stack; p; p = p->next)
    {
      size = 0;

      for (q = p->paste; q; q = q->next)
	size += strlen(q->str) + 1;

      if (p->who == NOTHING)
	strcpy(buf, "NOTHING");
      else
      {
	strcpy(buf, name(p->who));

	if (p->attr)
	  sprintf(buf + strlen(buf), "/%s", p->attr->name);
	if (Typeof(p->who) == TYPE_CHANNEL)
	  sprintf(buf + strlen(buf), "CHANNEL %s", db[p->who].cname);
      }

      notify(player, tprintf("%d: %s -> %s: %d bytes",
			     ++num_pastes, name(p->player), buf, size));
    }
    return;
  }

  for (p = paste_stack; p; p = p->next)
    num_pastes++;

  if (atoi(arg) < 1 || atoi(arg) > num_pastes)
  {
    notify(player, tprintf("Valid @pastes: 1 - %d", num_pastes));
    return;
  }

  i = 0;

  for (p = paste_stack; p; p = p->next)
  {
    if (++i == atoi(arg))
      break;
  }

  if (!p)			/* Shouldn't happen */
    return;

  size = 0;

  for (q = p->paste; q; q = q->next)
    size += strlen(q->str) + 1;

  if (p->who == NOTHING)
    strcpy(buf, "NOTHING");
  else
  {
    strcpy(buf, name(p->who));

    if (p->attr)
      sprintf(buf + strlen(buf), "/%s", p->attr->name);
    if (Typeof(p->who) == TYPE_CHANNEL)
      sprintf(buf + strlen(buf), "CHANNEL %s", db[p->who].cname);
  }

  notify(player, buf);
  notify(player, "|B+------ ||W+BEGIN ||B+------|");

  for (q = p->paste; q; q = q->next)
    notify(player, q->str);

  notify(player, "|B+------  ||W+END  ||B+------|");
}
