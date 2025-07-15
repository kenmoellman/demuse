
/* speech.c */
/* $Id: speech.c,v 1.21 1993/09/18 19:03:46 nils Exp $ */

/* Commands which involve speaking */
#include <ctype.h>
#include <sys/types.h>

#include "db.h"
#include "interface.h"
#include "net.h"
#include "match.h"
#include "config.h"
#include "externs.h"

char *perm_denied2(char *, unsigned int );
char *perm_denied2(char *filename, unsigned int lineno)
{
  return(tprintf("%s  %s:%u", perm_denied(), filename, lineno));
}

#define perm_denied()  perm_denied2(__FILE__, __LINE__)

char *announce_name P((dbref));
static int can_emit_msg P((dbref, dbref, char *));

char *announce_name(player)
dbref player;
{
  if (power(player, POW_ANNOUNCE))
    return stralloc(db[player].cname);
  else
    return stralloc(unparse_object(player, player));
}

/* generate name for object to be used when spoken */
/* for spoof protection player names always capitalized, object names */
/* always lower case */
char *spname(thing)
dbref thing;
{

  /* ack! this is evil to do. -shkoo  static char buff[1024];
     strcpy(buff,db[thing].name); if (Typeof(thing)==TYPE_PLAYER)
     buff[0]=to_upper(buff[0]); else buff[0]=to_lower(buff[0]); return(buff); 

   */
  return db[thing].cname;
}

/* this function is a kludge for regenerating messages split by '=' */
char *reconstruct_message(arg1, arg2)
char *arg1;
char *arg2;
{
  char buf[BUFFER_LEN];

  if (arg2)
    if (*arg2)
    {
      strcpy(buf, arg1 ? arg1 : "");
      strcat(buf, " = ");
      strcat(buf, arg2);
      return stralloc(buf);
    }

  return arg1 ? stralloc(arg1) : stralloc("");
}

void do_say(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  dbref loc;
  char *message;
  char buf[BUFFER_LEN], *bf;

  if ((loc = getloc(player)) == NOTHING)
    return;
  if (IS(loc, TYPE_ROOM, ROOM_AUDITORIUM) &&
      (!could_doit(player, loc, A_SLOCK) ||
       !could_doit(player, db[loc].zone, A_SLOCK)))
  {
    did_it(player, loc, A_SFAIL, "Shh.", A_OSFAIL, NULL, A_ASFAIL);
    return;
  }
  message = reconstruct_message(arg1, arg2);
  pronoun_substitute(buf, player, message, player);
  bf = buf + strlen(db[player].name) + 1;
  /* notify everybody */
  notify(player, tprintf("You say \"%s\"", bf));
  notify_in(loc, player,
	    tprintf("%s says \"%s\"", spname(player), bf));
}

void do_to(player,arg1,arg2)
dbref player;
char *arg1; 
char *arg2;
{
  dbref loc, thing;
  char *s;
  char *message;
  char buf2[BUFFER_LEN];
  char buf3[BUFFER_LEN];

  if ((loc = getloc(player)) == NOTHING)
    return;


  if (IS(loc, TYPE_ROOM, ROOM_AUDITORIUM) &&
      (!could_doit(player, loc, A_SLOCK) ||
       !could_doit(player, db[loc].zone, A_SLOCK)))
  {
    did_it(player, loc, A_SFAIL, "Shh.", A_OSFAIL, NULL, A_ASFAIL);
    return;
  }

  message = reconstruct_message(arg1, arg2);

  if(!(s=strchr(message, ' '))) {
    notify(player, "No message.");
    return;
  }

  *s++='\0';
  if((!*message) || (strlen(s) < 1)) {
    notify(player, "No player mentioned.");
    return;
  }
  if((thing=lookup_player(message)) == AMBIGUOUS)
    thing=NOTHING;

  if (thing != NOTHING)
    strcpy(buf2, db[thing].cname);
  else
    strcpy(buf2, message);

  if (*s == POSE_TOKEN)
    sprintf(buf3, "[to %s] %s %s", buf2, db[player].cname, s + 1);
  else if (*s == NOSP_POSE)
    sprintf(buf3, "[to %s] %s's %s", buf2, db[player].cname, s + 1);
  else if (*s == THINK_TOKEN)
    sprintf(buf3, "[to %s] %s . o O ( %s )", buf2, db[player].cname, s + 1);
  else
    sprintf(buf3, "%s [to %s]: %s", db[player].cname, buf2, s);

  /* notify everybody */
  notify_in(loc, NOTHING, buf3);
}


