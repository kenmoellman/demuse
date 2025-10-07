/* look.c */
/* $Id: look.c,v 1.18 1993/09/18 19:03:42 nils Exp $ */

/* commands which look at things */

#include "db.h"
#include "config.h"
#include "interface.h"
#include "match.h"
#include "externs.h"

char *eval_sweep P((dbref));

static void look_exits(player, loc, exit_name)
dbref player;
dbref loc;
char *exit_name;
{
  dbref thing;
  char buff[1024];
  char *e = buff;
  int flag = 0;

  /* make sure location is a room */
  /* if(db[loc].flags & DARK) return; */
  for (thing = Exits(loc); (thing != NOTHING) && (Dark(thing)); thing = db[thing].next) ;
  if (thing == NOTHING)
    return;
/*  notify(player,exit_name); */
  for (thing = Exits(loc); thing != NOTHING; thing = db[thing].next)
    if (!(Dark(loc)) ||
	(IS(thing, TYPE_EXIT, EXIT_LIGHT) && controls(thing, loc,
						      POW_MODIFY)))
      if (!(Dark(thing)))
	/* chop off first exit alias to display */
      {
	char *s;

	if (!flag)
	{
	  notify(player, exit_name);
	  flag++;
	}
	if (db[thing].cname && ((e - buff) < 1000))
	{
	  for (s = db[thing].cname; *s && (*s != ';') && (e - buff < 1000); *e++ = *s++) ;
	  *e++ = ' ';
	  *e++ = ' ';
	}
      }
  *e++ = 0;
  if (*buff)
    notify(player, buff);
}

static void look_contents(player, loc, contents_name)
dbref player;
dbref loc;
char *contents_name;
{
  dbref thing;
  dbref can_see_loc;

  /* check to see if he can see the location */
  /* patched so that player can't see in dark rooms even if owned by that
     player.  (he must use examine command) */
  can_see_loc = !Dark(loc);

  /* check to see if there is anything there */
  DOLIST(thing, db[loc].contents)
  {
    if (can_see(player, thing, can_see_loc))
    {
      /* something exists!  show him everything */
      notify(player, contents_name);
      DOLIST(thing, db[loc].contents)
      {
	if (can_see(player, thing, can_see_loc))
	{
	  notify(player, unparse_object_caption(player, thing));
	}
      }
      break;			/* we're done */
    }
  }
}

/* static struct all_atr_list *all_attributes_internal(thing, myop, myend, dep) */
struct all_atr_list *all_attributes_internal(thing, myop, myend, dep)
dbref thing;
struct all_atr_list *myop;
struct all_atr_list *myend;
int dep;
{
  ALIST *k;
  struct all_atr_list *tmp;
  int i;

  for (k = db[thing].list; k; k = AL_NEXT(k))
    if (AL_TYPE(k) && ((dep == 0) || (AL_TYPE(k)->flags & AF_INHERIT)))
    {
      for (tmp = myop; tmp; tmp = tmp->next)
	if (tmp->type == AL_TYPE(k))
	  break;
      if (tmp)
	continue;		/* there's already something with this type. */
      if (!myop)
      {
	myop = myend = stack_em(sizeof(struct all_atr_list));

	myop->next = NULL;
      }
      else
      {
	struct all_atr_list *foo;
	foo = stack_em(sizeof(struct all_atr_list));

	foo->next = myop;
	myop = foo;
      }
      myop->type = AL_TYPE(k);
      myop->value = AL_STR(k);
      myop->numinherit = dep;
    }
  for (i = 0; db[thing].parents && db[thing].parents[i] != NOTHING; i++)
  {
    if (myend)
      myop = all_attributes_internal(db[thing].parents[i], myop, myend, dep + 1);
    else
      myop = all_attributes_internal(db[thing].parents[i], 0, 0, dep + 1);
    while (myend && myend->next)
      myend = myend->next;
  }
  return myop;
}

struct all_atr_list *all_attributes(thing)
dbref thing;
{
  return all_attributes_internal(thing, 0, 0, 0);	/* start with 0 depth 

							 */
}

