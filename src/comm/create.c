/* create.c */
/* $Id: create.c,v 1.15 1994/02/18 22:51:55 nils Exp $ */


#include "db.h"
#include "config.h"
#include "interface.h"
#include "externs.h"

/* utility for open and link */
static dbref parse_linkable_room(player, room_name)
dbref player;
char *room_name;
{
  dbref room;

  /* skip leading NUMBER_TOKEN if any */
  if (*room_name == NUMBER_TOKEN)
    room_name++;

  /* parse room */
  if (!string_compare(room_name, "here"))
  {
    room = db[player].location;
  }
  else if (!string_compare(room_name, "home"))
  {
    return HOME;		/* HOME is always linkable */
  }
  else
  {
    room = parse_dbref(room_name);
  }

  /* check room */
  if (room < 0 || room >= db_top || Typeof(room) == TYPE_EXIT)
  {
    /* !!! if Wizard and unsafe do it any ways !!! */
    /* if (power(player, TYPE_DIRECTOR) && unsafe) return(room); */
    if (room < 0 || room >= db_top)
      notify(player,
	     tprintf("#%ld is not a valid object.", room));
    else
      notify(player,
	     tprintf("%s is an exit!", unparse_object(player, room)));
    return NOTHING;
  }
  else if (!can_link_to(player, room, POW_MODIFY))
  {
    /* !!! ... !!! XX */
    /* if (power(player, TYPE_DIRECTOR) && unsafe) return(room); */
    notify(player, tprintf("You can't link to %s.",
			   unparse_object(player, room)));
    return NOTHING;
  }
  else
  {
    return room;
  }
}

/* use this to create an exit */
void do_open(player, direction, linkto, pseudo)
dbref player;
char *direction;
char *linkto;
dbref pseudo;			/* a phony location for a player if a back

				   exit is needed */
{
  dbref loc = (pseudo != NOTHING) ? pseudo : db[player].location;
  dbref exit;
  dbref loczone;
  dbref linkzone;

  if ((loc == NOTHING) || (Typeof(loc) == TYPE_PLAYER))
  {
    notify(player, "Sorry you can't make an exit there.");
    return;
  }

  if (!*direction)
  {
    notify(player, "Open where?");
    return;
  }
  else if (!ok_exit_name(direction))
  {
    notify(player, tprintf("%s is a strange name for an exit!", direction));
    return;
  }

  if (!controls(player, loc, POW_MODIFY))
  {
    notify(player, perm_denied());
  }
  else if (can_pay_fees(def_owner(player), exit_cost, QUOTA_COST))
  {
    /* create the exit */
    exit = new_object();

    /* initialize everything */
    SET(db[exit].name, direction);
    SET(db[exit].cname, direction);
    db[exit].owner = def_owner(player);
    db[exit].zone = NOTHING;
    db[exit].flags = TYPE_EXIT;
    db[exit].flags |= (db[db[exit].owner].flags & INHERIT_POWERS);
/*     check_spoofobj(player, exit); */

    /* link it in */
    PUSH(exit, Exits(loc));
    db[exit].location = loc;
    db[exit].link = NOTHING;
    /* and we're done */
    notify(player, tprintf("%s opened.", direction));

    /* check second arg to see if we should do a link */
    if (*linkto != '\0')
    {
      if ((loc = parse_linkable_room(player, linkto)) != NOTHING)
      {
	if (!payfor(player, link_cost) && !power(player, POW_FREE))
	{
	  notify(player, "You don't have enough Credits to link.");
	}
	else
	{
	  loczone = get_zone_first(player);
	  linkzone = get_zone_first(loc);

	  /* it's ok, link it */
	  db[exit].link = loc;
	  notify(player, tprintf("Linked to %s.", unparse_object(player, loc)));
	}
      }
    }
  }
}