void do_whisper(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  dbref who;
  char buf[BUFFER_LEN], *bf;

  pronoun_substitute(buf, player, arg2, player);
  bf = buf + strlen(db[player].name) + 1;
  init_match(player, arg1, TYPE_PLAYER);
  match_neighbor();
  match_me();
  if (power(player, POW_REMOTE))
  {
    match_absolute();
    match_player();
  }
  switch (who = match_result())
  {
  case NOTHING:
    notify(player, "Whisper to whom?");
    break;
  case AMBIGUOUS:
    notify(player, "I don't know who you mean!");
    break;
  default:
    if (*bf == POSE_TOKEN || *bf == NOSP_POSE)
    {
      notify(player,
	     tprintf("You whisper-posed %s with \"%s%s %s\".",
		     db[who].cname, spname(player),
		     (*bf == NOSP_POSE) ? "'s" : "", bf + 1));
      notify(who,
	     tprintf("%s whisper-poses: %s%s %s", spname(player),
		   spname(player), (*bf == NOSP_POSE) ? "'s" : "", bf + 1));
      did_it(player, who, NULL, 0, NULL, 0, A_AWHISPER);
      break;
    }
    else if (*bf == THINK_TOKEN)
    {
      notify(player, tprintf("You whisper-thought %s with \"%s . o O ( %s )\".",
			     db[who].cname, spname(player), bf + 1));
      notify(who, tprintf("%s whipser-thinks: %s . o O ( %s )", spname(player),
			  spname(player), bf + 1));
      did_it(player, who, NULL, 0, NULL, 0, A_AWHISPER);
      break;
    }
    notify(player,
	   tprintf("You whisper \"%s\" to %s.", bf, db[who].name));
    notify(who,
	   tprintf("%s whispers \"%s\"", spname(player), bf));
    /* wptr[0]=bf; *//* Don't pass %0 */
    did_it(player, who, NULL, 0, NULL, 0, A_AWHISPER);
    break;
  }
}

void do_pose(player, arg1, arg2, possessive)
dbref player;
char *arg1;
char *arg2;
int possessive;
{
  dbref loc;
  char lastchar, *format;
  char *message;
  char buf[BUFFER_LEN], *bf;

  if ((loc = getloc(player)) == NOTHING)
    return;

  if (IS(loc, TYPE_ROOM, ROOM_AUDITORIUM) &&
      (!could_doit(player, loc, A_SLOCK) ||
       !could_doit(player, db[loc].zone, A_SLOCK)))
  {
    did_it(player, loc, A_SFAIL, "Shhh.", A_OSFAIL, NULL, A_ASFAIL);
    return;
  }

  if (possessive)
  {
    /* get last character of player's name */
    lastchar = to_lower(db[player].name[strlen(db[player].name) - 1]);

    format = (lastchar == 's') ? "%s' %s" : "%s's %s";
  }
  else
    format = "%s %s";

  message = reconstruct_message(arg1, arg2);
  pronoun_substitute(buf, player, message, player);
  bf = strlen(db[player].name) + buf + 1;

  /* notify everybody */
  notify_in(loc, NOTHING,
	    tprintf(format, spname(player), bf));
}

void do_think(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  dbref loc;
  char *message;
  char buf[BUFFER_LEN], *bf;

  if ((loc = getloc(player)) == NOTHING)
    return;

  if (IS(loc, TYPE_ROOM, ROOM_AUDITORIUM) &&
      (!could_doit(player, loc, A_SLOCK) ||
       !could_doit(player, db[loc].zone, A_SLOCK)))
  {
    did_it(player, loc, A_SFAIL, "Shhh.", A_OSFAIL, NULL, A_ASFAIL);
    return;
  }

  message = reconstruct_message(arg1, arg2);
  pronoun_substitute(buf, player, message, player);
  bf = strlen(db[player].name) + buf + 1;

  /* notify everybody */
  notify_in(loc, NOTHING, tprintf("%s . o O ( %s )", spname(player), bf));

}

void do_echo(player, arg1, arg2, type)
dbref player;
char *arg1, *arg2;
int type;
{
  char *message = reconstruct_message(arg1, arg2);
  char buf[1024];

  if (type == 0)
  {
    pronoun_substitute(buf, player, message, player);
    message = buf + strlen(db[player].name) + 1;
  }
  notify(player, message);
}

