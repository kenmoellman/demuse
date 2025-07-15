/* $Id: com.c,v 1.3 1993/08/22 04:53:48 nils Exp $ */
  
#include "db.h"
#include "config.h"
#include "interface.h"
#include "externs.h"
#include "copyright.h"
#include <ctype.h>


static int is_banned P((dbref, char *));

void do_channel_create(dbref, char *);
void do_channel_op(dbref, char *);
void do_channel_lock(dbref, char *);
void do_channel_password(dbref, char *);
void do_channel_join(dbref, char *);
void do_channel_leave(dbref, char *);
void do_channel_default(dbref, char *);
void do_channel_alias(dbref, char *);
void do_channel_boot(dbref, char *);
void do_channel_list(dbref, char *);
void do_channel_search(dbref, char *);
void do_channel_log(dbref, char *);
void do_channel_ban(dbref, char *);
void do_channel_unban(dbref, char *);
void do_channel_color(dbref, char *);
char *find_channel_alias(dbref, char *);
char *find_channel_int(dbref, char *, int);
char *find_channel_only(dbref, char *);
int is_on_channel(dbref, char *);
int is_on_channel_only(dbref, char *);
int is_on_channel_int(dbref, char *, int);
void list_channels(dbref, dbref);
char *find_channel_color(dbref, char *);
int remove_from_channel(dbref, char *);
int is_in_attr(dbref, char *, ATTR *);
char *remove_from_attr(dbref, int, ATTR *);
char *remove_from_ch_attr(dbref, int);
static dbref hash_chan_function(char *);
int channel_onoff_chk(dbref, dbref);
void channel_onoff_set(dbref, char *, char *);

/* begin code taken from players.c */
#define CHANNEL_LIST_SIZE (1 << 12)	/* must be a power of 2 */

static dbref hash_chan_fun_table[256];
static int hft_chan_initialized = 0;
static struct pl_elt *channel_list[CHANNEL_LIST_SIZE];
static int pl_used = 0;

#define DOWNCASE(x) to_lower(x)

struct pl_elt
{
  dbref channel;			/* pointer to channel */
  /* key is db[channel].name */
  struct pl_elt *prev;
  struct pl_elt *next;
};

/* end code taken from players.c */


char *find_channel_alias(dbref player, char *channel)
{
  char *clist;
  char *clbegin;
  char *alias;
  char *s;

  clbegin = clist = tprintf("%s ", atr_get(player, A_CHANNEL));
  while (*clist)
  {
    if (strchr(clist, ' '))
      *strchr(clist, ' ') = '\0';	/* insert null after first channel
					   name */
    alias = strchr(clist, ':');
    if (alias && *alias)
    {
      *alias++ = '\0';
      s = strchr(alias, ':');
      if(s && *s)
      {
        *s++ = '\0';
      }
      if (!strcmp(strip_color_nobeep(clist), channel)) 
      {
        return (tprintf("%s", alias));
      }
      clist += strlen(clist) + strlen(alias) + 2;
    }
    else
      clist += strlen(clist) + 1;
  }
  return NULL;
}


char *is_channel_alias(dbref player, char *al)
{
  char *clist;
  char *clbegin;
  char *alias;
  char *s;

  clbegin = clist = tprintf("%s ", atr_get(player, A_CHANNEL));
  while (*clist)
  {
    if (strchr(clist, ' '))
      *strchr(clist, ' ') = '\0';	/* insert null after first channel
					   name */
    alias = strchr(clist, ':');
    if (alias && *alias)
    {
      *alias++ = '\0';

      s = strchr(alias, ':');
      if(s && *s)
      { 
        *s++ = '\0';
      }

      if (!strcmp(alias, al)) 
      {
        return (tprintf("%s", alias));
      }
      clist += strlen(clist) + strlen(alias) + 2;
    }
    else
      clist += strlen(clist) + 1;
  }
  return NULL;
}


char *find_channel(dbref player, char *chan)
{
  return (find_channel_int(player, chan, 1));
}

char *find_channel_only(dbref player, char *chan)
{
  return (find_channel_int(player, chan, 0));
}


char *find_channel_int(dbref player, char *chan, int level)
{
  char *clist;
  char *clbegin;
  char *alias;
  char *s;

  clbegin = clist = tprintf("%s ", atr_get(player, A_CHANNEL));
  while (*clist)
  {
    if (strchr(clist, ' '))
      *strchr(clist, ' ') = '\0';	/* insert null after first channel
					   name */
    alias = strchr(clist, ':');
    if (alias && *alias)
    {
      *alias++ = '\0';
      s = strchr(alias, ':');
      if(s && *s)
      { 
        *s++ = '\0';
      }

    }


    if ((!strcmp(strip_color_nobeep(clist), chan)) || ((level == 1) && (alias && *alias && (!strcmp(alias, chan)))))  /* check for com aliases too. yay. */
    {				/* if listitem = channel, return string index
				   * of channel. */
      if(lookup_channel(clist))
        return (tprintf("%s", clist));

    }
    if (alias && *alias)
    {
      if (s && *s)
      {
        clist += strlen(clist) + strlen(alias) + strlen(s) + 3;
      }
      else
      {
        clist += strlen(clist) + strlen(alias) + 2;
      }
    }
    else
    {
      clist += strlen(clist) + 1;	/* Otherwise, move beginning of clist past *
				   	end of first channel name. */
    }
  }
  return NULL;
}

int is_on_channel(dbref player, char *chan)
{
  if(chan && *chan)
    return (is_on_channel_int(player, chan, 1));
  else
    return -1;
}

int is_on_channel_only(dbref player, char *chan)
{
  if(chan && *chan)
    return (is_on_channel_int(player, chan, 0));
  else
    return -1;
}

int is_on_channel_int(dbref player, char *chan, int level)
{
  char *clist;
  char *clbegin;
  char *alias;
  char *s;

  clbegin = clist = tprintf("%s ", atr_get(player, A_CHANNEL));
  while (*clist)
  {
    if (strchr(clist, ' '))
      *strchr(clist, ' ') = '\0';	/* insert null after first channel
					   name */
#ifdef DEBUG
    notify(player,clist);
#endif

    alias = strchr(clist, ':');
    if (alias && *alias)
    {
      *alias++ = '\0';
      s = strchr(alias, ':');
      if(s && *s)
      { 
        *s++ = '\0';
      }

    }

    if ((!strcmp(strip_color_nobeep(clist), chan)) || ((level == 1) && (alias && *alias && (!strcmp(alias, chan)))))  /* check for com aliases too. yay. */
    {				/* if listitem = channel, return string index
				   * of channel. */
      if(lookup_channel(clist))
        return (clist - clbegin);
    }
    if (alias && *alias)
    {
      if (s && *s)
      {
        clist += strlen(clist) + strlen(alias) + strlen(s) + 3;
      }
      else
      {
        clist += strlen(clist) + strlen(alias) + 2;
      }
    }
    else
    {
      clist += strlen(clist) + 1;	/* Otherwise, move beginning of clist past *
				   	end of first channel name. */
    }
  }

  return -1;  /* nope. */
}




extern void com_send(channel, message)
char *channel;
char *message;
{
  com_send_int(channel, message, 0, 0);
}

void com_send_as(char *channel, char *message, dbref player)
{
  com_send_int(channel, message, player, 0);
}

void com_send_as_hidden(char *channel, char *message, dbref player)
{
  com_send_int(channel, message, player, 1);
}
  