/* use this to link to a room that you own */
/* it seizes ownership of the exit */
/* costs 1 penny */
/* plus a penny transferred to the exit owner if they aren't you */
/* you must own the linked-to room AND specify it by room number */
void do_link(player, name, room_name)
dbref player;
char *name;
char *room_name;
{
  dbref thing;
  dbref room;

  init_match(player, name, TYPE_EXIT);
  match_everything();

  if ((thing = noisy_match_result()) != NOTHING)
  {
    switch (Typeof(thing))
    {
    case TYPE_EXIT:
      if ((room = parse_linkable_room(player, room_name)) == NOTHING)
	return;

      if ((room != HOME) && !controls(player, room, POW_MODIFY) &&
	  !(db[room].flags & LINK_OK))
      {
	notify(player, perm_denied());
	break;
      }

      /* we're ok, check the usual stuff */
      if (db[thing].link != NOTHING)
      {
	if (controls(player, thing, POW_MODIFY))
	{
	  /* if(Typeof(db[thing].link) == TYPE_PLAYER) notify(player, "That
	     exit is being carried."); else */
	  notify(player, tprintf("%s is already linked.",
				 unparse_object(player, thing)));
	}
	else
	{
	  notify(player, perm_denied());
	}
      }
      else
      {
	/* handle costs */
	if (db[thing].owner == db[player].owner)
	{
	  if (!payfor(player, link_cost) && !power(player, POW_FREE))
	  {
	    notify(player,
		   "It costs a Credit to link this exit.");
	    return;
	  }
	}
	else
	{
	  if (!can_pay_fees(def_owner(player),
			    link_cost + exit_cost, QUOTA_COST))
	    return;
	  else
	  {
	    /* pay the owner for his loss */
	    if (!power(db[thing].owner, POW_FREE))
	      giveto(db[thing].owner, exit_cost);
	    add_quota(db[thing].owner, QUOTA_COST);
	  }
	}

	/* link has been validated and paid for; do it */
	db[thing].owner = def_owner(player);
	if (!(db[player].flags & INHERIT_POWERS))
	  db[thing].flags &= ~(INHERIT_POWERS);
	db[thing].link = room;

	/* notify the player */
	notify(player, tprintf("%s linked to %s.",
			       unparse_object_a(player, thing),
			       unparse_object_a(player, room)));
      }
      break;
    case TYPE_PLAYER:
    case TYPE_THING:
    case TYPE_CHANNEL:
#ifdef USE_UNIV
    case TYPE_UNIVERSE:
#endif
      init_match(player, room_name, NOTYPE);
      match_exit();
      match_neighbor();
      match_possession();
      match_me();
      match_here();
      match_absolute();
      match_player();
      if ((room = noisy_match_result()) < 0)
      {
	/* notify(player,"No match."); noisy_match_result talks bout it */
	return;
      }
      if (Typeof(room) == TYPE_EXIT)
      {
	notify(player, tprintf("%s is an exit.", unparse_object(player, room)));
	return;
      }
      /* abode */
      if (!controls(player, room, POW_MODIFY) &&
	  !(db[room].flags & LINK_OK))
      {
	notify(player, perm_denied());
	break;
      }
      if (!controls(player, thing, POW_MODIFY)
	&& ((db[thing].location != player) || !(db[thing].flags & LINK_OK)))
      {
	notify(player, perm_denied());
      }
      else if (room == HOME)
      {
	notify(player, "Can't set home to home.");
      }
      else
      {
	/* do the link */
	db[thing].link = room;	/* home */
	notify(player, tprintf("Home set to %s.", unparse_object(player, room)));
      }
      break;
    case TYPE_ROOM:
      if ((room = parse_linkable_room(player, room_name)) == NOTHING)
	return;

      if (Typeof(room) != TYPE_ROOM)
      {
	notify(player, tprintf("%s is not a room!",
			       unparse_object(player, room)));
	return;
      }
      if ((room != HOME) && !controls(player, room, POW_MODIFY) &&
	  !(db[room].flags & LINK_OK))
      {
	notify(player, perm_denied());
	break;
      }
      if (!controls(player, thing, POW_MODIFY))
      {
	notify(player, perm_denied());
      }
      else
      {
	/* do the link, in location....no, in link! yay! */
	db[thing].link = room;	/* dropto */
	notify(player, tprintf("Dropto set to %s.",
			       unparse_object(player, room)));
      }
      break;
    default:
      notify(player, "Internal error: weird object type.");
      log_error(tprintf("PANIC weird object: Typeof(%d) = %ld",
			(int)thing, Typeof(thing)));
      report();
      break;
    }
  }
}

#ifdef USE_UNIV
/* Links a room to a "universe object" */
void do_ulink(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  dbref object;
  dbref univ;

  init_match(player, arg1, TYPE_THING);
  match_neighbor();
  match_possession();
  match_absolute();
  if ((object = noisy_match_result()) != NOTHING)
  {
    init_match(player, arg2, TYPE_UNIVERSE);
    match_neighbor();
    match_possession();
    match_absolute();
    if ((univ = noisy_match_result()) != NOTHING)
    {
      if (Typeof(univ) == TYPE_UNIVERSE)
      {
	if (!(controls(player, univ, POW_MODIFY) &&
	      controls(player, object, POW_MODIFY)))
	{
	  notify(player, perm_denied());
	}
	else if (db[object].universe == univ)
	{
	  notify(player, "Already linked to that universe.");
	}
	else
	{
	  db[object].universe = univ;
	  notify(player, tprintf("%s(#%ld) universe set to %s(#%ld)",
			     db[object].name, object, db[univ].name, univ));
	}
      }
      else
	notify(player, "That is not a valid Universe.");
    }
  }
}