char *unparse_list(player, list)
dbref player;
char *list;
{
  char buf[1000];
  int pos = 0;

  while (pos < 9990 && *list)
  {
    if (*list == '#' && list[1] >= '0' && list[1] <= '9')
    {
      int y = atoi(list + 1);
      char *x;

      if (y >= HOME || y < db_top)
      {
	x = unparse_object(player, y);
	if (strlen(x) + pos < 9900)
	{
	  buf[pos] = ' ';
	  strcpy(buf + pos + 1, x);
	  pos += strlen(buf + pos);
	  list++;		/* skip the # */
	  while (*list >= '0' && *list <= '9')
	    list++;
	  continue;
	}
      }
    }
    buf[pos++] = *list++;
  }
  buf[pos] = '\0';
  return (*buf) ? stralloc(buf + 1) : stralloc(buf);
}

static void look_atr(player, allatrs)
dbref player;
struct all_atr_list *allatrs;
{
  long cl;
  ATTR *attr;

  attr = allatrs->type;
  if (attr->flags & AF_DATE)
  {
    cl = atol((allatrs->value));
    notify(player, tprintf("%s:%s", unparse_attr(attr, allatrs->numinherit),
			   mktm(cl, "D", player)));
  }
  else if (attr->flags & AF_LOCK)
    notify(player,
	   tprintf("%s:%s", unparse_attr(attr, allatrs->numinherit),
		   unprocess_lock(player, stralloc(allatrs->value))));
  else if (attr->flags & AF_FUNC)
    notify_noc(player,
	       tprintf("%s():%s", unparse_attr(attr, allatrs->numinherit),
		       (allatrs->value)));
  else if (attr->flags & AF_DBREF)
    notify(player,
	   tprintf("%s:%s", unparse_attr(attr, allatrs->numinherit),
		   unparse_list(player, (allatrs->value))));
  else if (attr->flags & AF_TIME)
    notify(player,
	   tprintf("%s:%s", unparse_attr(attr, allatrs->numinherit),
		   time_format_4(atol((allatrs->value)))));
  else
    notify_noc(player,
	       tprintf("%s:%s", unparse_attr(attr, allatrs->numinherit),
		       (allatrs->value)));
}

static void look_atrs(player, thing, doall)
dbref player;
dbref thing;
int doall;
{
  struct all_atr_list *allatrs;
  ATTR *attr;

  for (allatrs = all_attributes(thing); allatrs; allatrs = allatrs->next)
  {
    if ((allatrs->type != A_DESC) &&
	(attr = allatrs->type) &&
	(allatrs->numinherit == 0 || !(db[allatrs->type->obj].flags & SEE_OK) || doall) &&
	can_see_atr(player, thing, attr))
      look_atr(player, allatrs);
  }
}

static void look_simple(player, thing, doatrs)
dbref player;
dbref thing;
int doatrs;
{
  if (controls(player, thing, POW_EXAMINE) || (db[thing].flags & SEE_OK))
    notify(player, unparse_object_caption(player, thing));
  /* if(*Desc(thing)) { notify(player, Desc(thing)); } else { notify(player,
     "You see nothing special."); } */
  if (doatrs)
    did_it(player, thing, A_DESC, "You see nothing special.", A_ODESC, NULL, A_ADESC);
  else
    did_it(player, thing, A_DESC, "You see nothing special.", NULL, NULL, NULL);
  if (Typeof(thing) == TYPE_EXIT)
    if (db[thing].flags & OPAQUE)
      if (db[thing].link != NOTHING)
      {
	notify(player, tprintf("You peer through to %s...", db[db[thing].link].name));
	did_it(player, db[thing].link, A_DESC, "You see nothing on the other side.", doatrs ? A_ODESC : NULL, NULL, doatrs ? A_ADESC : NULL);
	look_contents(player, db[thing].link, "You also notice:");
      }
}