extern void com_send_int(channel, message, player, hidden)
char *channel;
char *message;
dbref player;
int hidden;
{
  struct descriptor_data *d;
  char *output_str, *output_str2;


  for (d = descriptor_list; d; d = d->next)
  {
    if ((d) && (d->state == CONNECTED && d->player > 0 && is_on_channel_only(d->player, channel) >= 0) && (channel_onoff_chk(d->player, lookup_channel(channel)) == 1)) 
    {
      output_str = tprintf("[%s] %s", find_channel_color(d->player, channel), message);

      if ( ((!hidden) || could_doit(real_owner(d->player), real_owner(player), A_LHIDE)) 
#ifdef USE_BLACKLIST
        && ( ((!strlen(atr_get(real_owner(d->player), A_BLACKLIST))) && (!strlen(atr_get(real_owner(player), A_BLACKLIST)))) || 
             (!((could_doit(real_owner(player), real_owner(d->player), A_BLACKLIST)) && (could_doit(real_owner(d->player), real_owner(player), A_BLACKLIST)))) ) 
#endif /* USE_BLACKLIST */
         )
      {
        if ((db[d->player].flags & PUPPET) && (player > 0) && (player != d->player))
        {
          output_str2 = tprintf("%s  [#%ld/%s]", output_str, db[player].owner, atr_get(db[player].owner, A_ALIAS));
          output_str = output_str2;
        }

        output_str2 = add_pre_suf(d->player, 1, output_str, d->pueblo);
        output_str = output_str2;

        if (db[d->player].flags & PLAYER_NOBEEP)
        {
  	  if (db[d->player].flags & PLAYER_ANSI)
  	    queue_string(d, parse_color_nobeep(output_str, d->pueblo));
	  else
  	    queue_string(d, strip_color_nobeep(output_str));
        }
        else
        {
	  if (db[d->player].flags & PLAYER_ANSI)
	    queue_string(d, parse_color(output_str, d->pueblo));
	  else
	    queue_string(d, strip_color(output_str));
        }
        queue_string(d, "\n");
      }
    }
  }
}

static void com_who P((dbref, char *));
static void com_who(player, channel)
dbref player;
char *channel;
{
  struct descriptor_data *d;
  dbref channum;
  int hidden = 0;
  int vis = 0;

  channum = lookup_channel(channel);

  if (channum == NOTHING)
  {
    notify(player, "+channel: Sorry, this channel doesn't exist.");
    return;
  }
  if ( (db[channum].flags & DARK) &&
       ( (!controls(player, db[channum].owner, POW_CHANNEL)) &&
         (!group_controls(player, channum))
       )
     )
  {
    notify(player, "+channel: Sorry, this channel is set DARK.");
    return;
  }

  
  for (d = descriptor_list; d; d = d->next)
  {
    if (d->state == CONNECTED && d->player > 0 && is_on_channel_only(d->player, channel) >= 0)
    {
      if ( (could_doit(real_owner(d->player), real_owner(player), A_LHIDE))
#ifdef USE_BLACKLIST
        && ( ((!strlen(atr_get(real_owner(d->player), A_BLACKLIST))) && (!strlen(atr_get(real_owner(player), A_BLACKLIST)))) ||
             (!((could_doit(real_owner(player), real_owner(d->player), A_BLACKLIST)) && (could_doit(real_owner(d->player), real_owner(player), A_BLACKLIST)))) )
#endif /* USE_BLACKLIST */
         )  
      {  
        notify(player, tprintf("%s is on channel %s.",
			     unparse_object(player, d->player), channel));
        vis++;
      }
      else 
        hidden++;
    }
  }
  notify(player, tprintf("%d Visible and %d Hidden Players are on channel %s", vis, hidden, channel));
  notify(player, tprintf("--- %s ---", channel));
}

void do_com(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  char buf2[1024];
  dbref toplayer;
  char *nocolor;
  char *dispname;
  int onoff;
  char *curr;
  

  onoff = 0;
  if (!*arg1)
  {
    char *buf;
    char *alias;
    char *q;
    char *next;

    next = curr = buf = tprintf("%s ", atr_get(player, A_CHANNEL));

    next = strchr(curr, ' ');
    *next++ = '\0';

    alias = strchr(curr, ':');
    if (alias && *alias)
    {
      *alias++ = '\0';
      q = strchr(alias, ':');
      if (q && *q)
      {
        *q++ = '\0';
        onoff = atoi(q);
      }
      else
      {
        do_channel_alias(player, tprintf("%s:%s",curr,alias));
        onoff = 1;
      }
    }
    else
    {
      do_channel_alias(player, tprintf("%s:%s",curr,curr));
      onoff = 1;
    }
  }
  else
  {
    curr = arg1;
    onoff = channel_onoff_chk(player, lookup_channel(curr));
  }

  nocolor = tprintf("%s", strip_color_nobeep(curr));
  if (!*nocolor)
  {
    notify(player, "No channel.");
    return;
  }
  if (strchr(nocolor, ' '))
  {
    notify(player, "You're spacey.");
    return;
  }
  if (!string_compare(arg2, "who"))
    com_who(player, nocolor);
  else
  {
    char buf[2048];

#ifdef ALLOW_COM_NP
    if (Typeof(player) == TYPE_CHANNEL)
    { 
      notify(player, "+channel: Channels can't talk on channels. Imagine the Spam.");
      return;
    }
#else
    if (Typeof(player) != TYPE_PLAYER)
    { 
      notify(player, "+channel: Non-players cannot talk on channels. Sorry.");
      return;
    }
#endif

    if (is_banned(player, nocolor) >= 0)
    {
      notify(player, "+channel: You have been banned from that channel.");
      return;
    }

    if (lookup_channel(nocolor) == NOTHING)
    {
      notify(player, "+channel: Sorry.  You have old channels defined. Removing old channel..");
      do_channel_leave(player, nocolor);
      return;
    }
    if ( ( (db[lookup_channel(nocolor)].flags & HAVEN) || 
           (!could_doit(player, lookup_channel(nocolor), A_SLOCK))
         ) && 
         ( (!controls(player, db[lookup_channel(nocolor)].owner, POW_CHANNEL)) &&
           (!group_controls(player, lookup_channel(nocolor)))
         )
       ) 
    {
      notify(player,"+channel: You do not have permission to speak on this channel.");
      return;
    }

    if (!strcmp("on", arg2) || !strcmp("off", arg2))
    {
      channel_onoff_set(player, nocolor, arg2);
      return;
    }

    if (onoff != 1)
    {
      notify(player, tprintf("+channel: Channel %s is currently turned off. Sorry.", curr));
      return;
    }

    switch (*arg2)
    {
    case POSE_TOKEN:
    case NOSP_POSE:
      sprintf(buf, "%s%s %s", db[player].cname,
	      (*arg2 == NOSP_POSE) ? "\'s" : "", arg2 + 1);
      break;
    case TO_TOKEN:
      arg2++;
      strcpy(buf2, arg2);
      if (strchr(buf2, ' ') != NULL)
      {
	*strchr(buf2, ' ') = '\0';
	arg2 += strlen(buf2) + 1;
      }
      else
	arg2 += strlen(buf2);
      toplayer = lookup_player(buf2);
      if (toplayer != NOTHING)
      {
	strcpy(buf2, db[toplayer].cname);
	if (*arg2 == POSE_TOKEN)
	  sprintf(buf, "[to %s] %s %s", buf2, db[player].cname, arg2 + 1);
	else if (*arg2 == NOSP_POSE)
	  sprintf(buf, "[to %s] %s's %s", buf2, db[player].cname, arg2 + 1);
	else if (*arg2 == THINK_TOKEN)
	  sprintf(buf, "[to %s] %s . o O ( %s )", buf2, db[player].cname, arg2 + 1);
	else
	  sprintf(buf, "%s [to %s]: %s", db[player].cname, buf2, arg2);
      }
      else
	sprintf(buf, "%s: %c%s %s", db[player].cname, TO_TOKEN, buf2, arg2);
      break;
    case THINK_TOKEN:
      sprintf(buf, "%s . o O ( %s )", db[player].cname, arg2 + 1);
      break;
    default:
      if(strlen(atr_get(player, A_CTITLE)))
      {
        dispname = tprintf("%s <%s>", db[player].cname, atr_get(player, A_CTITLE));
      }
      else
      {
        dispname = db[player].cname;
      }

      sprintf(buf, "%s: %s", dispname, arg2);
      break;
    }

    com_send_int(nocolor, buf, player, 0);
    if (is_on_channel_only(player, nocolor) < 0)
      notify(player, "Your +com has been sent.");
  }
}