void do_unulink(player, arg1)
dbref player;
char *arg1;
{
  dbref thing;

  init_match(player, arg1, TYPE_THING);
  match_neighbor();
  match_possession();
  match_absolute();
  if ((thing = noisy_match_result()) != NOTHING)
  {
    if (!controls(player, thing, POW_MODIFY))
    {
      notify(player, perm_denied());
    }
    else
    {
      db[thing].universe = db[0].universe;
      notify(player, "Universe unlinked.");
    }
  }
  return;
}
#endif

/* Links a room to a "zone object" */
void do_zlink(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  dbref room;
  dbref object;
  dbref old_zone;

  init_match(player, arg1, TYPE_ROOM);
  match_here();
  match_absolute();
  if ((room = noisy_match_result()) != NOTHING)
  {
    init_match(player, arg2, TYPE_THING);
    match_neighbor();
    match_possession();
    match_absolute();
    old_zone = get_zone_first(room);
    if ((object = noisy_match_result()) != NOTHING)
    {
      if ((!(controls(player, room, POW_MODIFY) &&
	     controls(player, object, POW_MODIFY)))
	  || (Typeof(room) != TYPE_ROOM &&
	      (player != root)))
      {
	notify(player, perm_denied());
      }
      else if (is_in_zone(object, room))
      {
	notify(player, "Already linked to that zone.");
      }
      else
      {
	if (db[object].zone == NOTHING && object != db[0].zone)
	  db[object].zone = db[0].zone;		/* silly person doesn't know
						   * how to set up a zone *
						   right.. */
	db[room].zone = object;
	notify(player, tprintf("%s zone set to %s", db[room].name,
			       db[object].name));
      }
    }
  }
}

void do_unzlink(player, arg1)
dbref player;
char *arg1;
{
  dbref room;

  init_match(player, arg1, TYPE_ROOM);
  match_here();
  match_absolute();
  if ((room = noisy_match_result()) != NOTHING)
  {
    if (!controls(player, room, POW_MODIFY))
    {
      notify(player, perm_denied());
    }
    else
    {
      if (Typeof(room) == TYPE_ROOM)
	db[room].zone = db[0].zone;
      else
	db[room].zone = NOTHING;
      notify(player, "Zone unlinked.");
    }
  }
  return;
}

/* special link - sets the global zone */
void do_gzone(player, arg1)
dbref player;
char *arg1;
{
  dbref thing, p, oldu;

  if (player != root)
  {
    notify(player, "You don't have the authority. So sorry.");
    return;
  }
  init_match(player, arg1, TYPE_THING);
  match_possession();
  match_neighbor();
  match_absolute();
  if ((thing = noisy_match_result()) != NOTHING)
  {
    oldu = db[0].zone;
    db[0].zone = thing;
    for (p = 0; p < db_top; p++)
      if ((Typeof(p) == TYPE_ROOM) && !(db[p].flags & GOING) &&
	  ((db[p].zone == oldu) || (db[p].zone == NOTHING)))
	db[p].zone = thing;
  }
  db[thing].zone = NOTHING;
  notify(player, tprintf("Global zone set to %s.", db[thing].name));
}

#ifdef USE_UNIV
/* special GOD command to set the global universe */
void do_guniverse(player, arg1)
dbref player;
char *arg1;
{
  dbref thing, p, oldu;

  if (player != root)
  {
    notify(player, perm_denied());
    return;
  }
  init_match(player, arg1, TYPE_UNIVERSE);
  match_possession();
  match_neighbor();
  match_absolute();
  if ((thing = noisy_match_result()) != NOTHING)
  {
    if (Typeof(thing) == TYPE_UNIVERSE)
    {
      oldu = db[0].universe;
      for (p = 0; p < db_top; p++)
	if (!(db[p].flags & GOING) &&
	    ((db[p].universe == oldu) || (db[p].universe == NOTHING)))
	  db[p].universe = thing;
      notify(player, tprintf("Global universe set to #%ld.", thing));
    }
    else
      notify(player, "That is not a valid Universe.");
  }
}
#endif