void look_room(player, loc)
dbref player;
dbref loc;
{
  char *s;

  /* tell him the name, and the number if he can link to it */
  notify(player, unparse_object_caption(player, loc));
  if (Typeof(loc) != TYPE_ROOM)
  {
    did_it(player, loc, A_IDESC, NULL, A_OIDESC, NULL, A_AIDESC);
  }
  /* tell him the description */
  /* if(*Desc(loc)) notify(player, Desc(loc)); */
  else
  {
    if (!(db[player].flags & PLAYER_TERSE))
    {
      if (*(s = atr_get(get_zone_first(player), A_IDESC)) && !(db[loc].flags & OPAQUE))
	notify(player, s);
      did_it(player, loc, A_DESC, NULL, A_ODESC, NULL, A_ADESC);
    }
  }
  /* tell him the appropriate messages if he has the key */
  if (Typeof(loc) == TYPE_ROOM)
  {
    if (could_doit(player, loc, A_LOCK))
      did_it(player, loc, A_SUCC, NULL, A_OSUCC, NULL, A_ASUCC);
    else
      did_it(player, loc, A_FAIL, NULL, A_OFAIL, NULL, A_AFAIL);
  }

  /* tell him the contents */
  look_contents(player, loc, "Contents:");
  look_exits(player, loc, "Obvious exits:");
}

void do_look_around(player)
dbref player;
{
  dbref loc;

  if ((loc = getloc(player)) == NOTHING)
    return;
  look_room(player, loc);
}

void do_look_at(player, arg1)
dbref player;
char *arg1;
{
  dbref thing, thing2;
  char *s, *p;
  char *name = arg1;		/* so we don't mangle it */

  if (*name == '\0')
  {
    if ((thing = getloc(player)) != NOTHING)
    {
      look_room(player, thing);
    }
  }
  else
  {
    /* look at a thing here */
    init_match(player, name, NOTYPE);
    match_exit();
    match_neighbor();
    match_possession();
    if (power(player, POW_REMOTE))
    {
      match_absolute();
      match_player(NOTHING, NULL);
    }
    match_here();
    match_me();

    switch (thing = match_result())
    {
    case NOTHING:
      for (s = name; *s && *s != ' '; s++) ;
      if (!*s)
      {
	notify(player, tprintf(NOMATCH_PATT, arg1));
	return;
      }
      p = name;
      if ((*(s - 1) == 's' && *(s - 2) == '\'' && *(s - 3) != 's') ||
	  (*(s - 1) == '\'' && *(s - 2) == 's'))
      {

	if (*(s - 1) == 's')
	  *(s - 2) = '\0';
	else
	  *(s - 1) = '\0';
	name = s + 1;
	init_match(player, p, TYPE_PLAYER);
	match_neighbor();
	match_possession();
	switch (thing = match_result())
	{
	case NOTHING:
	  notify(player, tprintf(NOMATCH_PATT, arg1));
	  break;
	case AMBIGUOUS:
	  notify(player, AMBIGUOUS_MESSAGE);
	  break;
	default:
	  init_match(thing, name, TYPE_THING);
	  match_possession();
	  switch (thing2 = match_result())
	  {
	  case NOTHING:
	    notify(player, tprintf(NOMATCH_PATT, arg1));
	    break;
	  case AMBIGUOUS:
	    notify(player, AMBIGUOUS_MESSAGE);
	    break;
	  default:
	    if ((db[thing].flags & OPAQUE)
		&& !power(player, POW_EXAMINE))
	    {
	      notify(player, tprintf(NOMATCH_PATT, name));
	    }
	    else
	    {
	      look_simple(player, thing2, 0);
	    }
	  }
	}
      }
      else
	notify(player, tprintf(NOMATCH_PATT, arg1));
      break;
    case AMBIGUOUS:
      notify(player, AMBIGUOUS_MESSAGE);
      break;
    default:
      switch (Typeof(thing))
      {
      case TYPE_ROOM:
	look_room(player, thing);
	break;
      case TYPE_THING:
      case TYPE_PLAYER:
      case TYPE_CHANNEL:
#ifdef USE_UNIV
      case TYPE_UNIVERSE:
#endif
	look_simple(player, thing, 1);
	/* look_atrs(player,thing); */
	if (controls(player, thing, POW_EXAMINE) ||
	    !(db[thing].flags & OPAQUE) ||
	    power(player, POW_EXAMINE))
	{
	  look_contents(player, thing, "Carrying:");
	}
	break;
      default:
	look_simple(player, thing, 1);
	/* look_atrs(player,thing); */
	break;
      }
    }
  }
}

