
/* destroy.c */
/* $Id: destroy.c,v 1.8 1993/09/18 19:03:56 nils Exp $ */


#include <stdio.h>
#include <ctype.h>

#include "db.h"
#include "config.h"
#include "externs.h"


dbref first_free = NOTHING;
static void dbmark P((dbref));
static void dbunmark P((void));
static void dbmark1 P((void));
static void dbunmark1 P((void));
static void dbmark2 P((void));
static void mark_float P((void));

/* things must be completely dead before using this.. use do_empty first. */
static void free_object(obj)
dbref obj;
{
  db[obj].next = first_free;
  first_free = obj;
}

#define CHECK_REF(thing) if((thing)<-3 ||  (thing)>=db_top || ((thing)>=-1 && IS_GONE(thing)))
/*#define CHECK_REF(a) if ((((a)>-1) && (db[a].flags & GOING)) || (a>=db_top) || (a<-3)) */
/* check for free list corruption */
#define NOT_OK(thing) ((db[thing].location!=NOTHING) || ((db[thing].owner!=1) && (db[thing].owner != root)) || ((db[thing].flags&~(0x8000))!=(TYPE_THING | GOING)))

/* return a cleaned up object off the free list or NOTHING */
dbref free_get()
{
  dbref newobj;

  if (first_free == NOTHING)
  {
    log_important("No first free, creating new.");
    return (NOTHING);
  }
  newobj = first_free;
  log_important(tprintf("First free is %ld", newobj));
  first_free = db[first_free].next;
  /* Make sure this object really should be in free list */
  if (NOT_OK(newobj))
  {
    static nrecur = 0;

    if (nrecur++ == 20)
    {
      first_free = NOTHING;
      report();
      log_error("Removed free list and continued");
      return (NOTHING);
    }
    report();
    log_error(tprintf("Object #%ld shouldn't free, fixing free list", newobj));
    fix_free_list();
    nrecur--;
    return (free_get());
  }
  /* free object name */
  SET(db[newobj].name, NULL);
  return (newobj);
}

static int object_cost(thing)
dbref thing;
{
  switch (Typeof(thing))
  {
  case TYPE_THING:
    return OBJECT_DEPOSIT(Pennies(thing));
  case TYPE_ROOM:
    return room_cost;
/*  case TYPE_CHANNEL:
   return channel_cost;
 */
  case TYPE_EXIT:
    if (db[thing].link != NOTHING)
      return exit_cost;
    else
      return exit_cost + link_cost;
  case TYPE_PLAYER:
#ifdef USE_UNIV
  case TYPE_UNIVERSE:
#endif
    return 1000;
  default:
    log_error(tprintf("Illegal object type: %ld, thing_cost", Typeof(thing)));
    return 5000;
  }
}

