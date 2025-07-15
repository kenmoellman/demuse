/* move.c */
/* $Id: move.c,v 1.10 1993/09/18 19:04:11 nils Exp $ */

#include "copyright.h"

#include "db.h"
#include "config.h"
#include "interface.h"
#include "match.h"
#include "externs.h"

int moveto(what, where)
dbref what;
dbref where;
{
  return(enter_room(what, where));
}

void moveit(what, where)
dbref what;
dbref where;
{
  dbref loc, old, player;

  if (Typeof(what) == TYPE_EXIT && Typeof(where) == TYPE_PLAYER)
  {
    log_error("Moving exit to player.");
    report();
    return;
  }

  player = db[what].location;

  /* remove what from old loc */
  if ((loc = old = db[what].location) != NOTHING)
  {
    if (Typeof(what) == TYPE_EXIT)
      db[loc].exits = remove_first(db[loc].exits, what);
    else
      db[loc].contents = remove_first(db[loc].contents, what);
    if (Hearer(what) && (old != where))
    {
      did_it(what, loc, A_LEAVE, NULL, A_OLEAVE, Dark(old) ? NULL : "has left.", A_ALEAVE);
    }
    db[loc].contents = remove_first(db[loc].contents, what);
  }

  /* test for special cases */
  switch (where)
  {
  case NOTHING:
    db[what].location = NOTHING;
    return;			/* NOTHING doesn't have contents */
  case HOME:
    if (Typeof(what) == TYPE_EXIT || Typeof(what) == TYPE_ROOM)
      return;
    where = db[what].link;	/* home */
    break;
  case BACK:
    if(Typeof(what) == TYPE_EXIT || Typeof(what) == TYPE_ROOM)
      return;
    where = atol(atr_get(what, A_LASTLOC));
    if(!where || where == NOTHING || where > db_top || IS_GONE(where))
    {
      notify(what, "You can't go back.");
      return;
    }
    break;
  }

  /* now put what in where */
  if (Typeof(what) == TYPE_EXIT)
    PUSH(what, db[where].exits);
  else
    PUSH(what, db[where].contents);

  atr_add(what, A_LASTLOC, tprintf("%ld", db[what].location));

  db[what].location = where;
/*  if (Hearer(what) && (where != NOTHING) && (old != where)) */
  if (where != NOTHING) 
  {
    if (old != where)
    {
      if (Hearer(what))
      {
        did_it(what, where, A_ENTER, NULL, A_OENTER, ((Dark(where))?NULL:"has arrived."), A_AENTER);
      }
    } 
  }
}
extern int enter_room();

#define Dropper(thing) (Hearer(thing) && (db[db[thing].owner].flags & CONNECT || db[thing].flags&CONNECT))

static void send_contents(loc, dest)
dbref loc;
dbref dest;
{
  dbref first;
  dbref rest;

  first = db[loc].contents;
  db[loc].contents = NOTHING;

  /* blast locations of everything in list */
  DOLIST(rest, first)
  {
    db[rest].location = NOTHING;
  }

  while (first != NOTHING)
  {
    rest = db[first].next;
    if (Dropper(first))
    {
      db[first].location = loc;
      PUSH(first, db[loc].contents);
    }
    else
      enter_room(first, (db[first].flags & STICKY) ? HOME : dest);
    first = rest;
  }

  db[loc].contents = reverse(db[loc].contents);
}

static void maybe_dropto(loc, dropto)
dbref loc;
dbref dropto;
{
  dbref thing;

  if (loc == dropto)
    return;			/* bizarre special case */
  if (Typeof(loc) != TYPE_ROOM)
    return;
  /* check for players */
  DOLIST(thing, db[loc].contents)
  {
    if (Dropper(thing))
      return;
  }

  /* no players, send everything to the dropto */
  send_contents(loc, dropto);
}