void do_channel(dbref player, char *arg1, char *arg2)
{

  if (!strncmp(arg1, "create", 7))
  {
    do_channel_create(player, arg2);
  }
  else if (!strncmp(arg1, "destroy", 8))
  {
    do_channel_destroy(player, arg2);
  }
  else if (!strncmp(arg1, "op", 3))
  {
    do_channel_op(player, arg2);
  }
  else if (!strncmp(arg1, "lock", 5))
  {
    do_channel_lock(player, arg2);
  }
  else if (!strncmp(arg1, "password", 9))
  {
    do_channel_password(player, arg2);
  }
  else if (!strncmp(arg1, "join", 5))
  {
    do_channel_join(player, arg2);
  }
  else if (!strncmp(arg1, "leave", 6))
  {
    do_channel_leave(player, arg2);
  }
  else if (!strncmp(arg1, "default", 8))
  {
    do_channel_default(player, arg2);
  }
  else if (!strncmp(arg1, "alias", 6))
  {
    do_channel_alias(player, arg2);
  }
  else if (!strncmp(arg1, "boot", 5))
  {
    do_channel_boot(player, arg2);
  }
  else if (!strncmp(arg1, "list", 5))
  {
    do_channel_list(player, arg2);
  }
  else if (!strncmp(arg1, "search", 7))
  {
    do_channel_search(player, arg2);
  }
  else if (!strncmp(arg1, "log", 4))
  {
    do_channel_log(player, arg2);
  }
  else if (!strncmp(arg1, "ban", 4))
  {
    do_channel_ban(player, arg2);
  }
  else if (!strncmp(arg1, "unban", 6))
  {
    do_channel_unban(player, arg2);
  }
  else if (!strncmp(arg1, "color", 6))
  {
    do_channel_color(player, arg2);
  }

/* old legacy stuff. -wm */
  else if (*arg1 == '\0')
  {
    do_channel_list(player, "");
  }
  else if (*arg1 == '+')
  {
    arg1++;
    do_channel_join(player, arg1);
  }
  else if (*arg1 == '-')
  {
    arg1++;
    do_channel_leave(player, arg1);
  }
  else if (!*arg2)
  {
    do_channel_default(player, arg1);
  }
  else
  {
    notify(player, "+channel:  Invalid command.");
  }
}

void do_channel_default(player, arg1)
dbref player;
char *arg1;
{
  dbref i;
  int onoff;

  i = is_on_channel_only(player, arg1);

  if (i < 0) 
  {
    notify(player, "+channel default: Need to join the channel first.");
  }
  else
  {
    char *alias;

    alias = find_channel_alias(player, arg1);
    if (!(alias && *alias))
    {
      alias = tprintf("%s", arg1);
    }

    onoff = channel_onoff_chk(player, i);

    remove_from_channel(player, arg1);

    if (!*atr_get(player, A_CHANNEL))
      atr_add(player, A_CHANNEL, tprintf("%s:%s:%d", arg1, alias, onoff));
    else
      atr_add(player, A_CHANNEL, tprintf("%s:%s:%d %s", arg1, alias, onoff, atr_get(player, A_CHANNEL)));

    notify(player, tprintf("+channel default: %s is now your default channel.", arg1));
  }
}

void do_ban(dbref player, char *arg1, char *arg2)
{
  notify(player, "+channel: +ban depricated, use +channel ban");
  do_channel_ban(player, tprintf("%s:%s", arg1, arg2));
}

void do_unban(dbref player, char *arg1, char *arg2)
{
  notify(player, "+channel: +unban depricated, use +channel unban");
  do_channel_unban(player, tprintf("%s:%s", arg1, arg2));
}


/* arg2 = channel, arg1 = player */
void do_channel_ban(dbref player, char *arg2)
{
  dbref victim;
  char *arg1;

  if (!power(player, POW_BAN))
  {
    notify(player, perm_denied());
    return;
  }

  arg1 = strchr(arg2, ':');
  if (!(arg1 && *arg1))
  {
    notify(player, "+channel:  Bad ban syntax.");
    return;
  }
  *arg1++ = '\0';
  


  init_match(player, arg1, TYPE_PLAYER);
  match_neighbor();
  match_here();
  if (power(player, POW_REMOTE))
  {
    match_player();
    match_absolute();
  }

  if ((victim = noisy_match_result()) == NOTHING)
    return;

  if (!controls(player, victim, POW_BAN))
  {
    log_important(tprintf("%s failed to: +channel ban %s=%s", unparse_object_a(player, player),
			  unparse_object_a(victim, victim), arg2));
    notify(player, perm_denied());
    return;
  }

  if (strchr(arg2, ' ') || !*arg2)
  {
    notify(player, "Sorry, channel names can not have spaces in them.");
    return;
  }

  if (is_banned(player, arg2) >= 0)
  {
    notify(player, tprintf("%s has already been banned from %s.",
			   db[player].name, arg2));
    return;
  }

  if (!*atr_get(victim, A_BANNED))
    atr_add(victim, A_BANNED, arg2);
  else
    atr_add(victim, A_BANNED, tprintf("%s %s", arg2, atr_get(victim, A_BANNED)));

  remove_from_channel(victim, arg2);

  log_important(tprintf("%s executed: +channel ban %s=%s",
  unparse_object_a(player, player), unparse_object_a(victim, victim), arg2));
  notify(player, tprintf("%s banned from channel %s.",
			 unparse_object(player, victim), arg2));
  notify(victim, tprintf("You have been banned from channel %s by %s.", arg2,
			 unparse_object(victim, player)));
  com_send(arg2, tprintf("%s has been banned from this channel.", db[victim].cname));
}

void do_channel_boot(dbref player, char *channel)
{
  char *vic;

  vic = strchr(channel,':');
  if (vic && *vic)
  {
    *vic++ = '\0';
    if (vic && *vic)
    {
      dbref victim;

      victim = lookup_player(vic);
      if((victim != NOTHING) && (remove_from_channel(victim, channel) != NOTHING))
      {
        notify(player, tprintf("+channel: You have booted %s from %s.",  unparse_object(player, victim), channel));
        notify(victim, tprintf("+channel: You have been booted from %s by %s", channel, unparse_object(victim, player)));
        com_send(channel, tprintf("%s has been booted from this channel", db[victim].cname));
        return;
      }
    }
  }
  notify(player, "+channel: Bad boot syntax.");
}