/* go through and rebuild the free list */
void fix_free_list()
{
  dbref thing;
  char *ch;

  first_free = NOTHING;
  /* destroy all rooms+make sure everything else is really dead */
  for (thing = 0; thing < db_top; thing++)
    if (IS_DOOMED(thing))
    {
      if ((atol(ch = atr_get(thing, A_DOOMSDAY)) < now) && (atol(ch) > 0))
	do_empty(thing);
    }
    else
      /* if something other than room make sure it is located in NOTHING
         otherwise undelete it, needed incase @tel used on object */
    if (NOT_OK(thing))
      db[thing].flags &= ~GOING;

  first_free = NOTHING;
  /* check for references to destroyed objects */
  for (thing = db_top - 1; thing >= 0; thing--)
    /* if object is alive make sure it doesn't refer to any dead objects */
    if (!IS_GONE(thing))
    {
      CHECK_REF(db[thing].exits)
	switch (Typeof(thing))
      {
      case TYPE_PLAYER:
      case TYPE_CHANNEL:
#ifdef USE_UNIV
      case TYPE_UNIVERSE:
#endif
      case TYPE_THING:
      case TYPE_ROOM:
	{			/* yuck probably corrupted set to nothing */
	  log_error(tprintf("Dead exit in exit list (first) for room #%ld: %ld", thing, db[thing].exits));
	  report();
	  db[thing].exits = NOTHING;
	}
      }
      CHECK_REF(db[thing].zone)
	switch (Typeof(thing))
      {
      case TYPE_ROOM:
	log_error(tprintf("Zone for #%ld is #%ld! setting it to the global zone.", thing, db[thing].zone));
	db[thing].zone = db[0].zone;
	break;
      }
      CHECK_REF(db[thing].link)
	switch (Typeof(thing))
      {
      case TYPE_PLAYER:
      case TYPE_CHANNEL:
#ifdef USE_UNIV
      case TYPE_UNIVERSE:
#endif
      case TYPE_THING:
	db[thing].link = player_start;
	break;
      case TYPE_EXIT:
      case TYPE_ROOM:
	db[thing].link = NOTHING;
	break;
      }
      CHECK_REF(db[thing].location)
	switch (Typeof(thing))
      {
      case TYPE_PLAYER:	/* this case shouldn't happen but just incase 

				 */
      case TYPE_CHANNEL:
#ifdef USE_UNIV
      case TYPE_UNIVERSE:
#endif
      case TYPE_THING:
	db[thing].location = NOTHING;
	moveto(thing, player_start);
	break;
      case TYPE_EXIT:
	db[thing].location = NOTHING;
	destroy_obj(thing, atol(bad_object_doomsday));
	break;
      case TYPE_ROOM:
	db[thing].location = thing;	/* rooms are in themselves */
	break;
      }
      if (((db[thing].next < 0) || (db[thing].next >= db_top)) && (db[thing].next != NOTHING))
      {
	log_error(tprintf("Invalid next pointer from object %s(%ld)", db[thing].name,
			  thing));
	report();
	db[thing].next = NOTHING;
      }
      if ((db[thing].owner < 0) || (db[thing].owner >= db_top) || Typeof(db[thing].owner) != TYPE_PLAYER)
      {
	log_error(tprintf("Invalid object owner %s(%ld): %ld", db[thing].name, thing, db[thing].owner));
	report();
	db[thing].owner = root;
	db[thing].flags |= HAVEN;
      }
    }
    else
      /* if object is dead stick in free list */
      free_object(thing);
  /* mark all rooms that can be reached from limbo */
  dbmark(player_start);
  mark_float();
  dbmark2();
  /* look through list and inform any player with an unconnected room */
  dbunmark();
}

/* Check data base for disconnected rooms */
static void dbmark(loc)
dbref loc;
{
  dbref thing;

  if ((loc < 0) || (loc >= db_top) || (db[loc].i_flags & I_MARKED) ||
      (Typeof(loc) != TYPE_ROOM))
    return;
  db[loc].i_flags |= I_MARKED;
  /* recursively trace */
  for (thing = Exits(loc); thing != NOTHING; thing = db[thing].next)
    dbmark(db[thing].link);
}

static void dbmark2()
{
  dbref loc;

  for (loc = 0; loc < db_top; loc++)
    if (Typeof(loc) == TYPE_PLAYER || Typeof(loc) == TYPE_CHANNEL
#ifdef USE_UNIV
	|| Typeof(loc) == TYPE_UNIVERSE
#endif
	|| Typeof(loc) == TYPE_THING)
    {
      if (db[loc].link != NOTHING)
	dbmark(db[loc].link);
      if (db[loc].location != NOTHING)
	dbmark(db[loc].location);
    }
/*    if (Typeof(loc) == TYPE_THING && Exits(loc) != NOTHING)
   for (thing = Exits(loc); thing != NOTHING; thing = db[thing].next)
   dbmark(db[thing].link); i don't get this. -koosh */
}

