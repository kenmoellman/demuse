/* predicates.c */
/* $Id: predicates.c,v 1.25 1994/02/18 22:40:54 nils Exp $ */

/* Predicates for testing various conditions */

#include <ctype.h>
#include <stdarg.h>
/*
#include <varargs.h>
#define NO_PROTO_VARARGS
*/
#include "db.h"
#include "interface.h"
#include "config.h"
#include "externs.h"
int my_strncmp(char *, char *, size_t);

void did_it_int P((dbref, dbref, ATTR *, char *, ATTR *, char *, ATTR *, int));


#define TPRINTF_BUF 65535

char *tprintf(char *format, ...)
{
  static char buff[TPRINTF_BUF];
  va_list ap;

  va_start(ap, format);
  vsnprintf(buff, TPRINTF_BUF, format, ap);
  buff[TPRINTF_BUF-1] = 0;
  va_end(ap);
  return(stralloc(buff));
}

/*
char *tprintf(va_alist)  
va_dcl 
{
  long args[10];
  int k;
  static char buff[10240];
  va_list ap;
  char *format;

  va_start(ap);
  format = va_arg(ap, char *);

  for (k = 0; k < 10; k++)
    args[k] = va_arg(ap, long);

  va_end(ap);

  sprintf(buff, format, args[0], args[1], args[2], args[3], args[4],
	  args[5], args[6], args[7], args[8], args[9], args[10]);
  buff[10000] = 0;
  return (buff);
}
#endif
*/

int Level(thing)
dbref thing;
{
  if (db[thing].owner != thing)
    if (db[thing].flags & INHERIT_POWERS)
      return Level(db[thing].owner);
    else
      return CLASS_VISITOR;	/* no special powers. */

  if (db[thing].flags & PLAYER_MORTAL)
    return CLASS_VISITOR;

  return *db[thing].pows;
}
int Levnm(thing)
dbref thing;
{
  if (db[thing].flags & INHERIT_POWERS)
    thing = db[thing].owner;
  if (Typeof(thing) == TYPE_PLAYER)
    return *db[thing].pows;
  return Level(thing);
}

int power(thing, level_check)
dbref thing;
int level_check;
{
  /* mortal flag on player makes him mortal - no arguments */
  if (IS(thing, TYPE_PLAYER, PLAYER_MORTAL) && level_check != POW_MEMBER)
    return 0;
  return has_pow(thing, NOTHING, level_check);
}

int inf_mon(thing)
dbref thing;
{
  if (has_pow(db[thing].owner, NOTHING, POW_MONEY))
    return 1;
  return 0;
}
int inf_quota(thing)
dbref thing;
{
  if (has_pow(db[thing].owner, NOTHING, POW_NOQUOTA))
    return 1;
  return 0;
}

int can_link_to(who, where, cutoff_level)
dbref who;
dbref where;
int cutoff_level;
{
  return (where >= 0 && where < db_top &&
	  (controls(who, where, cutoff_level) ||
	   (db[where].flags & LINK_OK)));
}

int could_doit(player, thing, attr)
dbref player;
dbref thing;
ATTR *attr;
{
  /* no if puppet tries to get key */
  if ((Typeof(player) == TYPE_THING) && IS(thing, TYPE_THING, THING_KEY))
    return 0;
  if ((Typeof(thing) == TYPE_EXIT) && (db[thing].link == NOTHING))
    return 0;
  if ((Typeof(thing) == TYPE_PLAYER || Typeof(thing) == TYPE_CHANNEL ||
#ifdef USE_UNIV
       Typeof(thing) == TYPE_UNIVERSE ||
#endif
       Typeof(thing) == TYPE_THING)
      && db[thing].location == NOTHING)
    return 0;
  return (eval_boolexp(player, thing, atr_get(thing, attr),
		       get_zone_first(player)));
}

void did_it(player, thing, what, def, owhat, odef, awhat)
dbref player;
dbref thing;
ATTR *what;
char *def;
ATTR *owhat;
char *odef;
ATTR *awhat;
{
  did_it_int(player, thing, what, def, owhat, odef, awhat, 0);
}

void did_it_now(player, thing, what, def, owhat, odef, awhat)
dbref player;
dbref thing;
ATTR *what;
char *def;
ATTR *owhat;
char *odef;
ATTR *awhat;
{
  /* -20 is like nice -20 someday? maybe -wm */
  did_it_int(player, thing, what, def, owhat, odef, awhat, (-20));
}

