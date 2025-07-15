/* board.c  --  Ken C. Moellman, Jr.  -- 03/20/2000
 *
 *  This file has been built based upon mail.c and works with the mail database
 * by placing +b messages in object #0, which is a Room by default.  If you  
 * change that, you'll have trouble :)  -wm
 */


#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "externs.h"


#include "db.h"
#include "mail.h"

void board_list(dbref player);
void board_read(dbref, char *, char *);
void board_del(dbref, char *, char *);
long board_delete(dbref, dbref, mdbref, mdbref, char *); 
void board_purge(dbref, char *, char *);
void board_write(dbref, char *, char *);
void board_check(dbref);
void board_ban(dbref, char *, char *);
void board_unban(dbref, char *, char *);
long board_is_banned(dbref);




void do_board(player, arg1, arg2)
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
    board_del(player, arg1, arg2);
  }
  else if (!string_compare(arg1, "undelete"))
  {
    board_del(player, arg1, arg2);
  }
  else if (!string_compare(arg1, "check"))
  {
    /* check your +board status  */
    board_check(player);
  }
  else if (!string_compare(arg1, "read"))
  {
    board_read(player, arg1, arg2);
  }
  else if (!string_compare(arg1, "purge"))
  {
    board_purge(player, arg1, arg2);
  }
  else if (!string_compare(arg1, "ban"))
  {
    board_ban(player, arg1, arg2);
  }
  else if (!string_compare(arg1, "unban"))
  {
    board_unban(player, arg1, arg2);
  }
  else if ((!string_compare(arg1, "list")) || (!*arg1 && !*arg2))
  {                             /* must be listing board. */
    board_list(player);
  }
  else if (!*arg1 && *arg2)
  {
    notify(player, "+board: You want to do what?");
  }
  else if (!string_compare(arg1, "write"))
  {
    board_write(player,arg1,arg2);
  }
  else if (*arg1 && !*arg2)
  {                             /* must be reading a message. */
    board_read(player, "", arg1);
  }
  else
    notify(player, "+board: Command not recognized.  Try help +board for help");
}
  

void board_list(player)
dbref player;
{
  dbref target = default_room;
  mdbref j;
  long i = 1;
  char *s;

  notify(player, "|C++board|   |Y!+Author|               | |W!+Time/Date|           | Message");
  notify(player, "------------------------------+---------------------+------------------------");
  for (j = get_mailk(target); j != NOMAIL; j = mdb[j].next, i++)
  {
    char status = ' ';

    if (mdb[j].flags & MF_DELETED)
      status = 'd';

    if ( (status != 'd') || (player == 0) || (mdb[j].from == player) || (power(player, POW_BOARD)) )
    { 
      char mybuf[1024];
      char mybuf2[20];
      char *mybuf3;
      char spacebuf[40];
      int counter = 0;

      strncpy(spacebuf,"                                                  ", 39);
      strncpy(mybuf, truncate_color(db[mdb[j].from].cname, 20), 1024);
      counter = 20 - strlen(strip_color_nobeep(mybuf));
      if (counter > 0)
      {
        strncat(mybuf, spacebuf, counter);
      }

      strncpy(mybuf2, mktm(mdb[j].date, "D", player), 20);
      mybuf2[19]='\0';

      mybuf3=tprintf(truncate_color(mdb[j].message, 25));
      for (s = mybuf3 ; s ; )
      {
        s = strchr(mybuf3, '\n');
        if(s)
          *s = '\0';
      }

      notify(player, tprintf("%5ld) %c %s | %s | %-s", i, status, mybuf, mybuf2, mybuf3));
    }
  }
  notify(player, "---------------------- Use help +board for assistance -----------------------");
};				/* end listing board */