static void dbunmark()
{
  dbref loc;
  int ndisrooms = 0, nunlexits = 0;
  char *roomlist = NULL;
  char *exitlist = NULL;
  char *newbuf = NULL;

  for (loc = 0; loc < db_top; loc++)
  {
    if (db[loc].i_flags & I_MARKED)
      db[loc].i_flags &= ~I_MARKED;
    else if (Typeof(loc) == TYPE_ROOM)
    {
      ndisrooms++;
      if (roomlist)
        roomlist = tprintf("%s #%ld", roomlist, loc);
      else
        roomlist = tprintf("#%ld", loc);
      dest_info(NOTHING, loc);
    }
    if (Typeof(loc) == TYPE_EXIT && db[loc].link == NOTHING)
    {
      nunlexits++;
      if (exitlist)
        exitlist = tprintf("%s #%ld", exitlist, loc);
      else
        exitlist = tprintf("#%ld", loc);
    }
  }
  newbuf = tprintf("|Y!+*| There are %d disconnected rooms, %d unlinked exits.", ndisrooms, nunlexits);
  if (ndisrooms)
    newbuf = tprintf("%s Disconnected rooms:%s", newbuf, roomlist);
  if (nunlexits)
    newbuf = tprintf("%s Unlinked exits:%s", newbuf, exitlist); 

  com_send(dbinfo_chan, newbuf);
}

/* This is the original code for dbunmark, just for reference.

   static void dbunmark()
   {
   dbref loc;
   int ndisrooms=0;
   for(loc=0;loc<db_top;loc++)
   if (db[loc].i_flags & I_MARKED)
   db[loc].i_flags&=~I_MARKED;
   else
   if (Typeof(loc)==TYPE_ROOM) {
   ndisrooms++;
   dest_info(NOTHING,loc);
   }
   com_send(dbinfo_chan,tprintf("|Y!+*| There are %d disconnected rooms.",ndisrooms));
   }
 */

/* Check data base for disconnected objects */
static void dbmark1()
{
  dbref thing;
  dbref loc;

  for (loc = 0; loc < db_top; loc++)
    if (Typeof(loc) != TYPE_EXIT)
    {
      for (thing = db[loc].contents; thing != NOTHING; thing = db[thing].next)
      {
	if ((db[thing].location != loc) || (Typeof(thing) == TYPE_EXIT))
	{
	  log_error(tprintf("Contents of object %ld corrupt at object %ld cleared",
			    loc, thing));
	  db[loc].contents = NOTHING;
	  break;
	}
	db[thing].i_flags |= I_MARKED;
      }
      for (thing = db[loc].exits; thing != NOTHING; thing = db[thing].next)
      {
	if ((db[thing].location != loc) || (Typeof(thing) != TYPE_EXIT))
	{
	  log_error(tprintf("Exits of object %ld corrupt at object %ld. cleared.", loc, thing));
	  db[loc].exits = NOTHING;
	  break;
	}
	db[thing].i_flags |= I_MARKED;
      }
    }
}

static void dbunmark1()
{
  dbref loc;

  for (loc = 0; loc < db_top; loc++)
    if (db[loc].i_flags & I_MARKED)
      db[loc].i_flags &= ~I_MARKED;
    else if (!IS_GONE(loc))
      if (((Typeof(loc) == TYPE_PLAYER) || (Typeof(loc) == TYPE_CHANNEL)
#ifdef USE_UNIV
	   || (Typeof(loc) == TYPE_UNIVERSE)
#endif
	   || (Typeof(loc) == TYPE_THING)))
      {
	log_error(tprintf("DBCK: Moved object %ld", loc));
	if (db[loc].location > 0 && db[loc].location < db_top && Typeof(db[loc].location) != TYPE_EXIT)
	  moveto(loc, db[loc].location);
	else
	  moveto(loc, 0);
      }
      else if (Typeof(loc) == TYPE_EXIT)
      {
	log_error(tprintf("DBCK: moved exit %ld", loc));
	if (db[loc].location > 0 && db[loc].location < db_top && Typeof(db[loc].location) != TYPE_EXIT)
	  moveto(loc, db[loc].location);
	else
	  moveto(loc, 0);
      }
}

static void calc_memstats()
{
  int i;
  int j = 0;
  char *newbuf;

  for (i = 0; i < db_top; i++)
    j += mem_usage(i);

  newbuf = tprintf("|Y!+*| There are %d bytes being used in memory, total.", j);
  if (first_free)
    newbuf = tprintf("%s The first object in the free list is #%ld.", newbuf, first_free);

  com_send(dbinfo_chan, newbuf);
}