void do_emit(player, arg1, arg2, type)
dbref player;
char *arg1;
char *arg2;
int type;
{

  dbref loc;
  char *message;
  char buf[BUFFER_LEN], *bf = buf;

  if ((loc = getloc(player)) == NOTHING)
    return;

/*      !controls(player, loc, POW_SPOOF) &&  */
  if (IS(loc, TYPE_ROOM, ROOM_AUDITORIUM) &&
      !controls(player, loc, POW_REMOTE) && 
      (!could_doit(player, loc, A_SLOCK) ||
       !could_doit(player, db[loc].zone, A_SLOCK)))
  {
    did_it(player, loc, A_SFAIL, "Shh.", A_OSFAIL, NULL,
	   A_ASFAIL);
    return;
  }
  message = reconstruct_message(arg1, arg2);
  if (type == 0)
  {
    pronoun_substitute(buf, player, message, player);
    bf = buf + strlen(db[player].name) + 1;
  }
  if (type == 1)
  {
    strcpy(buf, message);
    bf = buf;
  }

  if (power(player, POW_REMOTE))
  {
    notify_in(loc, NOTHING, tprintf("%s", bf));
    return;
  }

  if (can_emit_msg(player, db[player].location, bf))
    notify_in(loc, NOTHING, tprintf("%s", bf));
  else
    notify(player, perm_denied());
}

static void notify_in_zone P((dbref, char *));

static void notify_in_zone(zone, msg)
dbref zone;
char *msg;
{
  dbref thing;
  static int depth = 0;

  if (depth > 10)
    return;

  depth++;
  for (thing = 0; thing < db_top; thing++)
  {
    if (db[thing].zone == zone)
    {
      notify_in_zone(thing, msg);
      notify_in(thing, NOTHING, msg);
    }
  }
  depth--;
}

void do_general_emit(player, arg1, arg2, emittype)
dbref player;
char *arg1;
char *arg2;
int emittype;
{
  dbref who;
  char buf[BUFFER_LEN], *bf = buf;

  if (emittype != 4)
  {
    pronoun_substitute(buf, player, arg2, player);
    bf = buf + strlen(db[player].name) + 1;
  }
  if (emittype == 4)
  {
    bf = arg2;
    while (*bf && !(bf[0] == '='))
      bf++;
    if (*bf)
      bf++;
    emittype = 0;
  }

  init_match(player, arg1, TYPE_PLAYER);
  match_absolute();
  match_player();

  match_neighbor();
  match_possession();
  match_me();
  match_here();
  who = noisy_match_result();

  if (get_room(who) != get_room(player) && !controls(player, get_room(who), POW_REMOTE) && !controls_a_zone(player, who, POW_REMOTE))
  {
    notify(player, perm_denied());
    return;
  }

/*      !controls(player, db[who].location, POW_SPOOF) && */
  if (IS(db[who].location, TYPE_ROOM, ROOM_AUDITORIUM) &&
      !controls(player, db[who].location, POW_REMOTE) && 
      (!could_doit(player, db[who].location, A_SLOCK) ||
       !could_doit(player, db[who].zone, A_SLOCK)))
  {
    did_it(player, db[who].location, A_SFAIL, "Shhh.", A_OSFAIL, NULL,
	   A_ASFAIL);
    return;
  }

  switch (who)
  {
  case NOTHING:
    break;
  default:
    if (emittype == 0)		/* pemit */

        /* || controls(player, who, POW_SPOOF) */
      if (can_emit_msg(player, db[who].location, bf)
         || controls(player, who, POW_REMOTE) 
         )
      {
	notify(who, bf);
	/* wptr[0]=bf; *//* Do not pass %0 */
	did_it(player, who, NULL, 0, NULL, 0, A_APEMIT);
	if (!(db[player].flags & QUIET))
	  notify(player, tprintf("%s just saw \"%s\".",
				 unparse_object(player, who), bf));
      }
      else
	notify(player, perm_denied());
    else if (emittype == 1)
    {				/* room. */

/*          && controls(player, who, POW_SPOOF)  */
      if ( (controls(player, who, POW_REMOTE) ) || 
           ( (db[player].location == who) && can_emit_msg(player, who, bf) ) 
         )
      {
	notify_in(who, NOTHING, bf);
	if (!(db[player].flags & QUIET))
	  notify(player, tprintf("Everything in %s saw \"%s\".",
				 unparse_object(player, who), bf));
      }
      else
      {
	notify(player, perm_denied());
	return;
      }
    }
    else if (emittype == 2)
    {				/* oemit */
      if (can_emit_msg(player, db[who].location, bf))
      {
	notify_in(db[who].location, who, bf);
      }
      else
      {
	notify(player, perm_denied());
	return;
      }
    }
    else if (emittype == 3)	/* zone */

/*	  controls(player, who, POW_SPOOF) && */
      if (controls(player, who, POW_REMOTE) &&
	  controls(player, who, POW_MODIFY) &&
	  can_emit_msg(player, (dbref) - 1, bf))
      {
	if (db[who].zone == NOTHING && !(db[player].flags & QUIET))
	  notify(player, tprintf("%s might not be a zone... but i'll do it anyways",
				 unparse_object(player, who)));
	notify_in_zone(who, bf);
	if (!(db[player].flags & QUIET))
	  notify(player, tprintf("Everything in zone %s saw \"%s\".",
				 unparse_object(player, who), bf));
      }
      else
	notify(player, perm_denied());
  }
}