void board_read(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  long i, k;
  mdbref j = NOMAIL;
  char buf[1024];
  dbref target=default_room;
  char *s = arg2;
  char *myfrom;

  if (strlen(arg2) == 0)
  { 
    notify(player, "+board: You MUST specify an arguement with +board read!");
    return;
  }

  if(strlen(s) > 4)
  {
    notify(player,tprintf("+board: Invalid Range! (%s)",s));
    return;
  }

  k = i = atol(s);
  if (i > 0)
    for (j = get_mailk(target); j != NOMAIL && i > 1; j = mdb[j].next, i--) ;
  if (j == NOMAIL)
  {
    notify(player, "+board: Invalid message number.");
    return;
  }
  if ((mdb[j].flags & MF_DELETED) && ((target != player) && (mdb[j].from != player)))
  {
    notify(player, "+board: Invalid message number.");
    return;
  }
  notify(player, tprintf("Message %ld:", k));

  if (mdb[j].from == 0)
    myfrom = stralloc("From: The MUSE Server");
  else if ( (mdb[j].from == NOTHING) || (Typeof(mdb[j].from) != TYPE_PLAYER) )
    myfrom = stralloc("* UNKNOWN *");
  else
    myfrom = tprintf("From: %s", unparse_object(player, mdb[j].from));
  notify(player, myfrom);


  sprintf(buf, "Date: %s", mktm(mdb[j].date, "D", player));
  notify(player, buf);
  if ((mdb[j].from == player) || (power(player, POW_BOARD)))
  {
    if (mdb[j].flags & MF_DELETED)
    {
      notify(player, "Flags: deleted");
    }
  }
  if (power(player, POW_SECURITY) && (power(player, POW_BOARD)))
  {
    notify(player, tprintf("Mailk: %ld", j));
  }

  notify(player, "");
  notify(player, mdb[j].message);
}				/* end board_read */

void board_del(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  /* delete a message. */
  dbref target = default_room;
  char *s, *t, *t2;
  long del = 0, leave_loop = 0;

  if (board_is_banned(player) >= 0)
  { 
    notify(player, "+board: You have been banned from the +board.");
    return;
  } 

  if( ( (arg2 && *arg2) && (strlen(arg2) < 5) && (atol(arg2)) )  || (strlen(arg2) == 0) )
  {
    s = arg2;
  }
  else  /* not a number, and not a blank string. wtf is it then? :) */
  {
    if( ((t = strchr(arg2, '-')) == NULL) && ((t = strchr(arg2, ',')) == NULL) )
    {
      notify(player,tprintf("+board: Invalid +board target: (%s)",arg2));
      return;
    }
    else
    {
      s = arg2;
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
	  del += board_delete(player, target, atol(t), atol(t), arg1);
        else
	{
          notify(player,tprintf("+board: Invalid Message Number! (%s)", t));
	  return;
	}
      }
      else
      {
	*t2++ = '\0';
        if((strlen(t) < 5) && strlen(t2) < 5 )
        {
	  if (atol(t2) < atol(t))
	    del += board_delete(player, target, atol(t2), atol(t), arg1);
	  else
	    del += board_delete(player, target, atol(t), atol(t2), arg1);
        }
        else
	{
          notify(player,tprintf("+board: Invalid Message Number Range! (%s - %s)", t, t2));
	  return;
	}
      }
    }
  }
  else
  {
    del = board_delete(player, target, 0, 0, arg1);
  }

  notify(player, tprintf("+board: %ld messages %sdeleted.", del, 
			 (!string_compare(arg1, "delete")) ? "" : "un"));
}

long board_delete(player, target, beg, end, arg1)
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
    else if ((mdb[i].from == player) || (power(player, POW_BOARD)))
    {
      mdb[i].flags = flag;
      del++;
    }
  }

  recalc_bytes(target);
  return (del);
}