char *flag_description(thing)
dbref thing;
{
  char buf[BUFFER_LEN];

  strcpy(buf, "Type:");
  switch (Typeof(thing))
  {
  case TYPE_ROOM:
    strcat(buf, "Room");
    break;
  case TYPE_EXIT:
    strcat(buf, "Exit");
    break;
  case TYPE_THING:
    strcat(buf, "Thing");
    break;
  case TYPE_CHANNEL:
    strcat(buf, "Channel");
    break;
#ifdef USE_UNIV
  case TYPE_UNIVERSE:
    strcat(buf, "Universe");
    break;
#endif
  case TYPE_PLAYER:
    strcat(buf, "Player");
    break;
  default:
    strcat(buf, "***UNKNOWN TYPE***");
    break;
  }

  if (db[thing].flags & ~TYPE_MASK)
  {
    /* print flags */
    strcat(buf, "      Flags:");
    if (db[thing].flags & GOING)
      strcat(buf, " Going");
    if (db[thing].flags & PUPPET)
      strcat(buf, " Puppet");
    if (db[thing].flags & STICKY)
      strcat(buf, " Sticky");
    if (db[thing].flags & DARK)
      strcat(buf, " Dark");
    if (db[thing].flags & LINK_OK)
      strcat(buf, " Link_ok");
    if (db[thing].flags & HAVEN)
      strcat(buf, " Haven");
    if (db[thing].flags & CHOWN_OK)
      strcat(buf, " Chown_ok");
    if (db[thing].flags & ENTER_OK)
      strcat(buf, " Enter_ok");
    if (db[thing].flags & SEE_OK)
      strcat(buf, " Visible");
    if (db[thing].flags & OPAQUE)
      strcat(buf, (Typeof(thing) == TYPE_EXIT) ? " Transparent" : " Opaque");
    if (db[thing].flags & INHERIT_POWERS)
      strcat(buf, " Inherit");
    if (db[thing].flags & QUIET)
      strcat(buf, " Quiet");
    if (db[thing].flags & BEARING)
      strcat(buf, " Bearing");
    if (db[thing].flags & CONNECT)
      strcat(buf, " Connected");
    switch (Typeof(thing))
    {
    case TYPE_PLAYER:
      if (db[thing].flags & PLAYER_SLAVE)
	strcat(buf, " Slave");
      if (db[thing].flags & PLAYER_TERSE)
	strcat(buf, " Terse");
      if (db[thing].flags & PLAYER_MORTAL)
	strcat(buf, " Mortal");
      if (db[thing].flags & PLAYER_NO_WALLS)
	strcat(buf, " No_walls");
      if (db[thing].flags & PLAYER_ANSI)
	strcat(buf, " ANSI");
      if (db[thing].flags & PLAYER_NOBEEP)
	strcat(buf, " NoBeep");
/*
      if (db[thing].flags & PLAYER_WHEN)
	strcat(buf, " When");
*/
      if (db[thing].flags & PLAYER_FREEZE)
	strcat(buf, " Freeze");
/*
      if (db[thing].flags & PLAYER_SUSPECT)
	  strcat(buf, " Suspect");
*/
      break;
    case TYPE_EXIT:
      if (db[thing].flags & EXIT_LIGHT)
	strcat(buf, " Light");
      break;
    case TYPE_THING:
      if (db[thing].flags & THING_KEY)
	strcat(buf, " Key");
      if (db[thing].flags & THING_DEST_OK)
	strcat(buf, " Destroy_ok");
      if (db[thing].flags & THING_SACROK)
	strcat(buf, " X_ok");
      if (db[thing].flags & THING_LIGHT)
	strcat(buf, " Light");
      break;
    case TYPE_ROOM:
      if (db[thing].flags & ROOM_JUMP_OK)
	strcat(buf, " Jump_ok");
      if (db[thing].flags & ROOM_AUDITORIUM)
	strcat(buf, " Auditorium");
      if (db[thing].flags & ROOM_FLOATING)
	strcat(buf, " Floating");
      break;
    }
    if (db[thing].i_flags & I_MARKED)
      strcat(buf, " Marked");
    if (db[thing].i_flags & I_QUOTAFULL)
      strcat(buf, " Quotafull");
    if (db[thing].i_flags & I_UPDATEBYTES)
      strcat(buf, " Updatebytes");
  }
  return stralloc(buf);
}
void do_examine(player, name, arg2)
dbref player;
char *name;
char *arg2;
{
  dbref thing;
  dbref content;
  dbref exit;
  dbref enter;
  char *rq, *rqm, *cr, *crm, buf[10];
  int pos = 0;
  struct all_atr_list attr_entry;

  if (*name == '\0')
  {
    if ((thing = getloc(player)) == NOTHING)
      return;
  }
  else
  {
    if (strchr(name, '/'))
    {
      if (!parse_attrib(player, name, &thing, &(attr_entry.type), 0))
	notify(player, "No match.");
      else if (!can_see_atr(player, thing, attr_entry.type))
      {
	notify(player, perm_denied());
      }
      else
      {
	attr_entry.value = atr_get(thing, attr_entry.type);
	attr_entry.numinherit = 0;	/* could find this, prob'ly not worth 

					   it */
	look_atr(player, &attr_entry);
      }
      return;
    }
    /* look it up */
    init_match(player, name, NOTYPE);
    match_exit();
    match_neighbor();
    match_possession();
    match_absolute();
    /* only Wizards can examine other players */
    if (has_pow(player, NOTHING, POW_EXAMINE) || has_pow(player, NOTHING, POW_REMOTE))
      match_player(NOTHING, NULL);
    /* if(db[thing].flags & UNIVERSAL) match_absolute(); */
    match_here();
    match_me();

    /* get result */
    if ((thing = noisy_match_result()) == NOTHING)
      return;
  }

  if ((!can_link(player, thing, POW_EXAMINE) &&
       !(db[thing].flags & SEE_OK)))
  {
    char buf2[BUFFER_LEN];

    strcpy(buf2, unparse_object(player, thing));
    notify(player, tprintf("%s is owned by %s",
			   buf2, unparse_object(player, db[thing].owner)));
    look_atrs(player, thing, *arg2);	/* only show osee attributes */
    /* check to see if there is anything there */
    /* as long as he owns what is there */
    DOLIST(content, db[thing].contents)
    {
      if (can_link(player, content, POW_EXAMINE))
      {
	notify(player, "Contents:");
	DOLIST(content, db[thing].contents)
	{
	  if (can_link(player, content, POW_EXAMINE))
	  {
	    notify(player, unparse_object(player, content));
	  }
	}
	break;			/* we're done */
      }
    }
    return;
  }
  notify(player, unparse_object_caption(player, thing));
  if (*Desc(thing) && can_see_atr(player, thing, A_DESC))
    notify(player, Desc(thing));

  rq = rqm = "";
  sprintf(buf, "%ld", Pennies(thing));
  cr = buf;
  crm = "  Credits: ";
  if (Typeof(thing) == TYPE_PLAYER)
  {
    if (Robot(thing))
      cr = crm = "";
    else
    {
      if (inf_mon(thing))
	cr = "INFINITE";
      rqm = "  Quota-Left: ";
      if (inf_quota(thing))
	rq = "INFINITE";
      else
      {
	rq = atr_get(thing, A_RQUOTA);
	if (atol(rq) <= 0)
	  rq = "NONE";
      }
    }
  }
  notify(player, tprintf("Owner:%s%s%s%s%s",
			 db[db[thing].owner].cname, crm, cr, rqm, rq));
  notify(player, flag_description(thing));
  if (db[thing].zone != NOTHING)
    notify(player, tprintf("Zone:%s",
			   unparse_object(player, db[thing].zone)));
#ifdef USE_UNIV
  if (db[thing].universe != NOTHING)
    notify(player, tprintf("Universe:%s",
			   unparse_object(player, db[thing].universe)));
#endif
  notify(player, tprintf("Created:%s",
			 db[thing].create_time ?
		       mktm(db[thing].create_time, "D", player) : "never"));
  notify(player, tprintf("Modified:%s",
			 db[thing].mod_time ?
			 mktm(db[thing].mod_time, "D", player) : "never"));
  if (db[thing].parents && *db[thing].parents != NOTHING)
  {
    char obuf[1000], buf[1000];
    int i;

    strcpy(obuf, "Parents:");
    for (i = 0; db[thing].parents[i] != NOTHING; i++)
    {
      sprintf(buf, " %s", unparse_object(player, db[thing].parents[i]));
      if (strlen(buf) + strlen(obuf) > 900)
      {
	notify(player, obuf);
	strcpy(obuf, buf + 1);
      }
      else
	strcat(obuf, buf);
    }
    notify(player, obuf);
  }
  if (db[thing].atrdefs)
  {
    ATRDEF *k;

    notify(player, "Attribute definitions:");
    for (k = db[thing].atrdefs; k; k = k->next)
    {
      char buf[1000];

      sprintf(buf, "  %s%s%c", k->a.name, (k->a.flags & AF_FUNC) ? "()" : "",
	      (k->a.flags) ? ':' : '\0');
      if (k->a.flags & AF_WIZARD)
	strcat(buf, " Wizard");
      if (k->a.flags & AF_UNIMP)
	strcat(buf, " Unsaved");
      if (k->a.flags & AF_OSEE)
	strcat(buf, " Osee");
      if (k->a.flags & AF_INHERIT)
	strcat(buf, " Inherit");
      if (k->a.flags & AF_DARK)
	strcat(buf, " Dark");
      if (k->a.flags & AF_DATE)
	strcat(buf, " Date");
      if (k->a.flags & AF_LOCK)
	strcat(buf, " Lock");
      if (k->a.flags & AF_FUNC)
	strcat(buf, " Function");
      if (k->a.flags & AF_DBREF)
	strcat(buf, " Dbref");
      if (k->a.flags & AF_HAVEN)
	strcat(buf, " Haven");
      notify(player, buf);
    }
  }
  look_atrs(player, thing, *arg2);

  /* show him the contents */
  if (db[thing].contents != NOTHING)
  {
    notify(player, "Contents:");
    DOLIST(content, db[thing].contents)
    {
      notify(player, unparse_object(player, content));
    }
  }
  fflush(stdout);
  switch (Typeof(thing))
  {
  case TYPE_ROOM:
    /* tell him about entrances */
    for (enter = 0; enter < db_top; enter++)
      if (Typeof(enter) == TYPE_EXIT && db[enter].link == thing)
      {
	if (pos == 0)
	  notify(player, "Entrances:");
	pos = 1;
	notify(player, unparse_object(player, enter));
      }
    if (pos == 0)
      notify(player, "No Entrances.");

    /* tell him about exits */
    if (Exits(thing) != NOTHING)
    {
      notify(player, "Exits:");
      DOLIST(exit, Exits(thing))
      {
	notify(player, unparse_object(player, exit));
      }
    }
    else
    {
      notify(player, "No exits.");
    }

    /* print dropto if present */
    if (db[thing].link != NOTHING)
    {
      notify(player,
	     tprintf("Dropped objects go to:%s",
		     unparse_object(player, db[thing].link)));
    }
    break;
  case TYPE_THING:
  case TYPE_PLAYER:
  case TYPE_CHANNEL:
#ifdef USE_UNIV
  case TYPE_UNIVERSE:
#endif
    /* print home */
    notify(player,
	   tprintf("Home:%s",
		   unparse_object(player, db[thing].link)));	/* home */
    /* print location if player can link to it */
    if (db[thing].location != NOTHING
	&& (controls(player, db[thing].location, POW_EXAMINE) ||
	    controls(player, thing, POW_EXAMINE)
	    || can_link_to(player, db[thing].location, POW_EXAMINE)))
    {
      notify(player,
	     tprintf("Location:%s",
		     unparse_object(player, db[thing].location)));
    }
    if (Typeof(thing) == TYPE_THING)
    {
      for (enter = 0; enter < db_top; enter++)
	if (Typeof(enter) == TYPE_EXIT && db[enter].link == thing)
	{
	  if (pos == 0)
	    notify(player, "Entrances:");
	  pos = 1;
	  notify(player, unparse_object(player, enter));
	}
    }
    if (Typeof(thing) == TYPE_THING && db[thing].exits != NOTHING)
    {
      notify(player, "Exits:");
      DOLIST(exit, db[thing].exits)
      {
	notify(player, unparse_object(player, exit));
      }
    }
    break;
  case TYPE_EXIT:
    /* Print source */
    notify(player, tprintf("Source:%s",
			   unparse_object(player, db[thing].location)));
    /* print destination */
    switch (db[thing].link)
    {
    case NOTHING:
      break;
    case HOME:
      notify(player, "Destination:*HOME*");
      break;
    default:
      notify(player,
	     tprintf("Destination:%s",
		     unparse_object(player, db[thing].link)));
      break;
    }
    break;
  default:
    /* do nothing */
    break;
  }
}