int remove_from_channel(dbref victim, char *arg2)
{
  int i;
  int j;

  i = is_on_channel_only(victim, arg2);
  if (i != NOTHING)
  {
    dbref channum;

    channum = lookup_channel(arg2);

    atr_add(victim, A_CHANNEL, remove_from_ch_attr(victim, i));
    if (channum != NOTHING)
    {
      j = is_on_channel_only(channum, tprintf("%ld", victim));
      if (j >= 0)
      {
        atr_add(channum, A_CHANNEL, remove_from_ch_attr(channum, j));
      }
    }
  }
  return (i);
}


void do_channel_unban(dbref player, char *arg2)
{
  dbref victim;
  int i;
  char *end;
  char buf[4096];
  char buf2[4096];
  char *arg1;

  if (!power(player, POW_BAN))
  {
    notify(player, perm_denied());
    return;
  }

  arg1 = strchr(arg2, ':');
  if (!(arg1 && *arg1))
  {
    notify(player, "+channel:  Bad ban syntax.");
    return;
  }
  *arg1++ = '\0';

  init_match(player, arg1, TYPE_PLAYER);
  match_neighbor();
  match_here();
  if (power(player, POW_REMOTE))
  {
    match_player();
    match_absolute();
  }

  if ((victim = noisy_match_result()) == NOTHING)
    return;

  if (!controls(player, victim, POW_BAN))
  {
    notify(player, tprintf("You don't have the authority to unban %s.",
			   unparse_object(player, victim)));
    return;
  }

  if (strchr(arg2, ' ') || !*arg2)
  {
    notify(player, "Sorry, channel names can not have spaces in them.");
    return;
  }

  i = is_banned(victim, arg2);
  if (i < 0)
  {
    notify(player, tprintf("%s is not banned from channel %s.",
			   unparse_object(player, victim), arg2));
    return;
  }

  strcpy(buf, atr_get(player, A_BANNED));
  end = strchr(buf + i, ' ');
  if (!end)
    end = strchr(buf + i, '\0');
  else
    end++;
  strcpy(buf2, atr_get(victim, A_BANNED));
  strcpy(buf2 + i, end);
  if ((end = strchr(buf2, '\0')) && *--end == ' ')
    *end = '\0';
  atr_add(victim, A_BANNED, buf2);

  notify(player, tprintf("%s may now join channel %s again.",
			 unparse_object(player, victim), arg2));
  notify(victim, tprintf("%s has allowed you to join channel %s again.",
			 unparse_object(victim, player), arg2));
  com_send(arg2, tprintf("%s has been allowed back to this channel.", db[victim].cname));

}

static int is_banned(player, chan)
dbref player;
char *chan;
{
  char *blist;
  char *blbegin;
  char *buf;

  buf=tprintf("%s ", atr_get(player, A_BANNED));
  blbegin = blist = buf;
  while (*blist)
  {
    if (strchr(blist, ' '))
      *strchr(blist, ' ') = '\0';
    if (!strcmp(blist, chan))
      return (blist - blbegin);
    blist += strlen(blist) + 1;
  }
  return -1;
}


int ok_channel_name(char *name)
{
  char *scan;

  if (! (name && *name 
              && *name != NUMBER_TOKEN 
              && *name != NOT_TOKEN 
              && !strchr(name, ARG_DELIMITER)  
              && !strchr(name, AND_TOKEN) 
              && !strchr(name, OR_TOKEN) 
              && string_compare(name, "me") 
              && string_compare(name, "home") 
              && string_compare(name, "here")
              && (lookup_channel(name)))
        || strchr(name, ';') 
        || strchr(name, ' ') 
        || strlen(name) > channel_name_limit)
  {
    return 0;
  }
  if (!string_compare(name, "i") ||
      !string_compare(name, "me") ||
      !string_compare(name, "my") ||
      !string_compare(name, "you") ||
      !string_compare(name, "your") ||
      !string_compare(name, "he") ||
      !string_compare(name, "she") ||
      !string_compare(name, "it") ||
      !string_compare(name, "his") ||
      !string_compare(name, "her") ||
      !string_compare(name, "hers") ||
      !string_compare(name, "its") ||
      !string_compare(name, "we") ||
      !string_compare(name, "us") ||
      !string_compare(name, "our") ||
      !string_compare(name, "they") ||
      !string_compare(name, "them") ||
      !string_compare(name, "their") ||
      !string_compare(name, "a") ||
      !string_compare(name, "an") ||
      !string_compare(name, "the") ||
      !string_compare(name, "one") ||
      !string_compare(name, "to") ||
      !string_compare(name, "if") ||
      !string_compare(name, "and") ||
      !string_compare(name, "or") ||
      !string_compare(name, "but") ||
      !string_compare(name, "at") ||
      !string_compare(name, "of") ||
      !string_compare(name, "op") ||
      !string_compare(name, "own") ||
      !string_compare(name, "all") ||
      !string_compare(name, "for") ||
      !string_compare(name, "foo") ||
      !string_compare(name, "so") ||
      !string_compare(name, "this") ||
      !string_compare(name, "that") ||
      !string_compare(name, ">") ||
      !string_compare(name, ".") ||
      !string_compare(name, "-") ||
      !string_compare(name, ">>") ||
      !string_compare(name, "..") ||
      !string_compare(name, "--") ||
      !string_compare(name, "->") ||
      !string_compare(name, ":)") ||
      !string_compare(name, "delete") ||
      !string_compare(name, "purge") ||
      !string_compare(name, "check"))
    return 0;


  for (scan = name; *scan; scan++)
  {
    if (!isprint(*scan))
    {                           /* was isgraph(*scan) */
      return 0;
    }
    switch (*scan)
    {
    case '~':
    case ';':
    case ',':
    case '*':
    case '@':
    case '#':
      if (scan != name)
      {
        return 0;
        break;
      }
      break;
    default:
      break;
    }
  }
  return 1;
}


void do_channel_create(dbref player, char *arg2)
{
  dbref channel;
  ptype k;
  char *alias;
  long cost = channel_cost;
  
  char *nocolor;

  nocolor = strip_color_nobeep(arg2);

  if (*arg2 == '\0')
  {  
    notify(player, "+channel: Create what?");
    return;
  }   

  if (strchr(arg2, ' ') || !*arg2)
  { 
    notify(player, "+channel: Sorry, channel names can not have spaces in them.");
    return;
  }

  alias = strchr(nocolor, ':');
  if (alias && *alias)
    *alias++ = '\0';

  if (lookup_channel(nocolor) != NOTHING)
  {  
    notify(player, tprintf("+channel: There is already a %s +channel.", unparse_object(player, lookup_channel(arg2))));
    return;
  }  
  else if ((!ok_channel_name(nocolor)) || (alias && *alias && (!ok_channel_name(alias))))
  {  
    notify(player, "+channel: That's a silly name for a channel!");
    return;
  }  
  if (!db[player].pows)
    return;
  k = *db[player].pows;
  if (*nocolor == '*' &&
      !(k == CLASS_ADMIN ||
        k == CLASS_DIR))
  { 
    notify(player, tprintf("+channel: %s", perm_denied()));
    return;
  }
  if (*nocolor == '.' &&
      !(k == CLASS_DIR ||
        k == CLASS_ADMIN ||
        k == CLASS_BUILDER))
  { 
    notify(player, tprintf("+channel: %s", perm_denied()));
    return;
  }
  if (*nocolor == '_' &&
      !(k == CLASS_DIR ||
        k == CLASS_ADMIN ||
        k == CLASS_BUILDER ||
        k == CLASS_OFFICIAL ||
        k == CLASS_JUNOFF))
  { 
    notify(player, tprintf("+channel: %s", perm_denied()));
    return;
  }

  if (can_pay_fees(def_owner(player), cost, QUOTA_COST))
  {  

    /* create the object */
    channel = new_object();

    /* initialize everychannel */
    SET(db[channel].name, nocolor);
    SET(db[channel].cname, arg2);
    db[channel].zone = NOTHING;
    db[channel].location = channel;
    db[channel].link = channel;
    db[channel].owner = def_owner(player);
    s_Pennies(channel, (long)OBJECT_ENDOWMENT(cost));
    db[channel].flags = TYPE_CHANNEL;
    db[channel].flags |= SEE_OK; 

    /* endow the object */
    if (Pennies(channel) > MAX_OBJECT_ENDOWMENT)
    { 
      s_Pennies(channel, (long)MAX_OBJECT_ENDOWMENT);
    }
    atr_add(channel, A_LASTLOC, tprintf("%ld", channel));
    moveto(channel, channel);
    db[channel].i_flags &= I_MARKED;

    add_channel(channel);

    notify(player, tprintf("+channel: %s created.", unparse_object(player, channel)));
    if (alias && *alias)
      do_channel_join(player, tprintf("%s:%s", db[channel].name, alias));
    else
      do_channel_join(player, db[channel].name);
  }
}