static int can_emit_msg(player, loc, msg)
dbref player;
dbref loc;
char *msg;
{
  char mybuf[1000];
  char *s;
  dbref thing, save_loc;

  for (; *msg == ' '; msg++) ;

  strcpy(mybuf, msg);
  for (s = mybuf; *s && (*s != ' '); s++) ;
  if (*s)
    *s = '\0';

  if ((thing = lookup_player(mybuf)) != NOTHING && !string_compare(db[thing].name, mybuf))
    if (!controls(player, thing, POW_REMOTE))
      return 0;

  if ((s - mybuf) > 2 && !strcmp(s - 2, "'s"))
  {
    *(s - 2) = '\0';

    if ((thing = lookup_player(mybuf)) != NOTHING && !string_compare(db[thing].name, mybuf))
      if (!controls(player, thing, POW_REMOTE))
	return 0;

  }
  /* yes, get ready, another awful kludge */
  save_loc = db[player].location;
  db[player].location = loc;
  init_match(player, mybuf, NOTYPE);
  match_perfect();
  db[player].location = save_loc;
  thing = match_result();

  /* && !controls(player, thing, POW_SPOOF) */
  if (thing != NOTHING 
  )
    return 0;
  return 1;
}

void do_announce(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  char *message;
  char buf[2000];

/* && !power(player, POW_SPOOF) */
  if (Guest(player) || (Typeof(player) != TYPE_PLAYER 
     ))
  {
    notify(player, "You can't do that.");
    return;
  }

  message = reconstruct_message(arg1, arg2);
  if (power(player, POW_ANNOUNCE) || payfor(player, announce_cost))
    if (*message == POSE_TOKEN)
      sprintf(buf, "%s announce-poses: %s %s", announce_name(player),
	      db[player].cname, message + 1);
    else if (*message == NOSP_POSE)
      sprintf(buf, "%s announce-poses: %s's %s", announce_name(player),
	      db[player].cname, message + 1);
    else if (*message == THINK_TOKEN)
      sprintf(buf, "%s announce-thinks: %s . o O ( %s )", announce_name(player),
	      db[player].cname, message + 1);
    else
      sprintf(buf, "%s announces \"%s\"", announce_name(player), message);
  else
  {
    notify(player, "Sorry, you don't have enough credits.");
    return;
  }
  log_io(tprintf("%s [owner=%s] executes: @announce %s", unparse_object_a(player, player),
	    unparse_object_a(db[player].owner, db[player].owner), message));
  com_send_as_hidden("pub_io",tprintf("%s [owner=%s] executes: @announce %s", unparse_object_a(player, player),
	    unparse_object_a(db[player].owner, db[player].owner), message), player);

  notify_all(buf, NOTHING, 1);

/*  old way.
  struct descriptor_data *d;

  strcat(buf, "\n");
  for (d = descriptor_list; d; d = d->next)
  {
    if (d->state != CONNECTED)
      continue;
    if (db[d->player].flags & PLAYER_NO_WALLS)
      continue;
    if (Typeof(d->player) != TYPE_PLAYER)
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
*/
}

void do_broadcast(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  char *message;
  char buf[2000];

  if (!power(player, POW_BROADCAST))
  {
    notify(player, "You don't have the authority to do that.");
    return;
  }

  message = reconstruct_message(arg1, arg2);
  sprintf(buf, "Official broadcast from %s: \"%s\"",
	  db[player].cname, message);

  log_important(tprintf("%s executes: @broadcast %s",
			unparse_object_a(player, player), message));

  notify_all(buf, NOTHING, 0);

/*  old way
  struct descriptor_data *d;

  for (d = descriptor_list; d; d = d->next)
  {
    if (d->state != CONNECTED)
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
*/
}