int enter_room(player, loc)
dbref player;
dbref loc;
{
  extern dbref speaker;
  dbref old;
  dbref zon;
  dbref dropto;
  int a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = 0;
  static int deep = 0;

  if (Typeof(player) == TYPE_ROOM)
  {
    notify(speaker, perm_denied());
    return(0);
  }

/* Fix so players can't get stuck in themselves */
  if(Typeof(player) == TYPE_PLAYER)
    if(loc == player)
    {
      notify(speaker, perm_denied());
      return(0);
    }

  if (Typeof(player) == TYPE_EXIT && !controls(player, loc, POW_MODIFY) &&
      !controls(speaker, loc, POW_MODIFY))
  {
    notify(speaker, perm_denied());
    return(0);
  }
  if (deep++ > 15)
  {
    deep--;
    return(0);
  }
  if (Typeof(loc) == TYPE_EXIT)
  {
    log_error(tprintf("Attempt to move %ld to exit %ld", player, loc));
    report();
    deep--;
    return(0);
  }
  /* check for room == HOME */
  if (loc == HOME)
    loc = db[player].link;	/* home */

  /* get old location */
  old = db[player].location;

  /* check for self-loop */
  /* self-loops don't do move or other player notification */
  /* but you still get autolook and penny check */

  /* go there */
  if (loc != old)
    did_it(player, player, A_MOVE, NULL, A_OMOVE, NULL, A_AMOVE);
  moveit(player, loc);
  if (loc != old)
    DOZONE(zon, player)
      did_it(player, zon, A_MOVE, NULL, A_OMOVE, NULL, A_AMOVE);

  /* if old location has STICKY dropto, send stuff through it */

  if ((a1 = (loc != old)) && (a2 = Dropper(player)) &&
      (a3 = (old != NOTHING)) && (a4 = (Typeof(old) == TYPE_ROOM))
      && (a5 = ((dropto = db[old].location) != NOTHING))
      && (a6 = (db[old].flags & STICKY)))
    maybe_dropto(old, dropto);

  /* autolook */
  look_room(player, loc);
  deep--;

  return(1);


}

/* teleports player to location while removing items they shouldnt take */
void safe_tel(player, dest)
dbref player;
dbref dest;
{
  dbref first;
  dbref rest;

  if ((Typeof(player) == TYPE_ROOM) || (Typeof(player) == TYPE_EXIT))
    return;

  if (dest == HOME)
    dest = db[player].link;
  else if(dest == BACK)
  {
    dest = atol(atr_get(player, A_LASTLOC));
    if(!dest || dest == NOTHING || dest > db_top || IS_GONE(dest))
    {
      notify(player, "You can't go back.");
      return;
    }
  }

  if (db[db[player].location].owner == db[dest].owner)
  {
    enter_room(player, dest);
    return;
  }

  first = db[player].contents;
  db[player].contents = NOTHING;

  /* blast locations of everything in list */
  DOLIST(rest, first)
  {
    db[rest].location = NOTHING;
  }

  while (first != NOTHING)
  {
    rest = db[first].next;
    /* if thing is ok to take then move to player else send home */
    if (controls(player, first, POW_MODIFY) ||
	(!(IS(first, TYPE_THING, THING_KEY)) && !(db[first].flags & STICKY)))
    {
      PUSH(first, db[player].contents);
      db[first].location = player;
    }
    else
      enter_room(first, HOME);
    first = rest;
  }
  db[player].contents = reverse(db[player].contents);
  enter_room(player, dest);
}

int can_move(player, direction)
dbref player;
char *direction;
{
  if ((Typeof(player) == TYPE_ROOM) || (Typeof(player) == TYPE_EXIT))
  {
    return 0;
  }
  if (!string_compare(direction, "home"))
    return 1;
  else if(!string_compare(direction, "back"))
    return(1);

  /* otherwise match on exits */
  init_match(player, direction, TYPE_EXIT);
  match_exit();
  return (last_match_result() != NOTHING);
}

