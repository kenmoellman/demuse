/* mail.c
 *
 *  This file has been heavily modified by me, wm, in order to make this code
 * more readable and to make the code more efficient, in addition to adding a
 * number of features.    - wm
 */


#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "externs.h"


#include "db.h"
#include "mail.h"

long mdb_alloc;
//static long mdb_first_free;
long mdb_first_free;


#ifdef __GNUC__
  __inline__
#endif
  mdbref get_mailk(player)
dbref player;
{
  char *i;

  if (!*(i = atr_get(player, A_MAILK)))
    return NOMAIL;
  else
    return atol(i);
}

#ifdef __GNUC__
  __inline__
#endif
void set_mailk(player, mailk)
dbref player;
mdbref mailk;
{
  char buf[20];

  sprintf(buf, "%ld", mailk);
  atr_add(player, A_MAILK, buf);
}

#ifdef __GNUC__
__inline__
#endif
long mail_size(player)
dbref player;
{
  long size, j;

  for (size = 0, j = get_mailk(player); j != NOMAIL; j = mdb[j].next)
    size += sizeof(struct mdb_entry) + strlen(mdb[j].message) + 1;

  return size;
}

void check_mail(player, arg2)
dbref player;
char *arg2;
{
  dbref target;

  target = lookup_player(arg2);
  if (target == NOTHING)
    target = player;
  if (((get_mailk(player) != NOMAIL) && (target == player)) || (get_mailk(target)))
/*  if (get_mailk(player) != NOMAIL) */
  {
    long read = 0, new = 0, tot = 0;
    char buf[1024];

    mdbref i;

    if (target == player)
    {
      for (i = get_mailk(target); i != NOMAIL; i = mdb[i].next)
      {
	if (mdb[i].flags & MF_READ)
	  read++;
	if (mdb[i].flags & MF_NEW)
	  new++;
	if (!(mdb[i].flags & MF_DELETED))
	  tot++;
      }
      sprintf(buf, "|W!++mail:| You have |Y!+%ld| message%s.", tot, (tot == 1) ? "" : "s");
      if (new)
      {
	sprintf(buf + strlen(buf), " |G!+%ld| of them %s new.", new, (new == 1) ? "is" : "are");
	if ((tot - read - new) > 0)
	  sprintf(buf + strlen(buf) - 1, "; |M!+%ld| other%s unread.", tot - read - new, (tot - read - new == 1) ? " is" : "s are");
      }
      else if ((tot - read) > 0)
	sprintf(buf + strlen(buf), " %ld of them %s unread.", tot - read, (tot - read == 1) ? "is" : "are");
      notify(player, buf);
    }
    else
    {
      for (i = get_mailk(target); i != NOMAIL; i = mdb[i].next)
      {
	if (mdb[i].from == player)
	{
	  if (mdb[i].flags & MF_READ)
	    read++;
	  if (mdb[i].flags & MF_NEW)
	    new++;
	  if (!(mdb[i].flags & MF_DELETED))
	    tot++;
	}
      }
      sprintf(buf, "|W!++mail:| %s has |Y!+%ld| message%s from you.", db[target].cname, tot, (tot == 1) ? "" : "s");
      if (new)
      {
	sprintf(buf + strlen(buf), " |G!+%ld| of them %s new.", new, (new == 1) ? "is" : "are");
	if ((tot - read - new) > 0)
	  sprintf(buf + strlen(buf) - 1, "; |M!+%ld| other%s unread.", tot - read - new, (tot - read - new == 1) ? " is" : "s are");
      }
      else if ((tot - read) > 0)
	sprintf(buf + strlen(buf), " %ld of them %s unread.", tot - read, (tot - read == 1) ? "is" : "are");
      notify(player, buf);
    }
  }
}

long check_mail_internal(player,arg2)
dbref player;
char *arg2;
{
  dbref target;

  if(arg2 && *arg2)
  {
    target = lookup_player(arg2);
    if (target == NOTHING)
    {
      log_error(tprintf("+mail error: Invalid target in check_mail_internal! (%s)",arg2));
      return -1;
    }
  }
  else
  {
    target = player;
  }
  if (((get_mailk(player) != NOMAIL) && (target == player)) || (get_mailk(target)))
  {
    long tot = 0;

    mdbref i;

    if (target == player)
    {
      for (i = get_mailk(target); i != NOMAIL; i = mdb[i].next)
      {
        if ((!(mdb[i].flags & MF_READ)) && (!(mdb[i].flags & MF_DELETED)))
	    tot++;
      }
      return tot;
    }
    else
    {
      for (i = get_mailk(target); i != NOMAIL; i = mdb[i].next)
      {
	if (mdb[i].from == player)
	{
	  if ((!(mdb[i].flags & MF_READ)) && (!(mdb[i].flags & MF_DELETED)))
	    tot++;
	}
      }
      return tot;
    }
  }
  return((long)0);
}