void do_channel_destroy(dbref player, char *name)
{
  dbref victim;


  victim = lookup_channel(name);

  if (victim == NOTHING)
  {
    notify(player,"+channel: invalid channel name.");
  } 
  else if ( (Typeof(player) != TYPE_PLAYER) || ((db[victim].owner != player) && (!power(player, POW_CHANNEL)) && (!power(player, POW_NUKE))))
  {  
    notify(player, tprintf("+channel: %s", perm_denied()));
  } 
  else if (Typeof(victim) != TYPE_CHANNEL)
  {  
    notify(player, "+channel: This isn't a channel!");
  }  
  else if (!controls(player, victim, POW_NUKE))
  {  
    log_important(tprintf("%s failed to: +channel destroy=%s", unparse_object_a(player, player),
                          unparse_object_a(victim, victim)));
    notify(player, perm_denied());
  }  
  else if (owns_stuff(victim))
  {  
    notify(player, "+channel: Problem. Channel owns something. That's bad.");
  }  
  else
  {  
    char *plist;
    char *s;

    /* need to boot all players off of the channel here. */
    plist = tprintf("%s ",atr_get(victim, A_CHANNEL));

    while (plist && *plist)
    {
      s = strchr(plist,' ');
      if(s && *s)
        *s++ = '\0';
      notify(atol(plist), tprintf("+channel: %s is being destroyed. you must leave now.", db[victim].cname));
      remove_from_channel(atol(plist), db[victim].name);
      plist = s;
    }

    do_halt(victim, "", "");
    db[victim].flags = TYPE_THING;
    db[victim].owner = root;
    delete_channel(victim);
    destroy_obj(victim, 1); /* destroy in 1 second */

    notify(player, tprintf("+channel: %s destroyed.", db[victim].cname));
    log_important(tprintf("%s executed: +channel destroy=%s",
       unparse_object_a(player, player), unparse_object_a(victim, victim)));
  }  
} 



void do_channel_op(dbref player, char *arg2)
{
  char *user;
  dbref target;
  dbref channum;
  int yesno;
  int  place;

  yesno = 1;
  
  user = strchr(arg2,':');
  if (user && *user)
  {
    *user++ = '\0';
    if (user && *user)
    {
      if(*user == '!')
      {
        yesno = 0;
        *user++ = '\0';
      }
      if (user && *user)
      {
        channum = lookup_channel(arg2);
        if (channum == NOTHING)
        {
          notify(player, "+channel:  Invalid channel specified in op operation.");
          return;
        }
        target = lookup_player(user);
        if (target == NOTHING)
        {
          notify(player, "+channel:  Invalid player specified in op operation.");
          return;
        }
        place = is_in_attr(channum, tprintf("#%ld",target), A_USERS);

        if (yesno == 0)
        {
          if (place != NOTHING)
          {
            atr_add(channum, A_USERS, remove_from_attr(channum, place, A_USERS));
            notify(player, tprintf("+channel: %s is no longer an op on %s", unparse_object(player, target), unparse_object(player, channum)));
            return;
          }
          else
          {
            notify(player, tprintf("+channel: %s was not an op on %s anyway!", unparse_object(player, target), unparse_object(player, channum)));
            return;
          }
        }
        else
        {
          if (place != NOTHING)
          {
            notify(player, tprintf("+channel: %s is already an op on %s!", unparse_object(player, target), unparse_object(player, channum)));
            return;
          }
          else 
          {
            char *tmp;

            tmp = tprintf("%s",atr_get(channum, A_USERS));

            if (tmp && strlen(tmp))
            {
              atr_add(channum, A_USERS, tprintf("%s #%ld", tmp, target));
            }
            else
            {
              atr_add(channum, A_USERS, tprintf("#%ld", target));
            }

            notify(player, tprintf("+channel: %s is now an op on %s", unparse_object(player, target), unparse_object(player, channum)));
            return;
          }
        }
      }
    }
  }
  notify(player, "+channel: Invalid op format.");
}


void do_channel_lock(dbref player, char *arg2)
{

}
void do_channel_password(dbref player, char *arg2)
{
    char *chan;
    char *password;
    dbref channel;

    chan = arg2;
    password = strchr(arg2, ':');
    if (!password)
    {
      notify(player, "+channel:  Bad password.");
      return;
    }
    *password++ = '\0';

    channel = lookup_channel(chan);

    if (!channel || (channel == NOTHING))
    {
      notify(player, "+channel: Invalid channel");
      return;
    }

    if ( (!controls(player, db[channel].owner, POW_CHANNEL)) &&
         (!group_controls(player, channel))
       )
    {
      notify(player, "+channel:  You do not have permission to set the password on this channel.");
      return;
    }

    if (password && *password)
    {
      s_Pass(channel, crypt(password, "XX"));
      notify(player, tprintf("+channel: %s password changed.", unparse_object(player, channel)));
    }
    else
    {
      s_Pass(channel, "");
      notify(player, tprintf("+channel: %s password erased.", unparse_object(player, channel)));
    }
}