void do_gripe(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  dbref loc;
  char *message;

  loc = db[player].location;
  message = reconstruct_message(arg1, arg2);
  log_gripe(tprintf("|R+GRIPE| from %s in %s: %s",
		    unparse_object_a(player, player),
		    unparse_object_a(loc, loc),
		    message));

  notify(player, "Your complaint has been duly noted.");
}

void do_pray(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  dbref loc;

  loc = db[player].location;
  log_prayer(tprintf("|G+PRAYER| from %s in %s to the god %s: %s",
		     unparse_object_a(player, player),
		     unparse_object_a(loc, loc), arg1, arg2));

  notify(player, tprintf("%s has heard your prayer, and will consider granting it.", arg1));
}

char *title(player)
dbref player;
{
  char buf[2048];

  if (!*atr_get(player, A_ALIAS))
    return stralloc(db[player].cname);
  sprintf(buf, "%s (%s)", db[player].cname, atr_get(player, A_ALIAS));
  return stralloc(buf);
}

void do_page(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  dbref *x;
  int k;

  x = lookup_players(player, arg1);
  if (x[0] == 0)
    return;
  if (!payfor(player, page_cost * (x[0])))
  {
    notify(player, "You don't have enough Credits.");
    return;
  }
  for (k = 1; k <= x[0]; k++)
  {
    if ((db[x[k]].owner == x[k])
	? (!(db[x[k]].flags & CONNECT))
	: !(*atr_get(x[k], A_APAGE) || Hearer(x[k])))  /* should this be A_APAGE or A_LPAGE? */
    {
      notify(player, tprintf("%s isn't connected.", db[x[k]].cname));
      if (*Away(x[k]))
      {
	notify(player,
	       tprintf("|C!+Away message from %s:| %s", spname(x[k]),
		       atr_get(x[k], A_AWAY)));
      }
    }
    else if (!could_doit(player, x[k], A_LPAGE))
    {
      notify(player,
	     tprintf("|R+%s is not accepting pages.|", spname(x[k])));
      if (*atr_get(x[k], A_HAVEN))
      {
	notify(player, tprintf("|R+Haven message from| %s|R+:| %s",
			       spname(x[k]),
			       atr_get(x[k], A_HAVEN)));
      }
    }
    else if (!could_doit(x[k], player, A_LPAGE))
    {
      notify(player,
             tprintf("|R!+%s is not allowed to page you, therefore, you can't page them.|", spname(x[k])));
    }
    else
    {
      char *hidden;

      if(strlen(atr_get(player, A_LHIDE)) || strlen(atr_get(player, A_BLACKLIST)))
/*      if(strlen(atr_get(player, A_LHIDE))) */
      {
        hidden=tprintf(" (HIDDEN) ");
      }
      else
      {
        hidden=tprintf(" ");
      }
      if (!*arg2)
      {
	notify(x[k],
	       tprintf("You sense that %s%sis looking for you in %s",
		       spname(player), hidden, db[db[player].location].cname));
	notify(player,
	       tprintf("You notified %s of your location.%s", spname(x[k]), hidden));
	/* wptr[0]=arg2; *//* Do not pass %0 */
	did_it(player, x[k], NULL, 0, NULL, 0, A_APAGE);
      }
      else if (*arg2 == POSE_TOKEN)
      {
	notify(x[k], tprintf("%s%spage-poses: %s %s", title(player), hidden, spname(player), arg2 + 1));
	notify(player, tprintf("You page-posed %s with \"%s %s\".%s",
			       db[x[k]].cname, spname(player), arg2 + 1, hidden));
	if (db[x[k]].owner != x[k])
	  wptr[0] = arg2;
	did_it(player, x[k], NULL, 0, NULL, 0, A_APAGE);
      }
      else if (*arg2 == NOSP_POSE)
      {
	notify(x[k], tprintf("%s%spage-poses: %s's %s",
			     title(player), hidden, spname(player), arg2 + 1));
	notify(player, tprintf("You page-posed %s with \"%s's %s\".%s",
			       db[x[k]].cname, spname(player), arg2 + 1, hidden));
	if (db[x[k]].owner != x[k])
	  wptr[0] = arg2;
	did_it(player, x[k], NULL, 0, NULL, 0, A_APAGE);
      }
      else if (*arg2 == THINK_TOKEN)
      {
	notify(x[k], tprintf("%s%spage-thinks: %s . o O ( %s )",
			     title(player), hidden, spname(player), arg2 + 1));
	notify(player, tprintf("You page-thought %s with \"%s . o O ( %s )\".%s",
			       db[x[k]].cname, spname(player), arg2 + 1, hidden));
	if (db[x[k]].owner != x[k])
	  wptr[0] = arg2;
	did_it(player, x[k], NULL, 0, NULL, 0, A_APAGE);
      }
      else
      {
	notify(x[k], tprintf("%s%spages: %s", title(player), hidden, arg2));
	notify(player, tprintf("You paged %s with \"%s\".%s",
			       spname(x[k]), arg2, hidden));
	if (db[x[k]].owner != x[k])
	  wptr[0] = arg2;
	did_it(player, x[k], NULL, 0, NULL, 0, A_APAGE);
      }
      if (*Idle(x[k]) || atr_get(x[k], A_IDLE_CUR))
      {
	struct descriptor_data *d;

	/* find player's descriptor */
	for (d = descriptor_list; d && (d->player != x[k]); d = d->next) ;

	/* If the player is idle, send the idle message */
        if (db[x[k]].flags & PLAYER_IDLE)
	{
	  if(strlen(atr_get(x[k], A_IDLE_CUR)) > 0)
	  {
	    notify(player,
	  	   tprintf("|C!+Idle message from| %s |R+(||R!+%s||R+)||C!+:| %s", spname(x[k]),
			   time_format_2(now - d->last_time), 
                           atr_get(x[k], A_IDLE_CUR)));
	    notify(x[k],
		   tprintf("|W!+Your Idle message| |R+(||R!+%s||R+)||W!+ has been sent to| %s|W!+.|",
			   time_format_2(now - d->last_time), 
			   spname(player)));
	  }
          else
          {
            notify(player,
                   tprintf("%s |C!+is idle ||R+(||R!+%s||R+)|", spname(x[k]),
                           time_format_2(now - d->last_time)));
            notify(x[k],
                   tprintf("%s |W!+has been told you are ||R!+%s||W!+ idle.|",
                           spname(player),
                           time_format_2(now - d->last_time)));
          }
	}
      }
    }
  }
}