void do_score(player)
dbref player;
{

  notify(player,
	 tprintf("You have %ld %s.",
		 Pennies(player),
		 Pennies(player) == 1 ? "Credit" : "Credits"));
}

void do_inventory(player)
dbref player;
{
  dbref thing;

  if ((thing = db[player].contents) == NOTHING)
  {
    notify(player, "You aren't carrying anything.");
  }
  else
  {
    notify(player, "You are carrying:");
    DOLIST(thing, thing)
    {
      notify(player, unparse_object(player, thing));
    }
  }

  do_score(player);
}

void do_find(player, name)
dbref player;
char *name;
{
  dbref i;

  if (!payfor(player, find_cost))
  {
    notify(player, "You don't have enough Credits.");
  }
  else
  {
    for (i = 0; i < db_top; i++)
    {
      if (Typeof(i) != TYPE_EXIT
	  && (power(player, POW_EXAMINE) || db[i].owner == db[player].owner)
	  && (!*name || string_match(db[i].name, name)))
      {
	notify(player, unparse_object(player, i));
      }
    }
    notify(player, "***End of List***");
  }
}

static void print_atr_match(thing, player, str)
dbref thing;
dbref player;
char *str;
{
  struct all_atr_list *ptr;

  for (ptr = all_attributes(thing); ptr; ptr = ptr->next)
    if ((ptr->type != 0) && !(ptr->type->flags & AF_LOCK) &&
	((*ptr->value == '!') || (*ptr->value == '$')))
    {
      /* decode it */
      char buff[1024];
      char *s;

      strncpy(buff, (ptr->value), 1024);
      /* search for first un escaped : */
      for (s = buff + 1; *s && (*s != ':'); s++) ;
      if (!*s)
	continue;
      *s++ = 0;
      if (wild_match(buff + 1, str))
      {
	if (controls(player, thing, POW_SEEATR))
	  notify(player, tprintf(" %s/%s: %s", unparse_object(player, thing), unparse_attr(ptr->type, ptr->numinherit), buff + 1));
	else
	  notify(player, tprintf(" %s", unparse_object(player, thing)));
      }
    }
}