void do_channel_join(dbref player, char *arg2)
{
  ptype k;
  char buf[4096];
  char *alias;
  char *password;
  char *cryptpass;
  char *chanpass;
  int pmatch;
  dbref channum;
    
    pmatch = 0;

    alias = strchr(arg2, ':');
    if (alias && *alias)
      *alias++ = '\0';
    if (!(alias && *alias))
    {
      alias = tprintf("%s", arg2);
    }
    else
    {
      password = strchr(alias, ':');
      if (password && *password)
      {
        *password++ = '\0';
        if (password && *password)
        {
          cryptpass = tprintf("%s",crypt(password, "XX"));
          chanpass = tprintf("%s",Pass(player));
          if(!strcmp(cryptpass, chanpass))
          {
            pmatch = 1;
          }
        }
      }
    }
    channum = lookup_channel(arg2);

#ifdef ALLOW_COM_NP
    if (Typeof(player) == TYPE_CHANNEL)
    {
      notify(player, "+channel: Channels can't talk on channels. Imagine the Spam.");
      return;
    }
#else
    if (Typeof(player) != TYPE_PLAYER)
    {
      notify(player, "+channel: Non-players cannot be on channels. Sorry.");
      return;
    }
#endif
    if (!pmatch)
    {
      if (!db[player].pows)
        return;
      k = *db[player].pows;
      if (*arg2 == '*' &&
  	!(k == CLASS_ADMIN ||
  	  k == CLASS_DIR))
      {
        notify(player, perm_denied());
        return;
      }
      if (*arg2 == '.' &&
  	!(k == CLASS_DIR ||
  	  k == CLASS_ADMIN ||
  	  k == CLASS_BUILDER))
      {
        notify(player, perm_denied());
        return;
      }
      if (*arg2 == '_' &&
  	!(k == CLASS_DIR ||
  	  k == CLASS_ADMIN ||
  	  k == CLASS_BUILDER ||
  	  k == CLASS_OFFICIAL ||
  	  k == CLASS_JUNOFF))
      {
        notify(player, perm_denied());
        return;
      }
  
      if ((!could_doit(player, channum, A_LOCK)) && (!controls(player, db[channum].owner, POW_CHANNEL)))
      {
        notify(player, "+channel: Sorry, you are not permitted to join this channel.");
        return;
      }
    } 
  


    if (strchr(arg2, ' '))
    {
      notify(player, "Sorry, channel names can not have spaces in them.");
      return;
    }
    if (!*arg2)
    {
      notify(player, "What channel?");
      return;
    }
    if (is_banned(player, arg2) >= 0)
    {
      notify(player, "You have been banned from that channel.");
      return;
    }
    if (is_on_channel(player, arg2) != NOTHING)
    {

      if (is_on_channel_only(player, alias) != NOTHING)
      {

/* If you want them to get an error for trying to rejoin a channel, uncomment *
 * the error message, and comment out the do_channel_default function call  */
        notify(player,"You are already on that channel. Try +ch alias to change aliases."); 
/*        do_channel_default(player, arg2); */
        return;
      }
      else
      {
/*        notify(player, "You are already on that channel, using a different alias."); */
        do_channel_alias(player, tprintf("%s:%s",arg2,alias));
        return;
      }
    }
    if (is_channel_alias(player, alias))
    {
      notify(player, tprintf("+channel: You're already using that alias. (%s)", alias));
      return;
    }


    if(channum == NOTHING)
    {
      notify(player,tprintf("+channel: channel %s does not exist.",arg2));
      return;
    }

    if (!*atr_get(player, A_CHANNEL))
      atr_add(player, A_CHANNEL, tprintf("%s:%s:1",arg2,alias));
    else
      atr_add(player, A_CHANNEL, tprintf("%s %s:%s:1",  atr_get(player, A_CHANNEL), arg2, alias));

    if (!*atr_get(channum, A_CHANNEL))
      atr_add(channum, A_CHANNEL, tprintf("%ld",player));
    else
      atr_add(channum, A_CHANNEL, tprintf("%s %ld",  atr_get(channum, A_CHANNEL), player));

    if (!(db[channum].flags & QUIET))
    {
      char sayit[1024];

      strncpy(sayit, atr_get(channum, A_OENTER), 1023);
      sayit[1023] = '\0';
      if (sayit && *sayit)
      {
        char buf2[4096];
        char *p;

        pronoun_substitute(buf2, player, sayit, channum);
        p = buf2 + strlen(db[player].name) + 1;
        strncpy(buf, p, 1024);
        buf[1023] = '\0';
      }
      else
      {
        sprintf(buf, "|G!+*| %s has joined this channel.", db[player].cname);
      }
      com_send(arg2, buf);
    }
    notify(player, tprintf("+channel: %s added to your channel list with alias %s.", arg2, alias));

    if (strlen(atr_get(channum, A_DESC)))
    {
      notify(player, tprintf("+channel topic: %s", atr_get(channum, A_DESC)));
    }

}
void do_channel_leave(dbref player, char *arg2)
{
    int i;
    int j;

    char buf[4096];
    char *pattr;
    char *cattr;

    dbref channum;


    if (strchr(arg2, ' ') || !*arg2)
    {
      notify(player, "Sorry, channel names can not have spaces in them.");
      return;
    }
    i = is_on_channel(player, arg2);
    if ((i < 0) || (!(find_channel_only(player, arg2))))
    {
      notify(player, "You aren't on that channel.");
      return;
    }

    notify(player, tprintf("%s has been deleted from your channel list.",
			   arg2));
    if (lookup_channel(arg2) && (!(db[channum].flags & QUIET)))
    {
      char sayit[1024];

      strncpy(sayit, atr_get(channum, A_OLEAVE), 1023);
      sayit[1023] = '\0';
      if (sayit && *sayit)
      {
        char buf2[4096];
        char *p;

/* this could be nasty, but should be okay.. but it does have root privs.. */
        pronoun_substitute(buf2, player, sayit, player);
        p = buf2 + strlen(db[player].name) + 1;
        strncpy(buf, p, 1024);
        buf[1023] = '\0';
      }
      else
      {
        sprintf(buf, "|G!+*| %s has left this channel.", db[player].cname);
      }
      com_send(arg2, buf);
    }

    pattr = remove_from_ch_attr(player, i);

    channum = lookup_channel(arg2);
    if(channum == NOTHING)
    {
      notify(player, "+channel: Removing old channel");
    }
    else
    {
      j = is_on_channel(channum, tprintf("%ld", player));
      if (j != NOTHING)
        cattr = remove_from_ch_attr(channum, j);
      else
        channum = NOTHING;
    }

    atr_add(player, A_CHANNEL, pattr);
    if(channum != NOTHING)
      atr_add(channum, A_CHANNEL, cattr);

}


char *remove_from_ch_attr(dbref player, int i)
{
  return(remove_from_attr(player, i, A_CHANNEL));
}

int is_in_attr(dbref player, char *str, ATTR *attr)
{
  char *cur;
  char *begin;
  char *next;
  
  begin = cur = tprintf("%s ", atr_get(player, attr));
  while (cur && *cur)
  {
    next = strchr(cur, ' ');
    if (next && *next)
    {   
      *next++ = '\0';
      if (!strcmp(cur, str))
      {
        return (cur - begin);
      }
    }
    cur = next;
  }
  return NOTHING;
}


char *remove_from_attr(dbref player, int i, ATTR *attr)
{
  char *end;
  char buf[4096];
  char buf2[4096];

  strcpy(buf, atr_get(player, attr));
  end = strchr(buf + i, ' ');
  if (!end)
    end = strchr(buf + i, '\0');
  else
    end++;
  strcpy(buf2, atr_get(player, attr));
  strcpy(buf2 + i, end);
  if ((end = strchr(buf2, '\0')) && *--end == ' ')
    *end = '\0';
  return tprintf("%s",buf2);
}

void do_channel_alias(dbref player, char *arg2)
{
  char *channel;
  char *alias;
  int pos;

  char *old1; 
  char *old2;
  char *new;
  char *s;

  channel = arg2;

  alias = strchr(channel, ':');
  if (alias)
  {
    *alias++ = '\0';
    if (alias && *alias)
    {
      s = strchr(alias, ':');
      if(s && *s)
      { 
        *s++ = '\0';
        if(!(s && *s))
        {
          s = tprintf("1");
        }
      }
      else
      {
        s = tprintf("1");
      }
      pos = is_on_channel_only(player, channel);
      if (pos == NOTHING)
      {
        notify(player, "+channel:  You must first join the channel before setting it's alias.");
      }
      else
      {
        old1 = tprintf("%s", atr_get(player, A_CHANNEL));
        old2 = old1 + pos;

        if (old2 && *old2)
        {
          *old2++ = '\0';
          old2 = strchr(old2, ' ');
          if (old2 && *old2)
            old2++;
          
          if (old2 && *old2)
          {
            new = tprintf("%s%s:%s:%s %s", old1, channel, alias, s, old2);
          }
          else
          {
            new = tprintf("%s%s:%s:%s", old1, channel, alias, s);
          }
        }
        else
        {
          new = tprintf("%s%s:%s:%s", old1, channel, alias, s);
        }
        atr_add(player, A_CHANNEL, new);
        notify(player, tprintf("Alias for channel %s is now %s",channel,alias));
      }
    }
    else
    {
      notify(player, "+channel: Bad +channel alias syntax.");
    }
  }
  else
  {
    notify(player, "+channel: Bad +channel alias syntax.");
  }
}