void did_it_int(player, thing, what, def, owhat, odef, awhat, pri)
dbref player;
dbref thing;
ATTR *what;
char *def;
ATTR *owhat;
char *odef;
ATTR *awhat;
int pri;
{
  char *d;
  char buff[1024];
  char buff2[1024];
  dbref loc = db[player].location;

  if (loc == NOTHING)
    return;
  /* message to player */
  if (what)
  {
    if (*(d = atr_get(thing, what)))
    {
      strcpy(buff2, d);
      pronoun_substitute(buff, player, buff2, thing);
      notify(player, buff + strlen(db[player].name) + 1);
    }
    else if (def)
      notify(player, def);
  }
  if (!IS(get_room(player), TYPE_ROOM, ROOM_AUDITORIUM))
    /* message to neighbors */
    if (owhat)
      if (*(d = atr_get(thing, owhat)) && !(db[thing].flags & HAVEN))
      {
	strcpy(buff2, d);
	pronoun_substitute(buff, player, buff2, thing);
	notify_in(loc, player, tprintf("%s %s", db[player].cname, buff + strlen(db[player].name) + 1));
      }
      else if (odef)
	notify_in(loc, player,
		  tprintf("%s %s", db[player].cname, odef));
  /* do the action attribute */
  if (awhat && *(d = atr_get(thing, awhat)))
  {
    char *b;
    char dbuff[1024];

    strcpy(dbuff, d);
    d = dbuff;
    /* check if object has # of charges */
    if (*(b = atr_get(thing, A_CHARGES)))
    {
      long num = atol(b);
      char ch[100];

      if (num)
      {
	sprintf(ch, "%ld", num - 1);
	atr_add(thing, A_CHARGES, ch);
      }
      else if (!*(d = atr_get(thing, A_RUNOUT)))
	return;
      strcpy(dbuff, d);
      d = dbuff;
    }
    /* pronoun_substitute(buff,player,d,thing); */
    /* parse the buffer and do the sub commands */
    parse_que_pri(thing, d, player, pri);
  }
}

dbref check_zone(player, who, where, move_type)
dbref player;
dbref who;
dbref where;
int move_type;			/* 0 => walking, 1 => teleport,  2 => home */
{
  dbref old_zone, new_zone;
  int zonefail = 0;

  old_zone = get_zone_first(who);
  new_zone = get_zone_first(where);
  /* if ((old_zone = get_zone(who)) == NOTHING) old_zone = db[0].zone; if
     ((new_zone = get_zone(where)) == NOTHING) new_zone = db[0].zone; */

  /* Check for illegal home zone crossing. Applies to objects only. */
  if (move_type == 2)
  {
#ifdef HOME_ACROSS_ZONES
    return (dbref) 1;	/* say what? we want to restrict the home cmd? nah. */
#else
    notify(player, "Sorry bub, can't go home across zones.");
    return (dbref) 0;
#endif
  }

  /* safety in case root hasn't set the universal zone yet */
  if ((old_zone == NOTHING) || (new_zone == NOTHING))
    return (dbref) 1;

  /* Check to see if the zones don't match */
  if (old_zone != new_zone)
  {

    /* Check to see if 'who' can pass the zone leave-lock */
    if (move_type == 1 && !could_doit(who, old_zone, A_LLOCK) &&
	!controls(player, old_zone, POW_TELEPORT))
    {
      did_it(who, old_zone, A_LFAIL, "You can't leave.", A_OLFAIL,
	     NULL, A_ALFAIL);
      return (dbref) 0;
    }
    else
    {
      /* Do not allow KEY objects to leave the zone. */
      if ((Typeof(who) != TYPE_PLAYER) && (db[new_zone].flags & THING_KEY))
	zonefail = 1;

      /* Check for @tel or walk and check appropriate lock. */
      if (!eval_boolexp(who, new_zone, atr_get(new_zone, (move_type)
					     ? A_ELOCK : A_LOCK), old_zone))
	zonefail = 1;

      if (move_type == 1)
      {
	/* Make sure player can properly @tel in or out of zone */
	if (!(db[new_zone].flags & ENTER_OK))
	  zonefail = 1;

	/* Anyone with POW_TEL can @tel anywhere, must be the last condition */
	if (power(player, POW_TELEPORT))
	  zonefail = 0;
      }

      /* Next we evaluate if successfull/failed and do the verbs */

      /* Failed walking attempt */
      if (zonefail == 1 && move_type == 0)
      {
	did_it(who, new_zone, A_FAIL, "You can't go that way.", A_OFAIL,
	       NULL, A_AFAIL);
	return (dbref) 0;
      }

      /* Failed @tel attempt */
      if (zonefail == 1 && move_type == 1)
      {
	did_it(who, new_zone, A_EFAIL, perm_denied(), A_OEFAIL,
	       NULL, A_AEFAIL);
	return (dbref) 0;
      }

      /* Successfull walking attempt */
      if (zonefail == 0 && move_type == 0)
      {
	did_it(who, new_zone, A_SUCC, NULL, A_OSUCC, NULL, A_ASUCC);
	return old_zone;
      }
      /* Successfull @tel attempt */
      if (zonefail == 0 && move_type == 1)
      {
	return (dbref) 1;
      }
    }
  }
  else
  {
    return (dbref) 1;
  }
  return (dbref) 1;
}