void do_move(player, direction)
dbref player;
char *direction;
{
  long moves;
  dbref exit;
  dbref loc;
  dbref zresult;
  dbref old = db[player].location;
  dbref old_exit;
  int deep;
#ifdef USE_UNIV
  dbref univ_src, univ_dest;
#endif

  if ( (Typeof(player) == TYPE_ROOM) || (Typeof(player) == TYPE_EXIT) )
  {
    notify(player, "Sorry, rooms and exits aren't allowed to move.");
    return;
  }

  if ((Typeof(player) == TYPE_PLAYER) &&
      (IS(player, TYPE_PLAYER, PLAYER_FREEZE)))
  {
    notify(player, "You're frozen!  You can't move.");
    return;
  }

  if ((Typeof(player) == TYPE_THING) &&
      strcmp(atr_get(player, A_MOVES), ""))
  {
    if ((moves = atol(atr_get(player, A_MOVES))) == 0 &&
	strcmp(direction, "home"))
    {
      notify(player, "Sorry, you are out of moves.");
      return;
    }
    atr_add(player, A_MOVES, tprintf("%d", (moves <= 0) ? 0 : --moves));
  }

  if (!string_compare(direction, "home"))
  {
    /* send him home */
    /* but steal all his possessions */
    if (Typeof(player) == TYPE_ROOM ||
	Typeof(player) == TYPE_EXIT)
      return;

#ifdef USE_UNIV
/* see if teleport is enabled in the universe */
    univ_src = db[get_zone_first(player)].universe;
    univ_dest = db[get_zone_first(db[player].link)].universe;
    if (
    (!db[univ_src].ua_int[UA_TELEPORT] || !db[univ_dest].ua_int[UA_TELEPORT])
	 && !power(player, POW_TELEPORT))
    {
      notify(player, perm_denied());
      return;
    }
#endif

    if(db[player].location == db[player].link)
    {
      notify(player, "But you're already there!");
      return;
    }

    if (!check_zone(player, player, db[player].link, 2))
      return;

    if (((loc = db[player].location) != NOTHING) &&
	!IS(loc, TYPE_ROOM, ROOM_AUDITORIUM))
    {
      /* tell everybody else */
      notify_in(loc, player,
		tprintf("%s goes home.", db[player].cname));
    }
    /* give the player the messages */
    safe_tel(player, HOME);
  }
  else
  {
    /* find the exit */
    init_match_check_keys(player, direction, TYPE_EXIT);
    match_exit();
    switch (exit = match_result())
    {
    case NOTHING:
      /* try to force the object */
      notify(player, "You can't go that way.");
      break;
    case AMBIGUOUS:
      notify(player, "I don't know which way you mean!");
      break;
    default:
      /* we got one */
      /* check to see if we got through */
      if (could_doit(player, exit, A_LOCK))
      {
	if ((zresult = check_zone(player, player, db[exit].link, 0)))
	{
	  did_it(player, exit, A_SUCC, NULL, A_OSUCC, (db[exit].flags & DARK) ? NULL : stralloc(tprintf("goes through the exit marked %s.", main_exit_name(exit))), A_ASUCC);
	  switch (Typeof(db[exit].link))
	  {
	  case TYPE_ROOM:
	    enter_room(player, db[exit].link);
	    break;
	  case TYPE_PLAYER:
	  case TYPE_THING:
	  case TYPE_CHANNEL:
#ifdef USE_UNIV
	  case TYPE_UNIVERSE:
#endif
	    if (db[db[exit].link].flags & GOING)
	    {
	      notify(player, "You can't go that way.");
	      return;
	    }
	    if (db[db[exit].link].location == NOTHING)
	      return;
	    safe_tel(player, db[exit].link);
	    break;
	  case TYPE_EXIT:
/* old - replaced by MAZE stuff 
	    notify(player, "This feature coming soon.");
	    break;
 * I dunno how much I trust this.... */

            old_exit = exit;
            for(deep = 0;Typeof(db[exit].link) == TYPE_EXIT;deep++)
            {
              exit = db[exit].link;
              if(deep > 99)        /* Continuous loop check */
              {
                log_error(tprintf("%s links to too many exits.",
                  unparse_object(root, old_exit)));
                notify(player, "You can't go that way.");
                return;
              }
            }
            enter_room(player, db[exit].link);
            break;
/* end untrusted code */
	  }
	  did_it(player, exit, A_DROP, NULL, A_ODROP, (db[exit].flags & DARK) ? NULL : stralloc(tprintf("arrives from %s.", db[old].name)), A_ADROP);
	  if (zresult > (dbref) 1)
	    /* we entered a new zone */
	    did_it(player, zresult, A_DROP, NULL, A_ODROP, NULL, A_ADROP);
	}
      }
      else
	did_it(player, exit, A_FAIL, "You can't go that way.",
	       A_OFAIL, NULL, A_AFAIL);
      break;
    }
  }
}

void do_get(player, what)
dbref player;
char *what;
{
  dbref thing;
  dbref loc = db[player].location;

  if(!*what)
  {
    notify(player, "Take what?");
    return;
  }

  if (Typeof(player) == TYPE_EXIT)
  {
    notify(player, "You can't pick up things!");
    return;
  }
  if ((Typeof(loc) != TYPE_ROOM) && !(db[loc].flags & ENTER_OK) &&
      !controls(player, loc, POW_TELEPORT))
  {
    notify(player, perm_denied());
    return;
  }
  init_match_check_keys(player, what, TYPE_THING);
  match_neighbor();
  match_exit();
  if (power(player, POW_TELEPORT))
    match_absolute();		/* the wizard has long fingers */

  if ((thing = noisy_match_result()) != NOTHING)
  {

    if (db[thing].location == player)
    {
      notify(player, "You already have that!");
      return;
    }
    switch (Typeof(thing))
    {
    case TYPE_PLAYER:
    case TYPE_CHANNEL:
#ifdef USE_UNIV
    case TYPE_UNIVERSE:
#endif
      notify(player, perm_denied());
      break;
    case TYPE_THING:
      if (could_doit(player, thing, A_LOCK))
      {
	if(moveto(thing, player))
	{
	  notify(thing, tprintf("You have been picked up by %s.",
			      unparse_object(thing, player)));
	  did_it(player, thing, A_SUCC, "Taken.", A_OSUCC, NULL, A_ASUCC);
	}
        else
	  did_it(player, thing, A_FAIL, "You can't pick that up.",
	       A_OFAIL, NULL, A_AFAIL);
      }
      else
	did_it(player, thing, A_FAIL, "You can't pick that up.",
	       A_OFAIL, NULL, A_AFAIL);
      break;
    case TYPE_EXIT:
      notify(player, "You can't pick up exits.");
      return;
    default:
      notify(player, "You can't take that!");
      break;
    }
  }
}