/* check the current location for bugs */
void do_sweep(player, arg1)
dbref player;
char *arg1;
{
  dbref zon;

  if (*arg1)
  {
    dbref i;

    notify(player, tprintf("All places that respond to %s:", arg1));
    for (i = db[db[player].location].contents; i != NOTHING; i = db[i].next)
      if (Typeof(i) != TYPE_PLAYER || i == player)
	print_atr_match(i, player, arg1);
    for (i = db[player].contents; i != NOTHING; i = db[i].next)
      if (Typeof(i) != TYPE_PLAYER || i == player)
	print_atr_match(i, player, arg1);
    print_atr_match(db[player].location, player, arg1);
    for (i = db[db[player].location].exits; i != NOTHING; i = db[i].next)
      if (Typeof(i) != TYPE_PLAYER || i == player)
	print_atr_match(i, player, arg1);
    print_atr_match(db[player].zone, player, arg1);
    if (db[player].zone != db[0].zone)
      print_atr_match(db[0].zone, player, arg1);
  }
  else
  {
    dbref here = db[player].location;
    dbref test;

    test = here;
    if (here == NOTHING)
      return;
    if (Dark(here))
    {
      notify(player, "Sorry it is dark here; you can't search for bugs");
      return;
    }
    notify(player, "Sweeping...");
    DOZONE(zon, player)
      if (Hearer(zon))
    {
      notify(player, "Zone:");
      break;
    }
    DOZONE(zon, player)
      if (Hearer(zon))
    {
      notify(player, "Zone:");
      notify(player, tprintf("  %s =%s.", db[zon].name,
			     eval_sweep(db[here].zone)));
    }
    if (Hearer(here))
    {
      notify(player, "Room:");
      notify(player, tprintf("  %s =%s.", db[here].name,
			     eval_sweep(here)));
    }
    for (test = db[test].contents; test != NOTHING; test = db[test].next)
      if (Hearer(test))
      {
	notify(player, "Contents:");
	break;
      }
    test = here;
    for (test = db[test].contents; test != NOTHING; test = db[test].next)
      if (Hearer(test))
	notify(player, tprintf("  %s =%s.", db[test].name,
			       eval_sweep(test)));
    test = here;
    for (test = db[test].exits; test != NOTHING; test = db[test].next)
      if (Hearer(test))
      {
	notify(player, "Exits:");
	break;
      }
    test = here;
    for (test = db[test].exits; test != NOTHING; test = db[test].next)
      if (Hearer(test))
	notify(player, tprintf("  %s =%s.", db[test].name,
			       eval_sweep(test)));
    for (test = db[player].contents; test != NOTHING; test = db[test].next)
      if (Hearer(test))
      {
	notify(player, "Inventory:");
	break;
      }
    for (test = db[player].contents; test != NOTHING; test = db[test].next)
      if (Hearer(test))
	notify(player, tprintf("  %s =%s.", db[test].name,
			       eval_sweep(test)));
    notify(player, "Done.");
  }
}