int can_see(player, thing, can_see_loc)
dbref player;
dbref thing;
int can_see_loc;
{
  /* 1) your own body isn't listed in a 'look' 2) exits aren't listed in a
     'look' 3) unconnected (sleeping) players aren't listed in a 'look' */
  if (player == thing || Typeof(thing) == TYPE_EXIT ||
      ((Typeof(thing) == TYPE_PLAYER) && !IS(thing, TYPE_PLAYER, CONNECT)))
    return 0;

  /* if the room is lit, you can see any non-dark objects */
  else if (can_see_loc)
    return (!Dark(thing));

  /* otherwise room is dark and you can't see a thing */
  else if (IS(thing, TYPE_THING, THING_LIGHT) && controls(thing, db[thing].location, POW_MODIFY))
    return 1;			/* LIGHT flag. can see it in a dark room. */
  else
    return 0;
}

int can_set_atr(who, what, atr)
dbref who;
dbref what;
ATTR *atr;
{
  if (!can_see_atr(who, what, atr))
    return 0;
  if (atr->flags & AF_BUILTIN)
    return 0;
  if ((atr == A_QUOTA || atr == A_RQUOTA) && !power(who, POW_SECURITY))
    return 0;
  if ((atr == A_PENNIES) && !power(who, POW_MONEY))
    return 0;
  if (!controls(who, what, POW_MODIFY))
    return 0;
  if (atr->flags & AF_WIZARD && atr->obj == NOTHING && !power(who, POW_WATTR))
    return 0;
  if (atr->flags & AF_WIZARD && atr->obj != NOTHING &&
      !controls(who, atr->obj, POW_WATTR))
    return 0;
  return 1;
}

int can_see_atr(who, what, atr)
dbref who;
dbref what;
ATTR *atr;
{
  if (atr == A_PASS && !is_root(who))
    return 0;
  if (!(atr->flags & AF_OSEE) && !controls(who, what, POW_SEEATR) && !(db[what].flags & SEE_OK))
    return 0;
  if (atr->flags & AF_DARK && atr->obj == NOTHING && !power(who, POW_EXAMINE))
    return 0;
  if (atr->flags & AF_DARK && atr->obj != NOTHING && !controls(who, atr->obj, POW_SEEATR))
    return 0;
  return 1;
}

static int group_depth = 0;
static int group_controls_int P((dbref, dbref));
int group_controls(who, what)
dbref who;
dbref what;
{
  group_depth = 0;
  return group_controls_int(who, what);
}
static int group_controls_int(who, what)
dbref who;
dbref what;
{
  group_depth++;
  if (group_depth > 20)
    return 0;
  if (who == what)
    return 1;
  else if (*atr_get(what, A_USERS))
  {
    char buf[1024];
    char *s = buf;
    char *z;

    strcpy(buf, atr_get(what, A_USERS));
    while ((z = parse_up(&s, ' ')))
    {
      dbref i;

      if (*z != '#')
	continue;
      i = atol(z + 1);
      if (i >= 0 && i < db_top && Typeof(i) == TYPE_PLAYER)
	if (group_controls_int(who, i))
	  return 1;
    }
  }
  return 0;
}

int controls_a_zone(who, what, cutoff_level)
dbref who, what;
int cutoff_level;
{
  dbref zon;

  DOZONE(zon, what)
    if (controls(who, zon, cutoff_level))
    return 1;
  return 0;
}