void do_dbck(player)
dbref player;
{
  extern dbref speaker;
  dbref i;

  speaker = root;
  for (i = 0; i < db_top; i++)
  {
    int m;
    dbref j;

    for (j = db[i].exits, m = 0; j != NOTHING; j = db[j].next, m++)
      if (m > 1000)
	db[j].next = NOTHING;
    for (j = db[i].contents, m = 0; j != NOTHING; j = db[j].next, m++)
      if (m > 1000)
	db[j].next = NOTHING;
  }
  if (!has_pow(player, NOTHING, POW_DB))
  {
    notify(player, "@dbck is a restricted command.");
    return;
  }
  fix_free_list();
  dbmark1();
  dbunmark1();
  calc_memstats();
}

/* send contents of destroyed object home+destroy exits */
/* all objects must be moved to nothing or otherwise unlinked first */
void do_empty(thing)
dbref thing;
{
  static int nrecur = 0;
  int i;
  ATRDEF *k, *next;

  if (nrecur++ > 20)
  {				/* if run away recursion return */
    report();
    log_error("Runaway recursion in do_empty");
    nrecur--;
    return;
  }
  while (boot_off(thing)) ;
  if (Typeof(thing) != TYPE_ROOM)
    moveto(thing, NOTHING);
  for (k = db[thing].atrdefs; k; k = next)
  {
    next = k->next;
    if (0 == --k->a.refcount)
    {
      SAFE_FREE(k->a.name);
      SAFE_FREE(k);
    }
  }
  db[thing].atrdefs = NULL;
  switch (Typeof(thing))
  {
  case TYPE_CHANNEL:
#ifdef USE_UNIV
  case TYPE_UNIVERSE:
    for (i = 0; i < NUM_UA; i++)
      SAFE_FREE(db[thing].ua_string[i]);
    SAFE_FREE(db[thing].ua_string);
    SAFE_FREE(db[thing].ua_float);
    SAFE_FREE(db[thing].ua_int);
#endif
  case TYPE_THING:
  case TYPE_PLAYER:
    moveto(thing, NOTHING);
  case TYPE_ROOM:
    {				/* if room destroy all exits out of it */
      dbref first;
      dbref rest;

      /* before we kill it tell people what is happening */
      if (Typeof(thing) == TYPE_ROOM)
	dest_info(thing, NOTHING);
      /* return owners deposit */
      db[thing].zone = NOTHING;
#ifdef USE_UNIV
      db[thing].universe = NOTHING;
#endif
      first = Exits(thing);
      /* Clear all exits out of exit list */
      while (first != NOTHING)
      {
	rest = db[first].next;
	if (Typeof(first) == TYPE_EXIT)
	  do_empty(first);
	first = rest;
      }
      first = db[thing].contents;
      /* send all objects to nowhere */
      DOLIST(rest, first)
      {
	if (db[rest].link == thing)
	{
	  db[rest].link = db[db[rest].owner].link;
	  if (db[rest].link == thing)
	    db[rest].link = 0;
	}
      }
      /* now send them home */
      while (first != NOTHING)
      {
	rest = db[first].next;
	/* if home is in thing set it to limbo */
	moveto(first, HOME);
	first = rest;
      }
    }
    break;
  }
  /* refund owner */
  if (!(db[db[thing].owner].flags & QUIET) && !power(db[thing].owner, POW_FREE))
    notify(db[thing].owner, tprintf
	   ("You get back your %d credit deposit for %s.",
	    object_cost(thing),
	    unparse_object(db[thing].owner, thing)));
  if (!power(db[thing].owner, POW_FREE))
    giveto(db[thing].owner, object_cost(thing));
  add_quota(db[thing].owner, 1);
  /* chomp chomp */
  atr_free(thing);
  db[thing].list = NULL;
  if (db[thing].pows)
  {
    SAFE_FREE(db[thing].pows);
    db[thing].pows = 0;
  }
  /* don't eat name otherwise examine will crash */
  s_Pennies(thing, (long)0);
  db[thing].owner = root;
  db[thing].flags = GOING | TYPE_THING;		/* toad it */
  db[thing].location = NOTHING;
  db[thing].link = NOTHING;
  for (i = 0; db[thing].children && db[thing].children[i] != NOTHING; i++)
    REMOVE_FIRST_L(db[db[thing].children[i]].parents, thing);
  if (db[thing].children)
    SAFE_FREE(db[thing].children);
  db[thing].children = NULL;
  for (i = 0; db[thing].parents && db[thing].parents[i] != NOTHING; i++)
    REMOVE_FIRST_L(db[db[thing].parents[i]].children, thing);
  if (db[thing].parents)
    SAFE_FREE(db[thing].parents);
  db[thing].parents = NULL;
  do_halt(thing, "", "");
  free_object(thing);
  nrecur--;
}