mdbref grab_free_mail_slot()
{
  if (mdb_first_free != NOMAIL)
    if (mdb[mdb_first_free].message)
    {
      log_error("+mail's first_free's message isn't null!");
      mdb_first_free = NOMAIL;
    }
    else
    {
      mdbref z = mdb_first_free;

      mdb_first_free = mdb[mdb_first_free].next;
      return z;
    }
  if (++mdb_top >= mdb_alloc)
  {
    mdb_alloc *= 2;
    mdb = realloc(mdb, (size_t)(sizeof(struct mdb_entry) * mdb_alloc));
  }
  mdb[mdb_top - 1].message = NULL;
  return mdb_top - 1;
}

void make_free_mail_slot(i)
mdbref i;
{
  if (mdb[i].message)
    free(mdb[i].message);
  mdb[i].message = NULL;
  mdb[i].next = mdb_first_free;
  mdb_first_free = i;
}

void init_mail()
{
  mdb_top = 0;
  mdb_alloc = 512;
  mdb = malloc((size_t)(sizeof(struct mdb_entry) * mdb_alloc));

  mdb_first_free = NOMAIL;
}

void free_mail()
{
  long i;

  for (i = 0; i < mdb_top; i++)
    if (mdb[i].message)
      free(mdb[i].message);
  if (mdb)
    free(mdb);
}

static void send_mail_as(from, recip, message, when, flags)
dbref from, recip;
char *message;
time_t when;
int flags;
{
  mdbref i, prev = NOMAIL;
  long msgno = 1;

  for (i = get_mailk(recip); i != NOMAIL; i = mdb[i].next)
    if (mdb[i].flags & MF_DELETED)
    {
      /* we found a spot! */
      break;
    }
    else
    {
      prev = i;
      msgno++;
    }
  if (i == NOMAIL)
  {
    /* sigh. no deleted messages to fill in. we'll have to tack a */
    /* new one on the end. */
    if (prev == NOMAIL)
    {
      /* they've got no mail at all. */
      i = grab_free_mail_slot();

      set_mailk(recip, i);
    }
    else
    {
      mdb[prev].next = i = grab_free_mail_slot();
    }
    mdb[i].next = NOMAIL;
  }
  mdb[i].from = from;
  mdb[i].date = when;
  mdb[i].flags = flags;
  SET(mdb[i].message, message);

  if (from != NOTHING)
  {
    if (could_doit(from, recip, A_LPAGE))
      notify(recip, tprintf("+mail: You have new +mail from %s (message number %ld)", unparse_object(recip, from), msgno));
  }
  else
    notify(recip, tprintf("+mail: You have new mail (message number %ld).", msgno));
}

static void send_mail(from, recip, message)
dbref from, recip;
char *message;
{
  send_mail_as(from, recip, message, now, MF_NEW);
}

long dt_mail(who)
dbref who;
{
  mdbref i;
  long count = 0;

  if (Typeof(who) != TYPE_PLAYER)
    return -1;
  for (i = get_mailk(who); i != NOMAIL; i = mdb[i].next)
    count++;
  return count;
}