int controls(who, what, cutoff_level)
dbref who;
dbref what;
int cutoff_level;
{
  dbref where;

  where = db[what].location;
  /* valid thing to control? */
  if (what == NOTHING)
    return has_pow(who, what, cutoff_level);
  if (what < 0 || what >= db_top)
    return 0;

  if ((cutoff_level == POW_EXAMINE || cutoff_level == POW_SEEATR) &&
      (db[what].flags & SEE_OK))
    return 1;

  /* owners (and their stuff) control the owner's stuff */
  /* if ( db[who].owner == db[what].owner ) return 1; */
#ifdef USE_SPACE		/* Added by MM */
  if ((db[what].owner == SPACE_LORD) && !power(who, POW_SPACE) &&
      (db[where].flags & ROOM_ZEROG))
    return 0;
#endif
  if (db[who].owner == db[what].owner
      || group_controls(db[who].owner, db[what].owner))
    /* the owners match, check ipow */
    if (db[who].owner == who)
      return 1;			/* it's the player -- e controls all eir
				   stuff */
    else if (db[who].flags & INHERIT_POWERS)
      return 1;
    else if ((db[what].flags & INHERIT_POWERS || db[what].owner == what) &&
    (!db[db[what].owner].pows || (*db[db[what].owner].pows) > CLASS_CITIZEN))
      return 0;			/* non inherit powers things don't control
				   inherit stuff */
    else
      return 1;			/* the target isn't inherit  */
  /* if (( db[who].owner == db[what].owner ) &&
     ((db[who].flags&INHERIT_POWERS) || !(db[what].flags&INHERIT_POWERS) ||
     Typeof(who)==TYPE_PLAYER || * or it's a player -- dont need I. *
     !power(db[who].owner,TYPE_OFFICIAL))) * or it's no special powers *
     return 1; */

  if (db[what].flags & INHERIT_POWERS)
    what = db[what].owner;

  /* root controls all */
  if (who == root)
    return 1;

  /* and root don't listen to anyone! */
  if ((what == root) || (db[what].owner == root))
    return 0;

  if (has_pow(who, what, cutoff_level))
    return 1;

  return 0;
}

dbref def_owner(who)
dbref who;
{
  if (!*atr_get(who, A_DEFOWN))
    return db[who].owner;
  else
  {
    dbref i;

    i = match_thing(who, atr_get(who, A_DEFOWN));
    if (i == NOTHING || Typeof(i) != TYPE_PLAYER)
      return db[who].owner;
    if (!controls(who, i, POW_MODIFY))
    {
      notify(who, tprintf("You don't control %s, so you can't make things owned by %s.", unparse_object(who, i), unparse_object(who, i)));
      return db[who].owner;
    }
    return db[i].owner;
  }
}

int can_link(who, what, cutoff_level)
dbref who;
dbref what;
int cutoff_level;
{
  return ((Typeof(what) == TYPE_EXIT && db[what].location == NOTHING)
	  || controls(who, what, cutoff_level));
}

int can_pay_fees(who, credits, quota)
dbref who;
int credits, quota;
{
  /* can't charge credits till we've verified building quota */
  if (!Guest(db[who].owner) && Pennies(db[who].owner) < credits
      && !(has_pow(db[who].owner, NOTHING, POW_MONEY) || power(db[who].owner, POW_FREE)))
  {
    notify(who, "You do not have sufficient credits.");
    return 0;
  }

  /* check building quota */
  if (!pay_quota(who, quota))
  {
    notify(who, "You do not have sufficient quota.");
    return 0;
  }

  /* charge credits */
  payfor(who, credits);

  return 1;
}

void giveto(who, pennies)
dbref who;
int pennies;
{
  int old_amount;

  /* wizards don't need pennies */
  if (has_pow(db[who].owner, NOTHING, POW_MONEY))
    return;

  who = db[who].owner;
  old_amount = Pennies(who);
  if (old_amount + pennies < 0)
  {
    if ((old_amount > 0) && (pennies > 0))
      s_Pennies(who, (long) (((unsigned)-2) / 2));
    else
      s_Pennies(who, (long) 0);
  }
  else
    s_Pennies(who, (long)(old_amount + pennies));
}

int payfor(who, cost)
dbref who;
int cost;
{
  dbref tmp;

  if (Guest(who) ||
      has_pow(db[who].owner, NOTHING, POW_MONEY))
    return 1;
  else if ((tmp = Pennies(db[who].owner)) >= cost)
  {
    s_Pennies(db[who].owner, (long) (tmp - cost));
    return 1;
  }
  else
    return 0;
}