void do_channel_log(dbref player, char *arg2)
{

}

void do_channel_search(dbref player, char *arg2)
{
  
  struct pl_elt *e;
  int level;
  char onoff[3];
  int i;

  if (!(arg2 && *arg2))
  {
    notify(player, "+channel:  Bad search syntax.");
    return;
  }

  level = 0;
 
  if(!strncmp(arg2, "own", 4))
  {
    level = 1;
  }
  else if(!strncmp(arg2, "op", 3))
  {
    level = 2;
  }
  else if(!strncmp(arg2, "all", 4))
  {
    level = 3;
  }
  
  notify(player, "+channel search results:");

  for (i = 0; i < CHANNEL_LIST_SIZE; i++)
  {
    if (pl_used)
    {
      for (e = channel_list[i]; e; e = e->next)
      {
        char owner[4095];
        char chan[4095];
        char filler[25] = "                         ";


        strncpy(owner, truncate_color(unparse_object(player, db[e->channel].owner), 20), 4090);
        filler[(20 - strlen(strip_color(owner)))] = '\0';
        strcat(owner, filler);

        strcpy(filler, "                        ");
        strncpy(chan, truncate_color(unparse_object(player, e->channel), 20), 4090);
        filler[(20 - strlen(strip_color(chan)))] = '\0';
        strcat(chan, filler);


        if (is_on_channel_only(player, db[e->channel].name) != NOTHING) 
        {
          if (channel_onoff_chk(player, e->channel)) 
          {
            strcpy(onoff, "ON ");
          }
          else
          {
            strcpy(onoff, "OFF");
          }
        }
        else
        {
          strcpy(onoff, "   ");
        }
    
        if (level == 0) 
        {
          if (!string_compare(db[e->channel].name, arg2)) 
          {
            notify(player, tprintf("  %s %s %s",onoff, chan, owner));
            break;
          }
        }
        else if (db[e->channel].owner == player)
        {
          notify(player, tprintf("* %s %s %s",onoff, chan, owner));
        }
        else if ((level > 1) && group_controls(player, e->channel) )
        {
          notify(player, tprintf("# %s %s %s",onoff, chan, owner));
        }
        else if (level == 3) 
        {
          if (!( (db[e->channel].flags & SEE_OK) &&  (could_doit(player, e->channel, A_LHIDE))) )
          {
            if(controls(player, e->channel, POW_CHANNEL))
            {
              strcpy(onoff, "HID");
              notify(player, tprintf("  %s %s %s",onoff, chan, owner));
            }
          }
          else
          {
            notify(player, tprintf("  %s %s %s",onoff, chan, owner));
          }
        }
      }
    }
  }
}

void do_channel_list(dbref player, char *arg2)
{

  dbref target;

  if (arg2 && *arg2) 
  {
    target = lookup_player(arg2);
    if ((!target) || (target == NOTHING) || (!valid_player(target)))
    {
      notify(player, "+channel: Invalid player specified.");
      return;
    }
  }
  else
  {
    target = player;
  }


  if ( (!(controls(player, target, POW_CHANNEL))) && (db[player].pows[0] != CLASS_DIR) && (target != player))
  {
    notify(player, tprintf("+channel: %s", perm_denied()));
  }
  else
  {
    if(*atr_get(target, A_CHANNEL))
    {
      notify(player, tprintf("+channel: %s is currently on the following channels: ", unparse_object(player, target)));

      list_channels(player, target);
    }
    else
    {
      notify(player, tprintf("+channel: %s isn't currently on any channels.", unparse_object(player, target))); 
      notify(player, "+channel: For a general chatting channel, turn to channel 'public'");
    }
  }
}


void list_channels(dbref player, dbref target)
{

  char *clist;
  char *clbegin;
  char *alias;
  char *al;
  char *status;
  char *s;
  dbref channum;

  clbegin = clist = tprintf("%s ", atr_get(target, A_CHANNEL));
  notify(player, "Channel:             Alias:     Status: Owner:");
  while (*clist)
  {
    if (strchr(clist, ' '))
      *strchr(clist, ' ') = '\0';       /* insert null after first channel
                                           name */
    alias = strchr(clist, ':');
    if (alias && *alias)
    {
      *alias++ = '\0';
      s = strchr(alias, ':');
      if(s && *s)
      {
        *s++ = '\0';
        if (!strncmp(s, "0", 1))
        { 
          status = tprintf("OFF");
        }
        else
        {
          status = tprintf("ON ");
        }
      }
      al = alias;
    }
    else
    {
      al = tprintf("UNDEFINED");
    }

    channum = lookup_channel(clist);
    if(channum == NOTHING)
    {
      notify(player, tprintf("%-30.30s  Invalid Channel.", clist));
    }
    else
    {
      char channame[4096];
      char filler[25] = "                         ";
      strncpy(channame, truncate_color(unparse_object(player, channum), 20), 4050);
      filler[(20 - strlen(strip_color(channame)))] = '\0';
      strcat(channame, filler);

      notify(player, tprintf("%s %-10.10s %s     %s", channame, al, status, unparse_object(player, db[channum].owner)));
    }

    if (alias && *alias)
    {
      clist += strlen(clist) + strlen(alias) + 2;
      if (s && *s)
        clist += strlen(s) + 1;
    }
    else
      clist += strlen(clist) + 1;
  }
  return;
}



/* taken from players.c */

static void init_chan_hft()
{
  int i;

  for (i = 0; i < 256; i++)
  {
    hash_chan_fun_table[i] = random() & (CHANNEL_LIST_SIZE - 1);
  }
  hft_chan_initialized = 1;
}

static dbref hash_chan_function(string)
char *string;
{
  dbref hash;

  if (!hft_chan_initialized)
    init_chan_hft();
  hash = 0;
  for (; *string; string++)
  {
    hash ^= ((hash >> 1) ^ hash_chan_fun_table[(int)DOWNCASE(*string)]);
  }
  return (hash);
}


void clear_channels()
{
  int i;
  struct pl_elt *e;
  struct pl_elt *next;

  for (i = 0; i < CHANNEL_LIST_SIZE; i++)
  {
    if (pl_used)
    {
      for (e = channel_list[i]; e; e = next)
      {
	next = e->next;
	free(e);
      }
    }
    channel_list[i] = 0;
  }
  pl_used = 1;
}

void add_channel(channel)
dbref channel;
{
  dbref hash;
  struct pl_elt *e;

  if (!strchr(db[channel].name, ' '))
  {
    hash = hash_chan_function(db[channel].name);
    e = malloc(sizeof(struct pl_elt));

    e->channel = channel;
    e->prev = NULL;
    e->next = channel_list[hash];
    if(channel_list[hash])
      channel_list[hash]->prev = e;
    channel_list[hash] = e;
  }
  else
  {
    log_error(tprintf("Channel (%s) with a space in it's name?  Inconceivable!", db[channel].name));
  }
}