void do_undestroy(player, arg1)
dbref player;
char *arg1;
{
  dbref object;

  object = match_controlled(player, arg1, POW_EXAMINE);
  if (object == NOTHING)
    return;

  if (!(db[object].flags & GOING))
  {
    notify(player, tprintf("%s is not scheduled for destruction",
			   unparse_object(player, object)));
    return;
  }

  db[object].flags &= ~GOING;

  if (atol(atr_get(object, A_DOOMSDAY)) > 0)
  {
    atr_add(object, A_DOOMSDAY, "");
    notify(player, tprintf("%s has been saved from destruction.",
			   unparse_object(player, object)));
  }
  else
    notify(player, tprintf("%s is protected, and the GOING flag shouldn't have been set in the first place so what on earth happened?",
			   unparse_object(player, object)));
}

void zero_free_list()
{
  first_free = NOTHING;
}

static int gstate = 0;
static struct object *o;
static dbref thing;

void do_check(player, arg1)
dbref player;
char *arg1;
{
  dbref obj;

  if (!power(player, POW_SECURITY))
  {
    notify(player, perm_denied());
    return;
  }
  obj = match_controlled(player, arg1, POW_MODIFY);
  if (obj == NOTHING)
    return;
  thing = obj;
  gstate = 1;
  notify(player, "Okay, i set the garbage point.");
}

extern dbref update_bytes_counter;
void info_db(player)
dbref player;
{
  notify(player, tprintf("db_top: #%ld", db_top));
  notify(player, tprintf("first_free: #%ld", first_free));
  notify(player, tprintf("update_bytes_counter: #%ld", update_bytes_counter));
  notify(player, tprintf("garbage point: #%ld", thing));
  do_stats(player, "");
}