void add_bytesused(who, payment)
dbref who;
int payment;
{
  char buf[20];
  long tot;

  if (!*atr_get(who, A_BYTESUSED))
    recalc_bytes(who);

  tot = atol(atr_get(who, A_BYTESUSED)) + payment;
  sprintf(buf, "%ld", tot);
  atr_add(who, A_BYTESUSED, buf);
  if (!*atr_get(who, A_BYTELIMIT))
    return;			/* no byte quota. */
  if (tot > atol(atr_get(who, A_BYTELIMIT)))
    db[who].i_flags |= I_QUOTAFULL;
  else
    db[who].i_flags &= ~I_QUOTAFULL;
}

void recalc_bytes(own)
dbref own;
{
  dbref i;

  for (i = 0; i < db_top; i++)
    if (db[i].owner == own)
    {
      db[i].size = 0;
      db[i].i_flags |= I_UPDATEBYTES;
    }
  atr_add(own, A_BYTESUSED, "0");
}

void add_quota(who, payment)
dbref who;
int payment;
{
  char buf[20];

  if (has_pow(db[who].owner, NOTHING, POW_NOQUOTA))
  {
    sprintf(buf, "%ld", atol(atr_get(db[who].owner, A_QUOTA)) - payment);
    atr_add(db[who].owner, A_QUOTA, buf);
    recalc_bytes(db[who].owner);
    return;
  }

  sprintf(buf, "%ld", atol(atr_get(db[who].owner, A_RQUOTA)) + payment);
  atr_add(db[who].owner, A_RQUOTA, buf);

  recalc_bytes(db[who].owner);
}

int pay_quota(who, cost)
dbref who;
int cost;
{
  long quota;
  char buf[20];

  if (has_pow(db[who].owner, NOTHING, POW_NOQUOTA))
  {
    sprintf(buf, "%ld", atol(atr_get(db[who].owner, A_QUOTA)) + cost);
    atr_add(db[who].owner, A_QUOTA, buf);
    recalc_bytes(db[who].owner);
    return 1;
  }

  if (db[db[who].owner].i_flags & I_QUOTAFULL)
    return 0;

  /* determine quota */
  quota = atol(atr_get(db[who].owner, A_RQUOTA));

  /* enough to build? */
  quota -= cost;
  if (quota < 0)
    return 0;

  /* doc the quota */
  sprintf(buf, "%ld", quota);
  atr_add(db[who].owner, A_RQUOTA, buf);

  recalc_bytes(db[who].owner);

  return 1;
}

int sub_quota(who, cost)
dbref who;
int cost;
{
  char buf[20];

  if (has_pow(who, NOTHING, POW_NOQUOTA))
  {
    sprintf(buf, "%ld", atol(atr_get(db[who].owner, A_QUOTA)) + cost);
    atr_add(db[who].owner, A_QUOTA, buf);
    recalc_bytes(db[who].owner);
    return 1;
  }

  /* doc the quota */
  sprintf(buf, "%ld", atol(atr_get(db[who].owner, A_RQUOTA)) - cost);
  atr_add(db[who].owner, A_RQUOTA, buf);

  recalc_bytes(db[who].owner);
  return 1;
}

int ok_attribute_name(name)
char *name;
{
  return (name
	  && *name
	  && !strchr(name, '=')
	  && !strchr(name, ',')
	  && !strchr(name, ';')
	  && !strchr(name, ':')
	  && !strchr(name, '.')
	  && !strchr(name, '[')
	  && !strchr(name, ']')
	  && !strchr(name, ' '));
}

int ok_thing_name(name)
const char *name;
{
  return ok_name(name) && !strchr(name, ';');
}

int ok_exit_name(name)
const char *name;
{
  return ok_name(name);
}

int ok_room_name(name)
const char *name;
{
  return ok_name(name) && !strchr(name, ';');
}