dbref lookup_channel(name)
char *name;
{
  struct pl_elt *e;
  dbref a;

  if (!name)
    return NOTHING;
  for (e = channel_list[hash_chan_function(name)]; e; e = e->next)
  {
    if (!string_compare(db[e->channel].name, name))
      return e->channel;
  }
  if (name[0] == '#' && name[1])
  {
    a = atol(name + 1);
    if (a >= 0 && a < db_top)
      return a;
  }
  return NOTHING;
}

void delete_channel(channel)
dbref channel;
{
  dbref hash;
  struct pl_elt *prev;
  struct pl_elt *e;

  if (!strchr(db[channel].name, ' '))
  {
    hash = hash_chan_function(db[channel].name);
    if ((e = channel_list[hash]) == 0)
    {
      return;
    }
    else if (e->channel == channel)
    {
      /* it's the first one */
      channel_list[hash] = e->next;
      free(e);
    }
    else
    {
      for (prev = e, e = e->next; e; prev = e, e = e->next)
      {
	if (e->channel == channel)
	{
	  /* got it */
	  prev->next = e->next;
	  free(e);
	  break;
	}
      }
    }
  }
  else
  {
    log_error(tprintf("Channel (%s) with a space in it's name?  Inconceivable!", db[channel].name));
  }
}

/* end reused/modified code */

void channel_talk (dbref player, char *chan, char *arg1, char *arg2)
{
  char *msg;
  
  char *channel;


  channel = find_channel(player, chan);

  if (!strlen(channel))
  {
    notify(player, "+channel: Invalid channel. Please leave and rejoin it.");
    return;
  }

/* verify allowed to talk on that channel */


/* reasseble message. joy */
  if(arg2 && *arg2)
  {
    msg = tprintf("%s = %s",arg1,arg2);
  }
  else
    msg = tprintf("%s",arg1);

  do_com(player, channel, msg);
}

void do_channel_color(dbref player, char *arg2)
{
  char *channel;
  char *alias;
  char *color;
  int pos;
  int onoff;

  char *old1; 
  char *old2;
  char *new;

  channel = arg2;

  color = strchr(channel, ':');
  if(!color)
  {
    color = arg2;
    channel = strip_color_nobeep(color);
  }
  else
  {
    *color++ = '\0';
  }
  alias = tprintf("%s", find_channel_alias(player, channel));
  if (!(color && *color))
  {
    color=tprintf("%s", channel);
  }

  onoff = channel_onoff_chk(player, lookup_channel(channel));

  pos = is_on_channel_only(player, channel);
  if (pos == -1)
  {
    notify(player, "+channel:  You must first join the channel before setting it's color.");
  }
  else if(strcmp(channel, strip_color_nobeep(color)) != 0)
  {
    notify(player, "+channel: Colored name does not match channel name.");
  }
  else
  {
    old1 = tprintf("%s", atr_get(player, A_CHANNEL));
    old2 = old1 + pos;

    if (old2 && *old2)
    {
      *old2++ = '\0';
      old2 = strchr(old2, ' ');
      if (old2 && *old2)
        old2++;
      
      if (old2 && *old2)
      {
        new = tprintf("%s%s:%s:%d %s", old1, color, alias, onoff, old2);
      }
      else
      {
        new = tprintf("%s%s:%s:%d", old1, color, alias, onoff);
      }
    }
    else
    {
      new = tprintf("%s%s:%s", old1, color, alias);
    }
    atr_add(player, A_CHANNEL, new);
    notify(player, tprintf("+channel: %s is now colored as %s", channel, color));
  }
}

char *find_channel_color(dbref player, char *channel)
{
  char *color;

  color=tprintf("%s",find_channel_only(player, channel));

  if(!strcmp(channel, color))
  {
    dbref channum;

    channum = lookup_channel(channel);
    if(channum != NOTHING);
    color = tprintf("%s", db[channum].cname);
  }

  return color;
}

int channel_onoff_chk(dbref player, dbref channum)
{
  char *next, *curr, *buf, *q, *alias;
  int onoff;

  next = curr = buf = tprintf("%s ", atr_get(player, A_CHANNEL));

  next = strchr(curr, ' ');
  *next++ = '\0';

  if ((alias = strchr(curr, ':')))
  { 
    *alias++ = '\0';
    q = strchr(alias, ':');
    if (q && *q)
    { 
      *q++ = '\0';
      onoff = atoi(q);
    }
    else
    { 
      do_channel_alias(player, tprintf("%s:%s",curr,alias));
      onoff = 1;
    }
  }
  else
  { 
    do_channel_alias(player, tprintf("%s:%s",curr,curr));
    onoff = 1;
  }

  return(onoff);
}

/* old way - very broken.
  char *clist;
  char *clbegin;
  char *alias;
  char *s;

  clbegin = clist = tprintf("%s ", atr_get(player, A_CHANNEL));
  while (*clist)
  {
    if (strchr(clist, ' '))
      *strchr(clist, ' ') = '\0';       * insert null after first channel
                                           name *
    alias = strchr(clist, ':');
    if (alias && *alias)
    { 
      *alias++ = '\0';
      if (!strcmp(strip_color_nobeep(clist), db[channum].name))
      { 
        s = strchr(alias, ':');
        if(s && *s)
        { 
          *s++ = '\0';
          if (s && *s)
          { 
            return atoi(s);
          } 
          else
          { 
            return 1;
          } 
          clist += strlen(s) + 1;
        }
      }
      clist += strlen(clist) + strlen(alias) + 2;
    }
    else
      clist += strlen(clist) + 1;
  }
  return 0;
}
*/
  

void channel_onoff_set(dbref player, char *arg1, char *arg2)
{
  char *alias;
  dbref channel;
  int pos;
  int onoff;
  int chang;

  char *old1; 
  char *old2;
  char *new;

  channel = lookup_channel(arg1);
  alias = find_channel_alias(player, db[channel].name);
  if (!(alias && *alias))
  {
    notify(player, "+channel: Sorry, you must first lasve and rejoin the channel.");
    return;
  }
  onoff = channel_onoff_chk(player, channel);

  pos = is_on_channel_only(player, db[channel].name);
  if (pos == NOTHING)
  {
    notify(player, "+channel:  You must first join the channel before changing it's status.");
    return;
  }

  if (!strcmp("on", arg2))
  {
    chang = 1;
  }
  else
  {
    chang = 0;
  }
  if (chang == onoff)
  {
    notify(player, tprintf("+channel: Channel %s is already %s!", db[channel].name, arg2));
    return;
  }
  if (chang == 0)
    com_send_as_hidden(db[channel].name, tprintf("|Y!+*| %s |G!+has turned this channel OFF.|", db[player].cname, arg2), player);

  old1 = tprintf("%s", atr_get(player, A_CHANNEL));
  old2 = old1 + pos;

  if (old2 && *old2)
  {
    *old2++ = '\0';
    old2 = strchr(old2, ' ');
    if (old2 && *old2)
    {
      old2++;
      new = tprintf("%s%s:%s:%d %s", old1, db[channel].name, alias, chang, old2);
    }
    else
    {
      new = tprintf("%s%s:%s:%d", old1, db[channel].name, alias, chang);
    }
  }
  else
  {
    new = tprintf("%s%s:%s:%d", old1, db[channel].name, alias, chang);
  }
  atr_add(player, A_CHANNEL, new);
  if (chang == 1)
    com_send_as_hidden(db[channel].name, tprintf("|Y!+*| %s |G!+has turned this channel ON.|", db[player].cname, arg2), player);
}
  