void do_mail(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  if (Typeof(player) != TYPE_PLAYER || Guest(player))
  {
    notify(player, "Sorry, only real players can use mail.");
    return;
  }

  if (!string_compare(arg1, "delete"))
  {
    del_msg(player, arg1, arg2);
  }
  else if (!string_compare(arg1, "undelete"))
  {
    del_msg(player, arg1, arg2);
  }
  else if (!string_compare(arg1, "check"))
  {
    /* check your +mail status  */
    check_mail(player, arg2);
  }
  else if (!string_compare(arg1, "read"))
  {
    reading_msg(player, arg1, arg2);
  }
  else if (!string_compare(arg1, "purge"))
  {
    purge_mail(player, arg1, arg2);
  }
  else if ((!string_compare(arg1, "list")) || (!*arg1 && !*arg2))
  {				/* must be listing mail. */
    listing_mail(player, arg1, arg2);
  }
  else if (!*arg1 && *arg2)
  {
    notify(player, "+mail: You want to do what?");
  }
  else if (!string_compare(arg1, "write"))
  {
    /* temporarily disabled    write_mail_edit(player,arg1,arg2); */
    do_paste(player, "mail", arg2);
/*    notify(player, "+mail: Splat. This isn't enabled yet."); */
  }
  else if (*arg1 && *arg2)
  {				/* must be sending msg */
    sending_mail(player, arg1, arg2);
  }
  else if (*arg1 && !*arg2)
  {				/* must be reading a message. */
    reading_msg(player, "", arg1);
  }
  else
    log_error(tprintf("+mail: We shouldn't get here. arg1: %s. arg2: %s.", arg1, arg2));
}

void del_msg(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  /* delete a message. */
  dbref target;
  char *s, *t, *t2;
  long del = 0, leave_loop = 0;

  if (arg2 && ((s = strchr(arg2, ':')) == NULL))
  {

    if( ( (arg2 && *arg2) && (strlen(arg2) < 5) && (atol(arg2)) )  || (strlen(arg2) == 0) )

    {
/*      notify(player,tprintf("%ld",atol(arg2))); */
      target = player;
      s = arg2;
    }
    else  /* not just a number, and not a blank string. wtf is it then? :) */
    {
      target = lookup_player(arg2);   /* is it a player? */
      if(((!target) || target == NOTHING) && strlen(arg2))
      {  /* nope, not a player */
	if( ((t = strchr(arg2, '-')) == NULL) && ((t = strchr(arg2, '-')) == NULL) )
        {
          notify(player,tprintf("+mail: Invalid +mail target: (%s)",arg2));
          return;
        }
        else
        {
          target = player;
          s = arg2;
	}
      }
    }
  }
  else
  {
    *s++ = '\0';
    target = lookup_player(arg2);
    if((!target) || target == NOTHING)
    {
      notify(player,tprintf("+mail: Invalid +mail target: (%s)",arg2));
      return;
    }
  }
  if (s && *s)
  {
    for (t = strrchr(s, ','); !leave_loop; t = strrchr(s, ','))
    {
      t = strrchr(s, ',');
      if (t != NULL)
	*t++ = '\0';
      else
      {
	leave_loop = 1;
	t = s;
      }
      if ((t2 = strchr(t, '-')) == NULL)
      {
        if(strlen(t) < 5)
	  del += delete_msg(player, target, atol(t), atol(t), arg1);
        else
	{
          notify(player,tprintf("+mail: Invalid Message Number! (%s)", t));
	  return;
	}
      }
      else
      {
	*t2++ = '\0';
        if((strlen(t) < 5) && strlen(t2) < 5 )
        {
	  if (atol(t2) < atol(t))
	    del += delete_msg(player, target, atol(t2), atol(t), arg1);
	  else
	    del += delete_msg(player, target, atol(t), atol(t2), arg1);
        }
        else
	{
          notify(player,tprintf("+mail: Invalid Message Number Range! (%s - %s)", t, t2));
	  return;
	}
      }
    }
  }
  else
  {
    del = delete_msg(player, target, 0, 0, arg1);
  }

  if (target != player)
  {
#ifdef TARGET_DEL_PURGE
    purge_mail(player, arg1, tprintf("#%ld",target));
#endif /* TARGET_DEL_PURGE */
    notify(target,tprintf("+mail: %s deleted %ld of the messages they sent marked unread.",
                           db[player].cname, del));
  }


  notify(player, tprintf("+mail: %ld of %s's messages %sdeleted.", del, db[target].cname,
			 (!string_compare(arg1, "delete")) ? "" : "un"));
}

long delete_msg(player, target, beg, end, arg1)
dbref player;
dbref target;
mdbref beg;
mdbref end;
char *arg1;
{
  int flag;
  mdbref i;
  long num;
  long del = 0;

  if (!string_compare(arg1, "delete"))
    flag = MF_DELETED;
  else
    flag = MF_READ;

  num = beg;
  for (i = get_mailk(target); i != NOMAIL && 0 < --num; i = mdb[i].next) ;
  for (num = end - beg; (0 <= num--) || (end == 0); i = mdb[i].next)
  {
    if (i == NOMAIL)
      return (del);
    else if ((target == player) || ((mdb[i].from == player) && (mdb[i].flags != MF_READ)))
    {
      mdb[i].flags = flag;
      del++;
    }
  }

  recalc_bytes(target);
  return (del);
}