int ok_name(name)
char *name;
{
  char *scan;

  for (scan = name; *scan; scan++)
  {
    if (!isprint(*scan))
    {				/* was isgraph(*scan) */
      return 0;
    }
    switch (*scan)
    {
    case '[':
    case ']':
    case '#':
    case '(':
    case ')':
    case '%':
    case '\'':
    case '\"':
      return 0;
      break;
    default:
      break;
    }
  }

  return (name
	  && *name
	  && *name != LOOKUP_TOKEN
	  && *name != NUMBER_TOKEN
	  && *name != NOT_TOKEN
	  && !strchr(name, ARG_DELIMITER)
	  && !strchr(name, AND_TOKEN)
	  && !strchr(name, OR_TOKEN)
	  && string_compare(name, "me")
	  && string_compare(name, "home")
	  && string_compare(name, "here") 
          && ((!atol(name + strlen(guest_alias_prefix))) &&
              strncmp(name + strlen(guest_alias_prefix), "0", 1)) );

/* guest_alias_prefix, 12));  */
                

}

int my_strncmp(char *string1, char *string2, size_t len)
{
  char newstring1[len+1]; /* new string 1 */
  char newstring2[len+1]; /* new string 2 */
  char *p; /* pointer */
  int x; /* counter */
  size_t newlen;  /* new length variable */



  newlen = len;
  if(newlen>strlen(string1))
  {
    newlen = strlen(string1);
  }

  log_io(tprintf("string1: %s   string2: %s   len: %d   newlen: %d", string1, string2, len, newlen));

  p = string1;

  for(x=0;x<newlen;x++)
  {
    newstring1[x]=toupper(*p++);
  } 

  p = string2;
  for(x=0;x<newlen;x++)
  {
    newstring2[x]=toupper(*p++);
  } 

  return(strncmp(newstring1, newstring2, newlen));

}


int ok_object_name(obj, name)
dbref obj;
char *name;
{
  switch (Typeof(obj))
  {
  case TYPE_THING:
  case TYPE_CHANNEL:
#ifdef USE_UNIV
  case TYPE_UNIVERSE:
#endif
    return ok_thing_name(name);
  case TYPE_EXIT:
    return ok_exit_name(name);
  case TYPE_ROOM:
    return ok_room_name(name);
  }
  log_error("Object with invalid name found!");
  return 0;
}

int ok_player_name(player, name, alias)
dbref player;
char *name;
char *alias;
{
  char *scan;


  if (!ok_name(name) || strchr(name, ';') || strlen(name) > player_name_limit)
    return 0;
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
    {				/* was isgraph(*scan) */
      return 0;
    }
/*    if (*scan == '~') return 0; */
    switch (*scan)
    {
    case '~':
    case ';':
    case ',':
    case '*':
    case '@':
    case '#':
      return 0;
      break;
    default:
      break;
    }
  }

  if (lookup_player(name) != NOTHING && lookup_player(name) != player)
    return 0;
  if (!string_compare(name, alias))
    return 0;
  if (*name && name[strlen(name) - 1] == ':')
  {
    char buf[1024];

    strcpy(buf, name);
    buf[strlen(buf) - 1] = '\0';
    if (lookup_player(buf) != NOTHING && lookup_player(buf) != player)
      return 0;
  }
  if (player != NOTHING)
  {				/* this isn't being created */
    int min;

    if (*alias)
    {
      min = strlen(alias);
      if (!strchr(name, ' '))
	if (strlen(name) < min)
	  min = strlen(name);
      if (lookup_player(alias) != player && lookup_player(alias) != NOTHING)
	return 0;
    }
    else
    {
      if (strchr(name, ' '))
	return 0;
      min = strlen(name);
    }
    if (min > player_reference_limit)
      return 0;
  }
  if (*alias && (!ok_name(alias) || strchr(alias, ' ')))
    return 0;
  if (strlen(name) > player_name_limit)
    return 0;
  return 1;
}

int ok_password(password)
char *password;
{
  char *scan;

  if (*password == '\0')
    return 0;

  for (scan = password; *scan; scan++)
  {
    if (!(isprint(*scan) && !isspace(*scan)))
    {
      return 0;
    }
  }

  return 1;
}

char *main_exit_name(exit)
dbref exit;
{
  char buf[1000];
  char *s;

  strcpy(buf, db[exit].cname);
  if ((s = strchr(buf, ';')))
    *s = '\0';
  return stralloc(buf);
}

static void sstrcat(old, string, app)
char *old;
char *string;
char *app;
{
  char *s;
  int hasnonalpha = 0;

  if ((strlen(app) + (s = (strlen(string) + string)) - old) > 950)
    return;
  strcpy(s, app);
  for (; *s; s++)
    if ((*s == '(') && !hasnonalpha)
      *s = '<';
    else
    {
      if ((*s == ',') || (*s == ';') || (*s == '['))
	*s = ' ';
      if (!isalpha(*s) && *s != '#' && *s != '.')
	hasnonalpha = 1;
    }
}