/* use this to create a room */
void do_dig(player, name, argv)
dbref player;
char *name;
char *argv[];
{
  dbref room;
  dbref where;

  /* we don't need to know player's location!  hooray! */
  where = db[player].location;	/* MMZ */
  if (*name == '\0')
  {
    notify(player, "Dig what?");
  }
  else if (!ok_room_name(name))
  {
    notify(player, "That's a silly name for a room!");
  }
  else if (can_pay_fees(def_owner(player), room_cost, QUOTA_COST))
  {
    room = new_object();

    /* Initialize everything */
    SET(db[room].name, name);
    SET(db[room].cname, name);
    db[room].owner = def_owner(player);
    db[room].flags = TYPE_ROOM;
    db[room].location = room;
    db[room].zone = db[where].zone;
    db[room].flags |= (db[db[room].owner].flags & INHERIT_POWERS);
/*     check_spoofobj(player, room); */
    notify(player,
	   tprintf("%s created with room number %ld.", name, room));
    if (argv[1] && *argv[1])
    {
      char nbuff[50];

      sprintf(nbuff, "%ld", room);
      do_open(player, argv[1], nbuff, NOTHING);
    }
    if (argv[2] && *argv[2])
    {
      char nbuff[50];

      sprintf(nbuff, "%ld", db[player].location);
      do_open(player, argv[2], nbuff, room);
    }
  }
}

/*
void check_spoofobj(player, thing)
dbref player, thing;
{
  dbref p;

  if ((p = starts_with_player(db[thing].name)) != NOTHING && !controls(player, p, POW_SPOOF) && !(db[thing].flags & HAVEN))
  {
    notify(player, tprintf("Warning: %s can be used to spoof an existing player. It has as been set haven.", unparse_object(player, thing)));
    db[thing].flags |= HAVEN;
  }
}
*/

/* use this to create an object */
void do_create(player, name, cost)
dbref player;
char *name;
int cost;
{
  dbref loc;
  dbref thing;

  if (*name == '\0')
  {
    notify(player, "Create what?");
    return;
  }
  else if (!ok_thing_name(name))
  {
    notify(player, "That's a silly name for a thing!");
    return;
  }
  else if (cost < 0)
  {
    notify(player, "You can't create an object for less than nothing!");
    return;
  }
  else if (cost < thing_cost)
    cost = thing_cost;

  if (can_pay_fees(def_owner(player), cost, QUOTA_COST))
  {
    /* create the object */
    thing = new_object();

    /* initialize everything */
    SET(db[thing].name, name);
    SET(db[thing].cname, name);
    db[thing].location = player;
    db[thing].zone = NOTHING;
    db[thing].owner = def_owner(player);
    s_Pennies(thing, (long)OBJECT_ENDOWMENT(cost));
    db[thing].flags = TYPE_THING;
    db[thing].flags |= (db[db[thing].owner].flags & INHERIT_POWERS);
/*    check_spoofobj(player, thing); */
    /* endow the object */
    if (Pennies(thing) > MAX_OBJECT_ENDOWMENT)
    {
      s_Pennies(thing, (long)MAX_OBJECT_ENDOWMENT);
    }

    /* home is here (if we can link to it) or player's home */
    if ((loc = db[player].location) != NOTHING
	&& controls(player, loc, POW_MODIFY))
    {
      db[thing].link = loc;
    }
    else
    {
      db[thing].link = db[player].link;
    }

    db[thing].exits = NOTHING;
    /* link it in */
    PUSH(thing, db[player].contents);

    /* and we're done */
    notify(player, tprintf("%s created.", unparse_object(player, thing)));
  }
}

#ifdef USE_UNIV
/* use this to create a UNIVERSE object */
void do_ucreate(player, name, cost)
dbref player;
char *name;
int cost;
{
  dbref loc;
  dbref thing;

  if (*name == '\0')
  {
    notify(player, "Create what?");
    return;
  }
  else if (!ok_thing_name(name))
  {
    notify(player, "That's a silly name for a thing!");
    return;
  }
  else if (cost < 0)
  {
    notify(player, "You can't create an object for less than nothing!");
    return;
  }
  else if (cost < univ_cost)
    cost = univ_cost;

  if (!power(player, POW_SECURITY))
  {
    notify(player, "Foolish mortal!  You can't make Universes.");
    return;
  }

  if (can_pay_fees(def_owner(player), cost, QUOTA_COST))
  {
    /* create the object */
    thing = new_object();

    /* initialize everything */
    SET(db[thing].name, name);
    SET(db[thing].cname, name);
    db[thing].location = player;
    db[thing].zone = NOTHING;
    db[thing].owner = def_owner(player);
    s_Pennies(thing, (long)OBJECT_ENDOWMENT(cost));
    db[thing].flags = TYPE_UNIVERSE;
    db[thing].flags |= (db[db[thing].owner].flags & INHERIT_POWERS);
/*     check_spoofobj(player, thing); */
    /* endow the object */
    if (Pennies(thing) > MAX_OBJECT_ENDOWMENT)
    {
      s_Pennies(thing, (long)MAX_OBJECT_ENDOWMENT);
    }

    /* home is here (if we can link to it) or player's home */
    if ((loc = db[player].location) != NOTHING
	&& controls(player, loc, POW_MODIFY))
    {
      db[thing].link = loc;
    }
    else
    {
      db[thing].link = db[player].link;
    }

    db[thing].exits = NOTHING;
    /* link it in */
    PUSH(thing, db[player].contents);

    /* do the thing that sets this apart from just an ordinary Thing */
    init_universe(&db[thing]);

    /* and we're done */
    notify(player, tprintf("%s created.", unparse_object(player, thing)));
  }
}