void purge_mail(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  /* purge deleted messages. */
  dbref target;
  mdbref i, next;
  mdbref prev = NOMAIL;

  if (strlen(arg2) != 0)
  {
    target = lookup_player(arg2);
    if((!target) || target == NOTHING)
    {
      notify(player,tprintf("+mail: Invalid +mail target: (%s)",arg2));
      return;
    }
  }
  else
  {
    target = player;
  }

  for (i = get_mailk(target); i != NOMAIL; i = next)
  {
    next = mdb[i].next;
    if ((target == player) || (mdb[i].from == player))
    {
      if (mdb[i].flags & MF_DELETED)
      {
        if (prev != NOMAIL)
          mdb[prev].next = mdb[i].next;
        else
          set_mailk(target, mdb[i].next);
        make_free_mail_slot(i);
      }
      else
        prev = i;
    }
    else
      prev = i;
  }
  if (strcmp(arg1,"purge") == 0)
    notify(player, tprintf("%s's deleted messages purged.", db[target].cname));
}

void reading_msg(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  long i, k;
  mdbref j = NOMAIL;
  char buf[1024];
  dbref target;
  char *s;

  if ((s = strchr(arg2, ':')) == NULL)
  {
    target = player;
    s = arg2;
  }
  else
  {
    if (strlen(arg2) != 0)
    {
      *s++ = '\0';
      target = lookup_player(arg2);
      if (!target) 
      {
        notify(player, tprintf("+mail: Invalid target (%s).",arg2));
        return;
      }
    }
    else
    {
      notify(player, "+mail: You MUST specify an arguement with +mail read!");
      return;
    }
  }

  if(strlen(s) > 4)
  {
    notify(player,tprintf("+mail: Invalid Range! (%s)",s));
    return;
  }

  k = i = atol(s);
  if (i > 0)
    for (j = get_mailk(target); j != NOMAIL && i > 1; j = mdb[j].next, i--) ;
  if (j == NOMAIL)
  {
    notify(player, tprintf("+mail: Invalid message number for %s.", db[target].cname));
    return;
  }
  if ((target != player) && (mdb[j].from != player))
  {
    notify(player, tprintf("+mail: Invalid message number for %s.", db[target].cname));
    return;
  }
  if ((mdb[j].from == player) || (target == player))
  {
    notify(player, tprintf("Message %ld:", k));
    notify(player, tprintf("To: %s", db[target].cname));
    notify(player, tprintf("From: %s", (mdb[j].from != NOTHING) ? unparse_object(player, mdb[j].from) : "The MUSE server"));
    sprintf(buf, "Date: %s", mktm(mdb[j].date, "D", player));
    notify(player, buf);
    strcpy(buf, "Flags:");
    if (mdb[j].flags & MF_DELETED)
      strcat(buf, " deleted");
    if (mdb[j].flags & MF_READ)
      strcat(buf, " read");
    if (mdb[j].flags & MF_NEW)
      strcat(buf, " new");
    notify(player, buf);
    if (power(player, POW_SECURITY))
      notify(player, tprintf("Mailk: %ld", j));

    notify(player, "");
    notify(player, mdb[j].message);
    if (target == player)
    {
      mdb[j].flags &= ~MF_NEW;
      mdb[j].flags |= MF_READ;
    }
  }
}				/* end reading_msg */