/*
 * pronoun_substitute()
 *
 * %-type substitutions for pronouns
 *
 * %s/%S for subjective pronouns (he/she/it/e/they, He/She/It/E/They)
 * %o/%O for objective pronouns (him/her/it/em/them, Him/Her/It/Em/Them)
 * %p/%P for possessive pronouns (his/her/its/eir/their, His/Her/Its/Eir/etc)
 * %n    for the player's name.
 * Note: e/em/eir are non-gender-specific pronouns and use the 'spivak' sex.
 */
void pronoun_substitute(result, player, str, privs)
char *result;
dbref player;
char *str;
dbref privs;			/* object whose privs are used */
{
  char c, *s, *p;
  char *ores;
  dbref thing;
  ATTR *atr;
  static char *subjective[7] =
  {"", "it", "she", "he", "e", "they", "he/she"};
  static char *possessive[7] =
  {"", "its", "her", "his", "eir", "their", "his/her"};
  static char *objective[7] =
  {"", "it", "her", "him", "em", "them", "him/her"};
  int gend;

  if ((privs < 0) || (privs >= db_top))
    privs = 3;
  /* figure out player gender */
  switch (*atr_get(player, A_SEX))
  {
  case 'M':
  case 'm':
    gend = 3;
    break;
  case 'f':
  case 'F':
  case 'w':
  case 'W':
    gend = 2;
    break;
  case 's':
  case 'S':
    gend = 4;
    break;
  case 'p':
  case 'P':
    gend = 5;
    break;
  case 'n':
  case 'N':
    gend = 1;
    break;
  case '/':
    gend = 6;
    break;
  case 'l':
  case 'L':
    gend = 0;
    break;
  default:
    gend = 4;
  }

 

/*  strncpy(mybuffer, db[player].name, strlen(result));
*  mybuffer[strlen(result)] = '\0'; *
  strncpy(ores = result, mybuffer, strlen(result)); */

  strcpy(ores = result, db[player].name); 

  result += strlen(result);
  *result++ = ' ';
  while (*str && ((result - ores) < 1000))
  {
    if (*str == '[')
    {
      char buff[1024];

      str++;
      museexec(&str, buff, privs, player, 0);
      if ((strlen(buff) + (result - ores)) > 950)
	continue;
      strcpy(result, buff);
      result += strlen(result);
      if (*str == ']')
	str++;
    }
    else if (*str == '%')
    {
      *result = '\0';
      c = *(++str);

      switch (c)
      {
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
	if (!wptr[c - '0'])
	  break;
	sstrcat(ores, result, wptr[c - '0']);
	break;
      case 'v':
      case 'V':
	{
	  int a;

	  a = to_upper(str[1]);
	  if ((a < 'A') || (a > 'Z'))
	    break;
	  if (*str)
	    str++;
	  sstrcat(ores, result, atr_get(privs, A_V[a - 'A']));
	}
	break;
      case 's':
      case 'S':
	sstrcat(ores, result, (gend == 0) ? db[player].cname : subjective[gend]);
	break;
      case 'p':
      case 'P':
	if (gend == 0)
	{
	  sstrcat(ores, result, db[player].cname);
	  sstrcat(ores, result, "'s");
	}
	else
	  sstrcat(ores, result, possessive[gend]);
	break;
      case 'o':
      case 'O':
	sstrcat(ores, result, (gend == 0) ? db[player].cname : objective[gend]);
	break;
      case 'n':
      case 'N':
	sstrcat(ores, result, safe_name(player));
	break;
      case '#':
	if ((strlen(result) + result - ores) > 990)
	  break;
	sprintf(result + strlen(result), "#%ld", player);
	break;
      case '/':
	str++;
	if ((s = strchr(str, '/')))
	{
	  *s = '\0';
	  if ((p = strchr(str, ':')))
	  {
	    *p = '\0';
	    thing = atol(++str);
	    *p = ':';
	    str = ++p;
	  }
	  else
	    thing = privs;
	  atr = atr_str(privs, thing, str);
	  if (atr && can_see_atr(privs, thing, atr))
	    sstrcat(ores, result, atr_get(thing, atr));
	  *s = '/';
	  str = s;
	}
	break;
      case 'r':
      case 'R':
	sstrcat(ores, result, "\n");
	break;
      case 't':
      case 'T':
	sstrcat(ores, result, "\t");
	break;
      case 'a':
      case 'A':
	sstrcat(ores, result, "\a");
	break;
      default:
	if ((result - ores) > 990)
	  break;
	*result = *str;
	result[1] = '\0';
	break;
      }
      if (isupper(c) && c != 'N')
	*result = to_upper(*result);

      result += strlen(result);
      if (*str)
	str++;
    }
    else
    {
      if ((result - ores) > 990)
	break;
      /* check for escape */
      if ((*str == '\\') && (str[1]))
	str++;
      *result++ = *str++;
    }
  }
  *result = '\0';
}