void init_universe(o)
struct object *o;
{
  int x;

  o->ua_string = (char **)malloc(NUM_UA * sizeof(char *));
  o->ua_float = (float *)malloc(NUM_UA * sizeof(float));
  o->ua_int = (int *)malloc(NUM_UA * sizeof(int));

  for (x = 0; x < NUM_UA; x++)
  {
    switch (univ_config[x].type)
    {
    case UF_BOOL:
    case UF_INT:
      o->ua_int[x] = atoi(univ_config[x].def);
      o->ua_string[x] = (char *)NULL;
      break;
    case UF_FLOAT:
      o->ua_float[x] = atof(univ_config[x].def);
      o->ua_string[x] = (char *)NULL;
      break;
    case UF_STRING:
      o->ua_string[x] = (char *)malloc((strlen(univ_config[x].def) + 1) *
				       sizeof(char));

      strcpy(o->ua_string[x], univ_config[x].def);
      break;
    }
  }
}
#endif

void do_clone(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  dbref clone, thing;

  if (Guest(db[player].owner))
  {
    notify(player, "Guests can't clone objects.");
    return;
  }
  init_match(player, arg1, NOTYPE);
  match_everything();

  thing = noisy_match_result();
  if ((thing == NOTHING) || (thing == AMBIGUOUS))
    return;

  if (!controls(player, thing, POW_SEEATR))
  {
    notify(player, perm_denied());
    return;
  }
  if (Typeof(thing) != TYPE_THING)
  {
    notify(player, "You can only clone things.");
    return;
  }
  if (!can_pay_fees(def_owner(player), thing_cost, QUOTA_COST))
  {
    notify(player, "You don't have enough money.");
    return;
  }
  clone = new_object();
  memcpy(&db[clone], &db[thing], sizeof(struct object));

  db[clone].name = NULL;
  db[clone].cname = NULL;
  db[clone].owner = def_owner(player);
  db[clone].flags &= ~(HAVEN | BEARING);	/* common parent flags */
  if (!(db[player].flags & INHERIT_POWERS))
    db[clone].flags &= ~INHERIT_POWERS;
  SET(db[clone].name, (*arg2) ? arg2 : db[thing].name);
  SET(db[clone].cname, (*arg2) ? arg2 : db[thing].cname);
/*   check_spoofobj(player, clone); */
  s_Pennies(clone, (long)1);
  atr_cpy_noninh(clone, thing);	/* copies the noninherited attributes. */
  db[clone].contents = db[clone].location = db[clone].next = NOTHING;
  db[clone].atrdefs = NULL;
  db[clone].parents = NULL;
  db[clone].children = NULL;
  PUSH_L(db[clone].parents, thing);
  PUSH_L(db[thing].children, clone);
  notify(player, tprintf("%s cloned with number %ld.",
			 unparse_object(player, thing), clone));
  moveto(clone, db[player].location);
  did_it(player, clone, NULL, NULL, NULL, NULL, A_ACLONE);
}

void do_robot(player, name, pass)
dbref player;
char *name;
char *pass;
{
  dbref thing;

  if (!power(player, POW_PCREATE))
  {
    notify(player, "You can't make robots.");
    return;
  }
  if (!can_pay_fees(def_owner(player), robot_cost, QUOTA_COST))
  {
    notify(player, "Sorry you don't have enough money to make a robot.");
    return;
  }
  if ((thing = create_player(name, pass, CLASS_VISITOR, player_start)) == NOTHING)
  {
    if (!power(player, POW_FREE))
      giveto(player, robot_cost);
    add_quota(player, QUOTA_COST);
    notify(player, tprintf("%s already exists.", name));
    return;
  }
  db[thing].owner = db[player].owner;
  atr_clr(thing, A_RQUOTA);
  enter_room(thing, db[player].location);
  notify(player, tprintf("%s has arrived.", unparse_object(player, thing)));
}