/* garbage collect the database */
void do_incremental()
{
  int j;
  int a;

  switch (gstate)
  {
  case 0:			/* pre collection need to age things first */
    /* fprintf(stderr,"Ageing\n"); */
    /* (fprintf(stderr,"agedone\n"); */
    gstate = 1;
    thing = 0;
    break;
  case 1:			/* into the copying stage */
    o = &(db[thing]);
    for (a = 0; (a < garbage_chunk) && (thing < db_top); a++, o++, thing++)
    {
      char buff[1024];
      extern char ccom[];
      int i;

      sprintf(ccom, "object #%ld\n", thing);
      if (thing == db_top)
      {
	gstate = 0;
	break;
      }

      strcpy(buff, o->name);
#ifdef MEMORY_DEBUG_LOG
      memdebug_log_ts( "GC: About to SET object #%ld name=%s ptr=%p\n",
            thing, o->name, (void*)o->name);
#endif
      SET(o->name, buff);

      atr_collect(thing);

      if (!IS_GONE(thing))
      {
	ALIST *atr, *nxt;

      again1:
	for (i = 0; db[thing].parents && db[thing].parents[i] != NOTHING; i++)
	{
	  CHECK_REF(db[thing].parents[i])
	  {
	    log_error(tprintf("Bad #%ld in parent list on #%ld.", db[thing].parents[i], thing));
	    REMOVE_FIRST_L(db[thing].parents, db[thing].parents[i]);
	    goto again1;
	  }
	  for (j = 0; db[db[thing].parents[i]].children &&
	       db[db[thing].parents[i]].children[j] != NOTHING; j++)
	    if (db[db[thing].parents[i]].children[j] == thing)
	    {
	      j = -1;
	      break;
	    }
	  if (j != -1)
	  {
	    log_error(tprintf("Wrong #%ld in parent list on #%ld.", db[thing].parents[i], thing));
	    REMOVE_FIRST_L(db[thing].parents, db[thing].parents[i]);
	    goto again1;
	  }
	}
      again2:
	for (i = 0; db[thing].children && db[thing].children[i] != NOTHING; i++)
	{
	  CHECK_REF(db[thing].children[i])
	  {
	    log_error(tprintf("Bad #%ld in children list on #%ld.", db[thing].children[i], thing));
	    REMOVE_FIRST_L(db[thing].children, db[thing].children[i]);
	    goto again2;	/* bad programming style, but it's easiest. */
	  }
	  for (j = 0; db[db[thing].children[i]].parents &&
	       db[db[thing].children[i]].parents[j] != NOTHING; j++)
	    if (db[db[thing].children[i]].parents[j] == thing)
	    {
	      j = -1;
	      break;
	    }
	  if (j != -1)
	  {
	    log_error(tprintf("Wrong #%ld in children list on #%ld.", db[thing].children[i], thing));
	    REMOVE_FIRST_L(db[thing].children, db[thing].children[i]);
	    goto again1;
	  }
	}
	for (atr = db[thing].list; atr; atr = nxt)
	{
	  nxt = AL_NEXT(atr);
	  if (AL_TYPE(atr) && AL_TYPE(atr)->obj != NOTHING
	      && !is_a(thing, AL_TYPE(atr)->obj))
	    atr_add(thing, AL_TYPE(atr), "");
	}
	{
	  dbref zon;

	  for (dozonetemp = 0, zon = get_zone_first(thing); zon != NOTHING; zon = get_zone_next(zon), dozonetemp++)
	    if (dozonetemp > 15)
	    {			/* ack. inf loop. */
	      log_error(tprintf("%s's zone %s is infinite.", unparse_object_a(1, thing), unparse_object_a(1, zon)));
	      db[zon].zone = db[0].zone;
	      db[db[0].zone].zone = NOTHING;
	    }
	}

	CHECK_REF(db[thing].exits)
	  switch (Typeof(thing))
	{
	case TYPE_PLAYER:
	case TYPE_THING:
	case TYPE_CHANNEL:
#ifdef USE_UNIV
	case TYPE_UNIVERSE:
#endif
	case TYPE_ROOM:
	  {			/* yuck probably corrupted set to nothing */
	    log_error(tprintf("Dead exit in exit list (first) for room #%ld: %ld", thing, db[thing].exits));
	    report();
	    db[thing].exits = NOTHING;
	  }
	}
	CHECK_REF(db[thing].zone)
	  switch (Typeof(thing))
	{
	case TYPE_ROOM:
	  log_error(tprintf("Zone for #%ld is #%ld! setting it to the global zone.", thing, db[thing].zone));
	  db[thing].zone = db[0].zone;
	  break;
	}
	CHECK_REF(db[thing].link)
	  switch (Typeof(thing))
	{
	case TYPE_PLAYER:
	case TYPE_THING:
	case TYPE_CHANNEL:
#ifdef USE_UNIV
	case TYPE_UNIVERSE:
#endif
	  db[thing].link = player_start;
	  break;
	case TYPE_EXIT:
	case TYPE_ROOM:
	  db[thing].link = NOTHING;
	  break;
	}
	CHECK_REF(db[thing].location)
	  switch (Typeof(thing))
	{
	case TYPE_PLAYER:	/* this case shouldn't happen but just incase 

				 */
	case TYPE_THING:
	case TYPE_CHANNEL:
#ifdef USE_UNIV
	case TYPE_UNIVERSE:
#endif
	  db[thing].location = NOTHING;
	  moveto(thing, player_start);
	  break;
	case TYPE_EXIT:
	  db[thing].location = NOTHING;
	  destroy_obj(thing, atol(bad_object_doomsday));
	  break;
	case TYPE_ROOM:
	  db[thing].location = thing;	/* rooms are in themselves */
	  break;
	}
	if (((db[thing].next < 0) || (db[thing].next >= db_top)) && (db[thing].next != NOTHING))
	{
	  log_error(tprintf("Invalid next pointer from object %s(%ld)", db[thing].name,
			    thing));
	  report();
	  db[thing].next = NOTHING;
	}
	if ((db[thing].owner < 0) || (db[thing].owner >= db_top) || Typeof(db[thing].owner) != TYPE_PLAYER)
	{
	  log_error(tprintf("Invalid object owner %s(%ld): %ld", db[thing].name, thing, db[thing].owner));
	  report();
	  db[thing].owner = root;
	}
      }
      if (!*atr_get(o->owner, A_BYTESUSED))
	recalc_bytes(o->owner);

    }
    /* if complete go to state 0 */
    if (thing == db_top)
      gstate = 0;
    break;
  }
}