void do_whereis(player, name)
dbref player;
char *name;
{
  dbref thing;

  if (*name == '\0')
  {
    notify(player, "You must specify a valid player name.");
    return;
  }
  if ((thing = lookup_player(name)) == NOTHING)
  {
    notify(player, tprintf("%s does not seem to exist.", name));
    return;
  }
  /* 
     init_match(player, name, TYPE_PLAYER); match_player(NOTHING, NULL); match_exit();
     match_neighbor(); match_possession(); match_absolute(); match_me(); */

  if (db[thing].flags & DARK)
  {
    notify(player, tprintf("%s wishes to have some privacy.", db[thing].name));
    if (!could_doit(player, thing, A_LPAGE))
      notify(thing,
	     tprintf("%s tried to locate you and failed.",
		     unparse_object(thing, player)));
    return;
  }
  notify(player,
	 tprintf("%s is at: %s.", db[thing].name,
		 unparse_object(player, db[thing].location)));
  if (!could_doit(player, thing, A_LPAGE))
    notify(thing,
	   tprintf("%s has just located your position.",
		   unparse_object(thing, player)));
  return;

}

void do_laston(player, name)
dbref player;
char *name;
{
  char *attr;
  dbref thing;
  long tim;

  if (*name == '\0')
  {
    notify(player, "You must specify a valid player name.");
    return;
  }

  if ((thing = lookup_player(name)) == NOTHING || Typeof(thing) != TYPE_PLAYER)
  {
    notify(player, tprintf("%s does not seem to exist.", name));
    return;
  }

  attr = atr_get(thing, A_LASTCONN);
  tim = atol(attr);
  if (!tim)
    notify(player, tprintf("%s has never logged on.", db[thing].name));
  else
    notify(player, tprintf("%s was last logged on: %s.",
			   db[thing].name, mktm(tim, "D", player)));

  attr = atr_get(thing, A_LASTDISC);
  tim = atol(attr);
  if (tim)
    notify(player, tprintf("%s last logged off at: %s.",
			   db[thing].name, mktm(tim, "D", player)));
  return;
}

char *eval_sweep(thing)
dbref thing;
{
  char sweep_str[30];

  strcpy(sweep_str, "\0");
  if (Live_Player(thing))
    strcat(sweep_str, " player");
  if (Live_Puppet(thing))
    strcat(sweep_str, " puppet");
  if (Commer(thing))
    strcat(sweep_str, " commands");
  if (Listener(thing))
    strcat(sweep_str, " messages");
  return stralloc(sweep_str);
}

/* End look.c */