void do_use(player, arg1)
dbref player;
char *arg1;
{
  dbref thing;

  thing = match_thing(player, arg1);
  if (thing == NOTHING)
    return;
  did_it(player, thing, A_USE, "You don't know how to use that.", A_OUSE, NULL, A_AUSE);
}

void do_chemit(player, channel, message)
dbref player;
char *channel;
char *message;
{
  if (!*channel)
  {
    notify(player, "What channel?");
    return;
  }

  if (strchr(channel, ' '))
  {
    notify(player, "You're spacey.");
    return;
  }

  if (!message)
  {
    notify(player, "No message");
    return;
  }

/* removing pow_spoof - no longer needed with PUPPET flag 
  if (!power(player, POW_SPOOF))
  {
    notify(player, perm_denied());
    return;
  }
*/

  com_send_int(channel, message, player, 0);
}

void do_cemit(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  struct descriptor_data *d;
  long target;
  char buf[BUFFER_LEN], *bf = buf;

  if (!power(player, POW_REMOTE))
  {
    notify(player, "You don't have the authority to do that.");
    return;
  }

  if (!isdigit(*arg1))
  {
    notify(player, "That's not a number.");
    return;
  }

  target = atol(arg1);

  d = descriptor_list;
  while (d && d->concid != target)
    d = d->next;

  if (!d)
  {
    notify(player, "Unable to find specified concid.");
    return;
  }

  pronoun_substitute(buf, player, arg2, player);
  bf = buf + strlen(db[player].name) + 1;

  if (!(db[player].flags & QUIET))
    notify(player, tprintf("Concid %ld just saw \"%s\".", target, bf));

  if (d->state == CONNECTED)
  {
    notify(d->player, bf);
  }
  else
    /* not a connected player */
  {
    queue_string(d, strcat(bf, "\n"));
  }
}

void do_wemit(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  struct descriptor_data *d;
  char *message;
  char buf[BUFFER_LEN], *bf = buf;

  if (!power(player, POW_BROADCAST))
  {
    notify(player, perm_denied());
    return;
  }

  message = reconstruct_message(arg1, arg2);

  pronoun_substitute(buf, player, message, player);
  bf = buf + strlen(db[player].name) + 1;

  for (d = descriptor_list; d; d = d->next)
  {
    if (d->state != CONNECTED)
      continue;
    notify(d->player, bf);
  }
}