void push_list(list, item)
dbref **list;
dbref item;
{
  int len;
  dbref *newlist;

  if ((*list) == NULL)
    len = 0;
  else
    for (len = 0; (*list)[len] != NOTHING; len++) ;
  len += 1;

  newlist = malloc((len + 1) * sizeof(dbref));

  if (*list)
  {
/*    bcopy(*list, newlist, sizeof(dbref) * (len)); */
    memcpy(newlist, *list, sizeof(dbref) * (len));
  }
  else
    newlist[0] = NOTHING;
  if (*list)
    free(*list);
  newlist[len - 1] = item;
  newlist[len] = NOTHING;
  (*list) = newlist;
}

void remove_first_list(list, item)
dbref **list;
dbref item;
{
  int pos;

  if (!*list)
    return;
  for (pos = 0; (*list)[pos] != item && (*list)[pos] != NOTHING; pos++) ;
  if ((*list)[pos] == NOTHING)
    return;
  for (; (*list)[pos] != NOTHING; pos++)
    (*list)[pos] = (*list)[pos + 1];
  /* and ignore the reallocation, unless we have to free it. */
  if ((*list)[0] == NOTHING)
  {
    free(*list);
    (*list) = NULL;
  }
}

/*
dbref starts_with_player_internal (char *);
*/

dbref starts_with_player(char *name)
{
/* this function is only used when creating an object, or changing an object's
 * name.  Probably need a slightly different function, or, a wrapper function,
 * for checking the output string for spoofing, so it'll handle %r's - wm  
 */

  return NOTHING;

/* REMOVE OLD SPOOF PROTECTIONS
  char *buf, *s, *t;
  dbref retval;

  t = buf = stralloc(name);

  while ((s = strchr(t, ' ')))
  {
    *s++ = 0;
    t = s;
    retval = starts_with_player_internal(buf);
    if(retval)
    {
      return (retval);
    }
    *--s = ' ';
  }
  retval = starts_with_player_internal(buf);
  return (retval);
*/
}


/*  REMOVE OLD SPOOF PROTECTIONS
dbref starts_with_player_internal(name)
char *name;
{
  dbref b;

  b = lookup_player(strip_color_nobeep(name));
#ifdef DEBUG
  notify(root,tprintf("b:%d",b)); 
#endif
  if (b == NOTHING || (string_compare(db[b].name, name)))
    return NOTHING;
  else
    return b;
}
*/

int is_in_zone(player, zone)
dbref player;
dbref zone;
{
  dbref zon;

  DOZONE(zon, player)
    if (zon == zone)
    return 1;
  return 0;
}

char *safe_name(foo)
dbref foo;
{
  if (Typeof(foo) == TYPE_EXIT)
  {
    return main_exit_name(foo);
  }
  return db[foo].cname;
}

char *ljust(text, number)
char *text;
int number;
{
  int leader;
  char tbuf1[1000];
  int i;

  leader = number - strlen(strip_color(text));

  if (leader <= 0)
  {
    return tprintf(truncate_color(text, number));
  }

  for (i = 0; i < leader; i++)
    tbuf1[i] = ' ';
  tbuf1[i] = '\0';
  return tprintf("%s%s", text, tbuf1);
}


dbref real_owner(dbref object)
{
  dbref current;
  int x;

  x = 0;
  current = db[object].owner;

  for(x=0 ; (current != db[current].owner) && (x < 1000) ; x++)
  {
    current = db[current].owner;
  }

  if (x < 1000)
  {
    return current;
  }

  log_security(tprintf("Object recursion occurred looking up owner of %s (%d)",db[object].name,object));
  return -1;

}
  

int valid_player(dbref player) 
{
  if ((player > -2) && (player < db_top))
  {
    return 1;
  }
  log_error("Eek. Something bad happened, an invalid player has a command in the queue!");
  return 0;
}