/*  Find and mark all floating rooms.  */
static void mark_float()
{
  dbref loc;

  for (loc = 0; loc < db_top; loc++)
    if (IS(loc, TYPE_ROOM, ROOM_FLOATING))
      dbmark(loc);
}

/* yay! workie! */
void do_upfront(player, arg1)
dbref player;
char *arg1;
{
  dbref object;
  dbref thing;

  if (!power(player, POW_DB))
  {
    notify(player, "Restricted command.");
    return;
  }

  if ((thing = match_thing(player, arg1)) == NOTHING)
    return;
  if (first_free == thing)
  {
    notify(player, "That object is already at the top of the free list.");
    return;
  }
  for (object = first_free; object != NOTHING && db[object].next != thing;
       object = db[object].next) ;
  if (object == NOTHING)
  {
    notify(player, "That object does not exist in the free list.");
    return;
  }
  db[object].next = db[thing].next;
  db[thing].next = first_free;
  first_free = thing;
  notify(player, "Object is now at the front of the free list.");
}

#ifdef SHRINK_DB


/*
 * warning: this function is... touchy.   MUSE doesn't like it when 
 *  the database changes radically between dumps.  Why?  I don't know.
 *  but I'd do this on 100 objects at a time, then @dump, and @reboot,
 *  then do another 100 objects.  Hey - I'd like it to work right, but
 *  the again, how often do you really need to shrink the db?
 *
 * Also: This moves players around, so it SEVERELY messes with the +mail
 *  system's head.  You should delete all +mail before shrinking the DB
 *  until I get a chance to fix @swap to clean up +mail too, this is 
 *  going to be the case.  -- wm
 */
void do_shrinkdbuse(player, arg1)
dbref player;
char *arg1;
{

  dbref vari = 0;
  dbref vari2 = 0;
  int my_exit = 0;
  char temp[5];
  char temp2[5];
  dbref distance;

  vari = 0;
  vari2 = 0;

  if (!arg1)
    return;

  distance = atol(arg1);

  if (distance == 0)
  {
/*    remove_all_mail();  */
    notify(player, tprintf("db_top: %ld", db_top));
  }
  else
  {

/*  for (vari = db_top - 1;vari > 273; vari --) */
    for (vari = db_top - 1; vari > distance; vari--)
      if (!(db[vari].flags & GOING))
      {
	for (my_exit = 0; my_exit != 1;)
	{
	  if ((db[vari2].flags & GOING) || (vari2 > vari))
	    my_exit = 1;
	  else
	    vari2++;
	}

	if (vari2 > 0)
	  if (vari > vari2)
	  {
	    notify(player, tprintf("Found one: %ld  Free: %ld", vari, vari2));
	    sprintf(temp, tprintf("#%ld", vari));
	    sprintf(temp2, tprintf("#%ld", vari2));
	    do_swap(root, temp, temp2);
	  }
      }
  }
}

#endif