void do_drop(player, name)
dbref player;
char *name;
{
  dbref loc;
  dbref thing;
  char buf[BUFFER_LEN];

  if ((loc = getloc(player)) == NOTHING)
    return;

  if(!*name)
  {
    notify(player, "Drop what?");
    return;
  }

  init_match(player, name, TYPE_THING);
  match_possession();

  switch (thing = match_result())
  {
  case NOTHING:
    notify(player, "You don't have that!");
    return;
  case AMBIGUOUS:
    notify(player, "I don't know which you mean!");
    return;
  default:
    if(db[db[player].location].owner != player && !Wizard(db[player].owner))
    {
      notify(player, perm_denied());
      return;
    }
    if (db[thing].location != player)
    {
      /* Shouldn't ever happen. */
      notify(player, "You can't drop that.");
    }
    else if (Typeof(thing) == TYPE_EXIT)
    {
      notify(player, "Sorry you can't drop exits.");
      return;
    }
    else if (db[thing].flags & STICKY)
    {
      notify(thing, "Dropped.");
      safe_tel(thing, HOME);
    }
    else if (db[loc].link != NOTHING &&
	     (Typeof(loc) == TYPE_ROOM) && !(db[loc].flags & STICKY))
    {
      if(moveto(thing, player))
      {
      /* location has immediate dropto */
        notify(thing, "Dropped.");
        moveto(thing, db[loc].link);
      }
      else
      {
        did_it(player, thing, A_FAIL, "You can't pick that up.",
             A_OFAIL, NULL, A_AFAIL);
        return;
      }

    }
    else
    {
      notify(thing, "Dropped.");
      enter_room(thing, loc);
      /* sprintf(buf, "%s dropped %s.", db[player].name, db[thing].name);
         notify_in(loc, player, buf); */
    }
    break;
  }

  sprintf(buf, "dropped %s.", db[thing].name);
  did_it(player, thing, A_DROP, "Dropped.", A_ODROP, buf, A_ADROP);
}

void do_enter(player, what)
dbref player;
char *what;
{
  dbref thing;

  if ((Typeof(player) == TYPE_PLAYER) &&
      (IS(player, TYPE_PLAYER, PLAYER_FREEZE)))
  {
    notify(player, "You're frozen!  You can't move.");
    return;
  }

  init_match_check_keys(player, what, TYPE_THING);
  match_neighbor();
  match_exit();
  if (power(player, POW_TELEPORT))
    match_absolute();		/* the wizard has long fingers */

  if ((thing = noisy_match_result()) == NOTHING)
  {
    /* notify(player,"I don't see that here.");   */
    return;
  }
  switch (Typeof(thing))
  {
  case TYPE_ROOM:
  case TYPE_EXIT:
    notify(player, perm_denied());
    break;
  default:
    if (!(db[thing].flags & ENTER_OK) && !controls(player, thing, POW_TELEPORT))
    {
      did_it(player, thing, A_EFAIL, "You can't enter that.", A_OEFAIL,
	     NULL, A_AEFAIL);
      return;
    }
    if (could_doit(player, thing, A_ELOCK) &&
	check_zone(player, player, thing, 0))
    {
      safe_tel(player, thing);
    }
    else
    {
      did_it(player, thing, A_EFAIL, "You can't enter that.", A_OEFAIL,
	     NULL, A_AEFAIL);
    }
    break;
  }
}

void do_leave(player)
dbref player;
{
  if ((Typeof(player) == TYPE_PLAYER) &&
      (IS(player, TYPE_PLAYER, PLAYER_FREEZE)))
  {
    notify(player, "You're frozen!  You can't move.");
    return;
  }

  if (Typeof(db[player].location) == TYPE_ROOM ||
      Typeof(db[player].location) == TYPE_EXIT ||
      db[db[player].location].location == NOTHING)
  {
    notify(player, "You can't leave.");
    return;
  }
  if (could_doit(player, db[player].location, A_LLOCK))
  {
    enter_room(player, db[db[player].location].location);
  }
  else
  {
    did_it(player, db[player].location, A_LFAIL, "You can't leave.", A_OLFAIL,
	   NULL, A_ALFAIL);
  }
}

/* Find the room the object/player is in */
dbref get_room(thing)
dbref thing;
{
  int depth = 10;
  dbref holder;

  for (holder = thing; depth; depth--, holder = thing,
       thing = db[holder].location)
    if (Typeof(thing) == TYPE_ROOM)
    {
      return thing;
    }
  return (dbref) 0;
}
/* End move.c */