void listing_mail(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  dbref target;
  mdbref j;
  long i = 1;
  char buf[1024];

  if(arg2 && *arg2)
  {
    target = lookup_player(arg2);
    if ((target == NOTHING) || (target == default_room))
    {
      notify(player,tprintf("+mail: Invalid target! (%s)",arg2));
      return;
    }
  }
  else
  {
    target = player;
  }

  sprintf(buf, "|W!+------>| |B!++mail| |W!+for| %s", db[target].cname);
  if (player != target)
    strcat(buf, tprintf(" |W!+from| %s", db[player].cname));
    strcat(buf, " |W!+<------|");
  notify(player, buf);
  for (j = get_mailk(target); j != NOMAIL; j = mdb[j].next, i++)
  {
    char status = 'u';

    if (mdb[j].flags & MF_DELETED)
      status = 'd';
    else if (mdb[j].flags & MF_NEW)
    {
      status = '*';
      if (player == target)
	mdb[j].flags &= ~MF_NEW;
    }
    else if (mdb[j].flags & MF_READ)
      status = ' ';
    if ((target == player) || (mdb[j].from == player))
    {
      sprintf(buf, "%5ld) %c %s %s", i, status, unparse_object(player, mdb[j].from), mktm(mdb[j].date, "D", player));
      notify(player, buf);
    }
  }
  notify(player, "");
};				/* end listing mail */

void sending_mail(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  dbref recip;

  recip = lookup_player(arg1);
  if (recip == NOTHING || Typeof(recip) != TYPE_PLAYER)
  {
    notify(player, "I haven't a clue who you're talking about.");
    return;
  }
  else if (db[recip].i_flags & I_QUOTAFULL)
  {
    notify(player, "That player has insufficient quota.");
    return;
  }
  else if (db[player].i_flags & I_QUOTAFULL)
  {
    notify(player, "You have insufficient quota.");
    return;
  }
  else if (!could_doit(player, recip, A_LPAGE))
  {
    notify(player,
           tprintf("|W!++mail:| %s is not accepting pages (and therefore, not +mail either).", spname(recip)));
    if (*atr_get(recip, A_HAVEN))
    {
      notify(player, tprintf("Haven message from %s: %s",
                             spname(recip),
                             atr_get(recip, A_HAVEN)));
      return;
    }
  }
  else if (!could_doit(recip, player, A_LPAGE))
  {
    notify(player,
           tprintf("|W!++mail:| %s is not allowed to page you, therefore, you can't +mail them.", spname(recip)));
    return;
  }

  send_mail(player, recip, arg2);
  recalc_bytes(recip);

  notify(player, tprintf("+mail: You mailed %s with:\n%s\n----==----", unparse_object(player, recip), arg2));
}				/* end sending_mail */

void write_mail(f)
FILE *f;
{
  dbref d;
  mdbref i;

  for (d = 0; d < db_top; d++)
    if ((d == 0 || Typeof(d) == TYPE_PLAYER) && (i = get_mailk(d)) != NOMAIL)
      for (; i != NOMAIL; i = mdb[i].next)
	if (!(mdb[i].flags & MF_DELETED))
        {
	  atr_fputs(tprintf("+%ld:%ld:%ld:%d:%s", mdb[i].from, d, mdb[i].date, mdb[i].flags, mdb[i].message), f);
          fputc('\n', f); 
        }
}

void read_mail(f)
FILE *f;
{
  char buf[2048];
  dbref to;
  dbref from;
  time_t date;
  int flags;
  char message[1024];
  char *s;
  int exitflag = 0;

    while ((strlen(atr_fgets(buf, 2048, f))) && exitflag == 0 && !feof(f))
    { 
      if (buf[strlen(buf) - 1] == '\n')
        buf[strlen(buf) - 1] = '\0';
      else
        exitflag = 1;

      if (*buf == '+')
      { 
        from = atol(s = buf + 1);
        if ((s = strchr(buf, ':')))
        { 
          to = atol(++s);
          if ((s = strchr(s, ':')))
          { 
            date = atol(++s);
            if ((s = strchr(s, ':')))
            { 
              flags = atoi(++s);
              if ((s = strchr(s, ':')))
              { 
                strcpy(message, ++s);
/*
                if ((s = strchr(message, '\n')))
                { 
                  *s = '\0';
*/
                /* a good one. we just ignore bad ones. */
                  send_mail_as(from, to, message, date, flags);
/*
                }
*/
              }
            }
          }
        }
      }
    }
}



#ifdef SHRINK_DB
void remove_all_mail()
{
  dbref i;
  char dah[5];

/* i < 3999 -- make sure that you have 4000 objects. it'd *
 * be wise to change this if you tinker with shrink_db    */
  for (i = 0; i < 3999; ++i)
    if (Typeof(i) == TYPE_PLAYER)
    {
      do_mail(i, "delete", "");
      sprintf(dah, tprintf("#%ld", i));
      do_mail(i, "purge", dah);
    }

}
#endif