void board_purge(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  /* purge deleted messages. */
  dbref target=default_room;
  mdbref i, next;
  mdbref prev = NOMAIL;

  for (i = get_mailk(target); i != NOMAIL; i = next)
  {
    next = mdb[i].next;
    if ((power(player, POW_BOARD)) || (mdb[i].from == player))
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
    notify(player, "+board: deleted messages purged.");
}



void board_write(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  dbref recip = 0;
  mdbref i, prev = NOMAIL;
  long msgno = 1;


  if (board_is_banned(player) >= 0)
  { 
    notify(player, "+board: You have been banned from the +board.");
    return;
  } 
  else if (db[player].i_flags & I_QUOTAFULL)
  {
    notify(player, "You have insufficient quota.");
    return;
  }

  for (i = get_mailk(recip); i != NOMAIL; i = mdb[i].next)
  {
    prev = i;
    msgno++;
  }
  if (i == NOMAIL)
  {
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
  mdb[i].from = player;
  mdb[i].date = now;
  mdb[i].flags = MF_READ;
  SET(mdb[i].message, arg2);
  recalc_bytes(recip);

  notify(player, tprintf("+board: You wrote '%s' to the +board.", arg2));
}				/* end board_write */



void board_check(player)
dbref player;
{
  dbref target = default_room;
  long tot = 0;


  mdbref i;

  for (i = get_mailk(target); i != NOMAIL; i = mdb[i].next)
  {
    if (!(mdb[i].flags & MF_DELETED))
      tot++;
  }
  notify(player, tprintf("+board: The +board currently has %ld message%s.", tot, (tot == 1) ? "" : "s"));
}

void board_ban(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  if(!power(player,POW_BOARD))
  {
    notify(player, "+board: You do not have the power. sorry charlie.");
    return;
  }
  else
  {
    dbref target;

    target = lookup_player(arg2);
    if (target == NOTHING)
    {
      notify(player, tprintf("Invalid target: %s", arg2));
      return;
    }

    if (!*atr_get(player, A_LPAGE))
      atr_add(default_room, A_LPAGE, tprintf("#%ld",target));
    else
      atr_add(default_room, A_LPAGE, tprintf("#%ld&%s", target, atr_get(player, A_LPAGE)));
    notify(player, tprintf("%s has been banned from the +board.", db[target].cname));
    notify(target, "You have been banned from the +board.");
  }

}

void board_unban(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  if(!power(player,POW_BOARD))
  {
    notify(player, "+board: You do not have the power. sorry charlie.");
    return;
  }
  else
  {
    long i;
    char *end;
    char buf[1024];
    char buf2[1024];
    dbref target;

    target = lookup_player(arg2);
    if (target == NOTHING)
    {
      notify(player, tprintf("+board: Invalid target (%s)", arg2));
      return;
    }

    arg1++;

    i = board_is_banned(target);
    if (i < 0)
    { 
      notify(player, tprintf("+board: %s is no currently banned.", db[target].cname));
      return;
    }
    strcpy(buf, atr_get(default_room, A_LPAGE));
    end = strchr(buf+i+1, '&');

    if (!end)
      end = strchr(buf + i, '\0');
    else
      end++;
    strcpy(buf2, atr_get(default_room, A_LPAGE));
    strcpy(buf2 + i, end);
    if ((end = strchr(buf2, '\0')) && *--end == '&')
      *end = '\0';

    notify(target, "+board: You are now allowed to post to the +board.");
    notify(player, tprintf("+board: %s is now allowed to post on the +board.", db[target].cname));
    atr_add(default_room, A_LPAGE, buf2);
  }

}


long board_is_banned(dbref player)
{
  char *blist;
  char *blbegin;
  char buf[1025];
  char *string;

  if(could_doit(player, default_room, A_LPAGE))
  {
    sprintf(buf, "%s&", atr_get(default_room, A_LPAGE));
    blbegin = blist = stralloc(buf);
  
    string = tprintf("#%ld", player);
  
    while (*blist)
    {
      if (strchr(blist, '&'))
        *strchr(blist, '&') = '\0';
      if (!strcmp(blist, string))
        return (blist - blbegin);
      blist += strlen(blist) + 1;
    }
  }

  return -1;
}
  


/*  this sort of logic for banned stuff.
    if (board_is_banned(player) >= 0)
    { 
      notify(player, "You have been banned from the +board.");
      return;
    }
*/
