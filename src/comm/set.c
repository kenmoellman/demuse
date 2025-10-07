/* set.c */
/* $Id: set.c,v 1.23 1994/01/26 22:28:27 nils Exp $ */
  
/* commands which set parameters */
#include <stdio.h>
#include <ctype.h>
#include <values.h>

#include "db.h"
#include "config.h"
#include "match.h"
#include "interface.h"
#include "externs.h"
#include "credits.h"


void do_destroy(player, name)
dbref player;
char *name;
{
  dbref thing;

  if (controls(player, db[player].location, POW_MODIFY))
    init_match(player, name, NOTYPE);
  else
    init_match(player, name, TYPE_THING);

  if (controls(player, db[player].location, POW_MODIFY))
  {
    match_exit();
  }

  match_everything();
  thing = match_result();
  if ((thing != NOTHING) && (thing != AMBIGUOUS) &&
      !controls(player, thing, POW_MODIFY) &&
      !(Typeof(thing) == TYPE_THING &&
	(db[thing].flags & THING_DEST_OK)))
  {
    notify(player, perm_denied());
    return;
  }

  if (db[thing].children && (*db[thing].children) != NOTHING)
    notify(player, "Warning: It has children.");

  if (thing < 0)
  {				/* I hope no wizard is this stupid but just
				   in case */
    notify(player, "I don't know what that is, sorry.");
    return;
  }
  if (thing == 0 || thing == 1 || thing == player_start || thing == root)
  {
    notify(player, "Don't you think that's sorta an odd thing to destroy?");
    return;
  }
  /* what kind of thing we are destroying? */

  if (Typeof(thing) == TYPE_PLAYER)
  { 
    notify(player, "Destroying players isn't allowed, try a @nuke instead.");
    return;
  }
  else if (Typeof(thing) == TYPE_CHANNEL)
  {
    do_channel_destroy(player, name);
  } 
  else  
  {

/*
  switch (Typeof(thing))
  {
  case TYPE_PLAYER:
	{ 
		notify(player, "Destroying players isn't allowed, try a @nuke instead.");
		return;
	}
  case TYPE_CHANNEL:
    do_channel_destroy(player, name);
    ;
  case TYPE_THING:
  case TYPE_ROOM:
#ifdef USE_UNIV
  case TYPE_UNIVERSE:
#endif
  case TYPE_EXIT:
  }
  */
    {
      char *k;

      k = atr_get(thing, A_DOOMSDAY);
      if (*k)
	if (db[thing].flags & GOING)
	{
	  notify(player, tprintf("It seems it's already gunna go away in %s... if you wanna stop it, use @undestroy", time_format_2(atol(k) - now)));
	  return;
	}
	else
	{
	  notify(player, "Sorry, it's protected.");
	}
      else if (db[thing].flags & GOING)
      {
	notify(player, "It seems to already be destroyed.");
	return;
      }
      else
      {
	k = atr_get(player, A_DOOMSDAY);
	if (*k)
	{
	  destroy_obj(thing, atol(k));
	  notify(player, tprintf("Okay, %s will go away in %s.", unparse_object(player, thing), time_format_2(atol(k))));
	}
	else
	{
	  destroy_obj(thing, atol(default_doomsday));
	  notify(player, tprintf("Okay, %s will go away in %s.", unparse_object(player, thing), time_format_2(atol(default_doomsday))));
	}
      }
    }
  }
}

void destroy_obj(obj, no_seconds)
dbref obj;
int no_seconds;
{
  if (!(db[obj].flags & QUIET))
    do_pose(obj, "shakes and starts to crumble", "", 0);
  atr_add(obj, A_DOOMSDAY, int_to_str(no_seconds + now));
  db[obj].flags |= GOING;
  do_halt(obj, "", "");
}

extern time_t muse_up_time;
void do_cname(player, name, cname)
dbref player;
char *name;
char *cname;
{
  dbref thing;

  if ((thing = match_controlled(player, name, POW_MODIFY)) != NOTHING)
  {
    if (Typeof(thing) == TYPE_EXIT)
    {
      char buf[2048];
      char *visname, *rest;

      strcpy(buf, db[thing].name);
      visname = buf;
      rest = strchr(buf, ';');

      if (rest && *rest)
	*rest++ = '\0';
      else
	strcpy(rest, "");

      if (strcmp(visname, strip_color(cname)) != 0)
      {
	notify(player, "Colorized name of exits must match visible name (the name before the first ';').");
	return;
      }
      else
      {
	char buf2[2048];

	sprintf(buf2, "%s;%s", cname, rest);
	notify(player, tprintf("Okay, %s's colorized name is now %s.",
			       db[thing].cname, buf2));
	SET(db[thing].cname, buf2);
      }
    }
    else
    {
      if (strcmp(db[thing].name, strip_color(cname)) != 0)
      {
	notify(player, "Hey! Colorized name doesn't match real name!");
	return;
      }
      else
      {
	if (Typeof(thing) == TYPE_PLAYER)
	  log_important(tprintf("|G+COLOR CHANGE|: %s to %s",
				db[thing].cname, cname));
	notify(player, tprintf("Okay, %s's colorized name is now %s.",
			       db[thing].cname, cname));
	SET(db[thing].cname, cname);
      }
    }
  }
}

void do_name(player, name, cname, is_direct)
dbref player;
char *name;
char *cname;
int is_direct;
{
  dbref thing;
  char *password;
  char *newname;
  

  newname = strip_color_nobeep(cname);
  if ((password = strrchr(cname, ' ')))
  {
    if (password && *password)
    {
      *password++ = '\0';
    }
  }
  if (strncmp(db[player].name, strip_color_nobeep(cname), strlen(db[player].name)+2) == 0)
  {
    do_cname(player, name, cname);
    return;
  }

  if ((thing = match_controlled(player, name, POW_MODIFY)) != NOTHING)
  {
    /* check for bad name */
    if (*newname == '\0')
    {
      notify(player, "Give it what new name?");
      return;
    }

    if (!strncmp("HTTP:", newname, 5))
    {
      notify(player, "Names can't start with HTTP:.");
      return;
    }

    /* check for renaming a player */
    if (Typeof(thing) == TYPE_PLAYER)
    {
      if (!is_direct)
      {
	notify(player, "sorry, players must change their names directly from a net connection.");
	return;
      }
      if (player == thing && !power(player, POW_MEMBER))
      {
	notify(player, tprintf(
		   "Sorry, only registered %s users may change their name.",
				muse_name));
	return;
      }

      if ((password = strrchr(newname, ' ')))
      {
	while (isspace(*password))
	  password--;
	password++;
	*password = '\0';
	password++;
	while (isspace(*password))
	  password++;
      }
      else
	password = newname + strlen(newname);

      /* check for reserved player names */
      if (string_prefix(newname, guest_prefix))
      {
	notify(player, tprintf(
			   "Only guests may have names beginning with '%s'",
				guest_prefix));
	return;
      }

      if (string_prefix(newname, guest_alias_prefix) &&
	  isdigit(*(newname + strlen(guest_alias_prefix))))
      {
	notify(player, tprintf(
	     "Only guests may have names beginning with '%s' and a number.",
				guest_alias_prefix));
	return;
      }

      /* check for null password */
      if (!*password)
      {
	notify(player,
	       "You must specify a password to change a player name.");
	notify(player, "E.g.: name player = newname password");
	return;
      }
      else if (*Pass(player) && strcmp(Pass(player), password) &&
	       strcmp(crypt(password, "XX"), Pass(player)))
      {
	notify(player, "Incorrect password.");
	return;
      }
      else if (!(ok_player_name(thing, newname, atr_get(thing, A_ALIAS))))
      {
	notify(player, "You can't give a player that name.");
	return;
      }

      /* everything ok, notify */

      log_important(tprintf("|G+NAME CHANGE|: %s to %s",
			    unparse_object_a(thing, thing), cname));
      notify_in(db[thing].location, thing, tprintf("%s is now known as %s.", db[thing].name, cname));
      delete_player(thing);
      SET(db[thing].name, newname);
      add_player(thing);
      SET(db[thing].cname, cname);
      notify(player, "Name set.");
      return;
    }

    /* we're an object. */

    if (!ok_object_name(thing, newname))
    {
      notify(player, "That is not a reasonable name.");
      return;
    }

    /* everything ok, change the name */
    if (Hearer(thing))
      notify_in(db[thing].location, thing, tprintf("%s is now known as %s.", db[thing].name, newname));
    SET(db[thing].name, newname);
    SET(db[thing].cname, newname);
/*    check_spoofobj(player, thing); */
    notify(player, "Name set.");
  }
}

void do_describe(player, name, description)
dbref player;
char *name;
char *description;
{
  dbref thing;

  if ((thing = match_controlled(player, name, POW_MODIFY)) != NOTHING)
  {
    s_Desc(thing, description);
    notify(player, "Description set.");
  }
}

void do_unlink(player, name)
dbref player;
char *name;
{
  dbref exit;

  init_match(player, name, TYPE_EXIT);
  match_exit();
  match_here();
  if (power(player, POW_REMOTE))
  {
    match_absolute();
  }

  switch (exit = match_result())
  {
  case NOTHING:
    notify(player, "Unlink what?");
    break;
  case AMBIGUOUS:
    notify(player, "I don't know which one you mean!");
    break;
  default:
    if (!controls(player, exit, POW_MODIFY))
    {
      notify(player, perm_denied());
    }
    else
    {
      switch (Typeof(exit))
      {
      case TYPE_EXIT:
	db[exit].link = NOTHING;
	notify(player, "Unlinked.");
	break;
      case TYPE_ROOM:
	db[exit].link = NOTHING;
	notify(player, "Dropto removed.");
	break;
      default:
	notify(player, "You can't unlink that!");
	break;
      }
    }
  }
}

void do_chown(player, name, newobj)
dbref player;
char *name;
char *newobj;
{
  dbref thing;
  dbref owner;

  log_important(tprintf("%s attempts: @chown %s=%s",
			unparse_object_a(player, player), name, newobj));

  init_match(player, name, TYPE_THING);
  match_possession();
  match_here();
  match_exit();
  match_absolute();
  switch (thing = match_result())
  {
  case NOTHING:
    notify(player, "You don't have that!");
    return;
  case AMBIGUOUS:
    notify(player, "I don't know which you mean!");
    return;
  }
  if (!*newobj || !string_compare(newobj, "me"))
    owner = def_owner(player);	/* @chown thing or @chown thing=me */
  else if ((owner = lookup_player(newobj)) == NOTHING)
    notify(player, "I couldn't find that player.");
  if (power(player, POW_SECURITY))
  {
    if (Typeof(thing) == TYPE_PLAYER && db[thing].owner != thing)
      db[thing].owner = thing;
  }
  if (owner == NOTHING) ;	/* for the else */
  /* if non-robot player */
  else if ((db[thing].owner == thing && Typeof(thing) == TYPE_PLAYER) && !is_root(player))
    notify(player, "Players always own themselves.");

  else if (!controls(player, owner, POW_CHOWN) ||
	   (!controls(player, thing, POW_CHOWN) &&
	    (!(db[thing].flags & CHOWN_OK) ||
	     ((Typeof(thing) == TYPE_THING) &&
	      (db[thing].location != player && !power(player, POW_CHOWN))))))
    notify(player, perm_denied());

  else
  {
    if (power(player, POW_CHOWN))
    {
      /* adjust quotas */
      add_quota(db[thing].owner, QUOTA_COST);
      sub_quota(db[owner].owner, QUOTA_COST);
      /* adjust credits */
      if (!power(player, POW_FREE))
	payfor(player, thing_cost);
      if (!power(db[thing].owner, POW_FREE))
	giveto(db[thing].owner, thing_cost);
    }
    else
    {
      if (Pennies(db[player].owner) < thing_cost)
      {
	notify(player, "You don't have enough money.");
	return;
      }
      /* adjust quotas */
      if (!pay_quota(owner, QUOTA_COST))
      {
	notify(player, (player == owner) ?
	       "Your quota has run out." : "Nothing happens.");
	return;
      }
      add_quota(db[thing].owner, QUOTA_COST);
      /* adjust credits */
      if (!power(player, POW_FREE))
	payfor(player, thing_cost);
      if (!power(player, POW_FREE))
	giveto(db[thing].owner, thing_cost);
    }

    log_important(tprintf("%s succeeds with: @chown %s=%s",
			  unparse_object_a(player, player), unparse_object_a(thing, thing), unparse_object_a(owner, owner)));

    if (db[thing].flags & CHOWN_OK || !controls(player, db[owner].owner, POW_CHOWN))
    {
      db[thing].flags |= HAVEN;
      db[thing].flags &= ~CHOWN_OK;
      db[thing].flags &= ~INHERIT_POWERS;
    }
    db[thing].owner = db[owner].owner;
    notify(player, "Owner changed.");
  }
}

static struct hearing
{
  dbref obj;
  int did_hear;
  struct hearing *next;
}
 *hearing_list = NULL;

void mark_hearing(obj)
dbref obj;
{
  int i;
  struct hearing *mine;

  mine = malloc(sizeof(struct hearing));

  mine->next = hearing_list;
  mine->did_hear = Hearer(obj);
  mine->obj = obj;

  hearing_list = mine;
  for (i = 0; db[obj].children && db[obj].children[i] != NOTHING; i++)
  {
    mark_hearing(db[obj].children[i]);
  }
}

void check_hearing()
{
  struct hearing *mine;
  int now_hear;
  dbref obj;

  while (hearing_list)
  {
    mine = hearing_list;
    hearing_list = hearing_list->next;

    obj = mine->obj;
    now_hear = Hearer(mine->obj);

    if (now_hear && !mine->did_hear)
      notify_in(db[obj].location, obj,
		tprintf("%s grows ears and can now hear.", db[obj].name));
    if (mine->did_hear && !now_hear)
      notify_in(db[obj].location, obj,
		tprintf("%s loses its ears and is now deaf.", db[obj].name));
    free(mine);
  }
}

void do_unlock(player, name)
dbref player;
char *name;
{
  dbref thing;
  ATTR *attr = A_LOCK;

  if ((thing = match_controlled(player, name, POW_MODIFY)) == NOTHING)
    return;
  if (thing == root && player != root)
  {
    notify(player, "Not likely.");
    return;
  }
  atr_add(thing, attr, "");
  notify(player, "Unlocked.");
}

void do_set(player, name, flag, allow_commands)
dbref player;
char *name;
char *flag;
int allow_commands;
{
  dbref thing;
  char *p, *q;
  object_flag_type f;
  int her;

  /* find thing */
  if ((thing = match_thing(player, name)) == NOTHING)
    return;
  if (thing == root && player != root)
  {
    notify(player, "Only root can set him/herself!");
    return;
  }
  if (!*atr_get(db[thing].owner, A_BYTESUSED))
    recalc_bytes(db[thing].owner);

  her = Hearer(thing);

  /* check for attribute set first */
  for (p = flag; *p && (*p != ':'); p++)
    ;
  if (*p)
  {
    ATTR *attr;

    *p++ = '\0';
    if (!(attr = atr_str(player, thing, flag)))
    {
      notify(player, "Sorry that isn't a valid attribute.");
      return;
    }
    /* if ( (attr->flags & AF_WIZARD) && !((attr->obj ==
       NOTHING)?power(player,
       POW_WATTR):controls(player,attr->obj,POW_WATTR))) {
       notify(player,"Sorry only Administrators can change that."); return; } 

     */
    if (!can_set_atr(player, thing, attr))
    {
      notify(player, "You can't set that attribute.");
      return;
    }
    if (attr == A_ALIAS && (Typeof(thing) != TYPE_PLAYER))
    {
      notify(player, "Sorry, only players can have aliases using @alias.");
      return;
    }
    if (attr == A_ALIAS && (!ok_player_name(thing, db[thing].name, p)))
    {
      notify(player, tprintf("You can't set %s's alias to that.", db[thing].name));
      return;
    }

    if (attr == A_NICE)
    {
/*      notify(player, tprintf("atoi(p) = %d", atoi(p))); */
      if ((atoi(p) > 20) || (atoi(p) < -20))
      {
        notify(player, "@nice: Bad value (must be between -20 and 20).");
        return;
      } 
      if ((atoi(p) < 0) && !power(player, POW_SECURITY) )
      {
        notify(player, "@nice: Sorry, You lack the power.");
        return;
      }
    }

    if (db[db[thing].owner].i_flags & I_QUOTAFULL && strlen(p) > strlen(atr_get(thing, attr)) && !(attr->flags & AF_NOMEM))
    {
      notify(player, "Your quota has run out.");
      return;
    }
    if (attr->flags & AF_LOCK)
    {
      if ((q = process_lock(player, p)))
      {
	db[thing].mod_time = now;
	atr_add(thing, attr, q);
	if (*q)
	  notify(player, "Locked.");
	else
	  notify(player, "Unlocked.");
      }
      return;
    }
    if (attr == A_ALIAS)
      delete_player(thing);

    mark_hearing(thing);
    if (!allow_commands && (*p == '!' || *p == '$'))
    {
      /* urgh. fix it. */
      char *x;

      x = stack_em(strlen(p) + 2);
      strcpy(x, "_");
      strcat(x, p);
      p = x;
    }
    db[thing].mod_time = now;
    atr_add(thing, attr, p);

    if (attr == A_ALIAS)
      add_player(thing);

    if (!(db[player].flags & QUIET))
      notify(player, tprintf("%s - Set.", db[thing].cname));
    check_hearing();
    return;
  }

  /* move p past NOT_TOKEN if present */
  for (p = flag; *p && (*p == NOT_TOKEN || isspace(*p)); p++)
    ;

  /* identify flag */
  if (*p == '\0')
  {
    notify(player, "You must specify a flag to set.");
    return;
  }

  f = 0;
  switch (Typeof(thing))
  {
  case TYPE_THING:
    if (string_prefix("KEY", p))
      f = THING_KEY;
    if (string_prefix("DESTROY_OK", p))
      f = THING_DEST_OK;
    if (string_prefix("LIGHT", p))
      f = THING_LIGHT;
    /* if ( string_prefix("ROBOT",p) ) f=THING_ROBOT; */
    if (string_prefix("X_OK", p))
      f = THING_SACROK;
    break;
  case TYPE_PLAYER:
/* get rid of newbie flag, it's stupid
    if (string_prefix("NEWBIE", p) || string_prefix("NOVICE", p))
      f = PLAYER_NEWBIE;
*/
    if (string_prefix("SLAVE", p))
      f = PLAYER_SLAVE;
    if (string_prefix("TERSE", p))
      f = PLAYER_TERSE;
    if (string_prefix("MORTAL", p))
      f = PLAYER_MORTAL;
    if (string_prefix("NO_WALLS", p))
      f = PLAYER_NO_WALLS;
    if (string_prefix("ANSI", p))
      f = PLAYER_ANSI;
    if (string_prefix("NOBEEP", p))
      f = PLAYER_NOBEEP;
/*  replaced with @prefix / @suffix
    if (string_prefix("WHEN", p))
      f = PLAYER_WHEN;
*/
    if (string_prefix("FREEZE", p))
      f = PLAYER_FREEZE;
    if (string_prefix("SUSPECT", p))
      if (db[player].pows[0] == CLASS_DIR)
        f = PLAYER_SUSPECT;
    break;
  case TYPE_ROOM:
    if (string_prefix("ABODE", p))
      f = ROOM_JUMP_OK;
    if (string_prefix("AUDITORIUM", p))
      f = ROOM_AUDITORIUM;
    if (string_prefix("JUMP_OK", p))
      f = ROOM_JUMP_OK;
    if (string_prefix("FLOATING", p))
      f = ROOM_FLOATING;
    break;
  case TYPE_EXIT:
    if (string_prefix("LIGHT", p))
      f = EXIT_LIGHT;
    if (string_prefix("TRANSPARENT", p))
      f = OPAQUE;
    break;
  }
  if (!f)
  {
    if (string_prefix("GOING", p))
    {
      if (player != root || Typeof(thing) == TYPE_PLAYER)
      {
	notify(player, "I think the @[un]destroy command is more what you're looking for.");
	return;
      }
      else
      {
	notify(player, "I hope you know what you're doing.");
	f = GOING;
      }
    }
    else if (string_prefix("BEARING", p))
      f = BEARING;
    else if (string_prefix("LINK_OK", p))
      f = LINK_OK;
    else if (string_prefix("QUIET", p))
      f = QUIET;
    else if (string_prefix("DARK", p))
      f = DARK;
    else if (string_prefix("DEBUG", p))
      f = DARK;
    else if (string_prefix("STICKY", p))
      f = STICKY;
    else if (string_prefix("PUPPET", p))
      f = PUPPET;
    else if (string_prefix("INHERIT", p))
      f = INHERIT_POWERS;
    else if (string_prefix("ENTER_OK", p))
      f = ENTER_OK;
    else if (string_prefix("CHOWN_OK", p))
      f = CHOWN_OK;
    else if (string_prefix("SEE_OK", p))
    {
      notify(player, "Warning: the see_ok flag has been renamed to 'visible'");
      f = SEE_OK;
    }
    else if (string_prefix("VISIBLE", p))
      f = SEE_OK;
    else if (string_prefix("OPAQUE", p))
      f = OPAQUE;
    else if (string_prefix("HAVEN", p) || string_prefix("HALTED", p))
      f = HAVEN;
    else
    {
      notify(player, "I don't recognize that flag.");
      return;
    }
  }

  if (f == BEARING && *flag == NOT_TOKEN)
  {
    int i;

    for (i = 0; db[thing].children && db[thing].children[i] != NOTHING; i++)
    {
      if (db[db[thing].children[i]].owner != db[player].owner)
      {
	if (!controls(player, db[thing].children[i], POW_MODIFY))
	{
	  notify(player,
		 tprintf("Sorry, you don't control its child, %s.",
			 unparse_object(player, db[thing].children[i])));
	  return;
	}
	else if (db[db[thing].children[i]].owner != db[thing].owner)
	  notify(player,
		 tprintf("Warning: you are locking in %s as a child.",
			 unparse_object(player, db[thing].children[i])));
      }
    }
  }
  /* check for restricted flag */
/* new spoof protection installed.
  if (f == HAVEN && *flag == NOT_TOKEN)
  {
    dbref p;

    p = starts_with_player(db[thing].name);
    if (p != NOTHING && !controls(player, p, POW_SPOOF))
    {
      notify(player, "Sorry, a player holds that name.");
      return;
    }
  }
*/

  if (Typeof(thing) == TYPE_PLAYER && f == PLAYER_SLAVE)
  {
    if (!has_pow(player, thing, POW_SLAVE) || db[player].owner == thing)
    {
      notify(player, "You can't enslave/unslave that!");
      return;
    }
    log_important(tprintf("%s %sslaved %s", unparse_object_a(player, player),
       (*flag == NOT_TOKEN) ? "un" : "en", unparse_object_a(thing, thing)));
  }
  else if (!controls(player, thing, POW_MODIFY))
  {
    notify(player, perm_denied());
    return;
  }

  if (Typeof(thing) == TYPE_PLAYER && f == PLAYER_FREEZE)
  {
    if (!has_pow(player, thing, POW_SLAVE) || db[player].owner == thing)
    {
      notify(player, "You can't freeze/unfreeze that!");
      return;
    }
    log_important(tprintf("%s %sfroze %s", unparse_object_a(player, player),
	 (*flag == NOT_TOKEN) ? "un" : "", unparse_object_a(thing, thing)));
  }
  else if (!controls(player, thing, POW_MODIFY))
  {
    notify(player, perm_denied());
    return;
  }

  if (!controls(player, db[thing].owner, POW_SECURITY)
      && f == INHERIT_POWERS)
  {
    notify(player, "Sorry, you cannot do that.");
    return;
  }
  /* else everything is ok, do the set */
  if (*flag == NOT_TOKEN)
  {
    /* reset the flag */
    db[thing].flags &= ~f;
    notify(player, "Flag reset.");
    if ((f == PUPPET) && her && !Hearer(thing))
      notify_in(db[thing].location, thing, tprintf
		("%s loses its ears and becomes deaf.", db[thing].name));
  }
  else
  {
    /* set the flag */
    db[thing].flags |= f;
    if ((f == PUPPET) && !her)
    {
      char *buff;

      buff = tprintf("%s grows ears and can now hear.",
		     db[thing].name);
      notify_in(db[thing].location, thing, buff);
    }
    notify(player, "Flag set.");
  }
}

/* check for abbreviated set command */
int test_set(player, command, arg1, arg2, is_direct)
dbref player;
char *command;
char *arg1;
char *arg2;
int is_direct;
{
  extern ATTR *builtin_atr_str P((char *));
  ATTR *a;
  char buff[2000];

  if (command[0] != '@')
    return (0);
  if (!(a = builtin_atr_str(command + 1)))
  {
    init_match(player, arg1, NOTYPE);
    match_everything();
    if (match_result() != NOTHING && match_result() != AMBIGUOUS)
    {
      a = atr_str(player, match_result(), command + 1);
      if (a)
      {
	sprintf(buff, "%s:%s", command + 1, arg2);
	do_set(player, arg1, buff, is_direct);
	return (1);
      }
    }
  }
  else if (!(a->flags & AF_NOMOD))
  {
    sprintf(buff, "%s:%s", command + 1, arg2);
    do_set(player, arg1, buff, is_direct);
    return 1;
  }
  return (0);
}

int parse_attrib(player, s, thing, atr, withpow)
dbref player;
char *s;
dbref *thing;
ATTR **atr;
int withpow;
{
  char buff[1024];

  strncpy(buff, s, 1023);
  buff[1023] = '\0';
  /* get name up to / */
  for (s = buff; *s && (*s != '/'); s++) ;
  if (!*s)
    return (0);
  *s++ = 0;
  if (withpow != 0)
  {
    if ((*thing = match_controlled(player, buff, withpow)) == NOTHING)
      return 0;
  }
  else
  {
    init_match(player, buff, NOTYPE);
    match_everything();
    if ((*thing = match_result()) == NOTHING)
      return 0;
  }

  /* rest is attrib name */
  if (!((*atr) = atr_str(player, *thing, s)))
    return (0);
  if (withpow != 0)
    if (((*atr)->flags & AF_DARK) ||
      (!controls(player, *thing, POW_SEEATR) && !((*atr)->flags & AF_OSEE)))
      return (0);
  return (1);
}

void do_edit(player, it, argv)
dbref player;
char *it;
char *argv[];
{
  dbref thing;
  int d, len;
  char *r, *s, *val;
  char dest[1024];
  ATTR *attr;

  if (!parse_attrib(player, it, &thing, &attr, POW_MODIFY))
  {
    notify(player, "No match.");
    return;
  }
  if (!attr)
  {
    notify(player, "Gack! Don't do that. it makes me uncomfortable.");
    return;
  }
  if ((attr->flags & AF_WIZARD) && !power(player, POW_WATTR))
  {
    notify(player, "Eeg! Tryin to edit a admin-only prop? hrm. don't do it.");
    return;
  }
  if (!controls(player, thing, POW_MODIFY))
  {
    notify(player, perm_denied());
    return;
  }
  if (attr == A_ALIAS)
  {
    notify(player, "To set an alias, do @alias me=<new alias>. Don't use @edit.");
    return;
  }
  if (!argv[1] || !*argv[1])
  {
    notify(player, "Nothing to do.");
    return;
  }
  val = argv[1];
  r = (argv[2]) ? argv[2] : "";
  /* replace all occurances of val with r */
  s = atr_get(thing, attr);
  len = strlen(val);
  for (d = 0; (d < 1000) && *s;)
    if (strncmp(val, s, len) == 0)
    {
      if ((d + strlen(r)) < 1000)
      {
	strcpy(dest + d, r);
	d += strlen(r);
	s += len;
      }
      else
	dest[d++] = *s++;
    }
    else
      dest[d++] = *s++;
  dest[d++] = 0;
  if (db[db[thing].owner].i_flags & I_QUOTAFULL && strlen(dest) > strlen(atr_get(thing, attr)))
  {
    notify(player, "Your quota has run out.");
    return;
  }
  atr_add(thing, attr, dest);
  if (!(db[player].flags & QUIET))
  {
    notify(player, "Set.");
    do_examine(player, it, 0);
  }
}

void do_hide(player)
dbref player;
{
  atr_add((Typeof(player) == TYPE_PLAYER) ? player : db[player].owner, A_LHIDE, "me&!me");
  if (Typeof(player) == TYPE_PLAYER)
    notify(player, "Your name is HIDDEN.");
  else
    notify(player, "Your owner's name is HIDDEN.");
  return;
}

void do_unhide(play)
dbref play;
{
  atr_add((Typeof(play) == TYPE_PLAYER) ? play : db[play].owner, A_LHIDE, "");
  if (Typeof(play) == TYPE_PLAYER)
    notify(play, "Your name is back on the WHO list.");
  else
    notify(play, "Your owner's name is back on the WHO list.");
  return;
}

void do_haven(player, haven)
dbref player;
char *haven;
{
  if (*haven == '?')
  {
    if (*atr_get(player, A_HAVEN))
    {
      notify(player, tprintf("Your Haven message is: %s",
			     atr_get(player, A_HAVEN)));
      return;
    }
    else
    {
      notify(player, "You have no Haven message.");
      return;
    }
  }

  if (*haven == '\0')
  {
    atr_clr(player, A_HAVEN);
    notify(player, "Haven message removed.");
    return;
  }

  atr_add(player, A_HAVEN, haven);
  notify(player, tprintf("Haven message set as: %s", haven));
}

void do_idle(player, idle)
dbref player;
char *idle;
{
  if (*idle == '?')
  {
    if (*Idle(player))
    {
      notify(player, tprintf("Your Idle message is: %s",
			     atr_get(player, A_IDLE)));
      return;
    }
    else
    {
      notify(player, "You have no Idle message.");
      return;
    }
  }

  if (*idle == '\0')
  {
    atr_clr(player, A_IDLE);
    notify(player, "Idle message removed.");
    return;
  }

  atr_add(player, A_IDLE, idle);
  notify(player, tprintf("Idle message set as: %s", idle));

}

void do_away(player, away)
dbref player;
char *away;
{
  if (*away == '?')
  {
    if (*Away(player))
    {
      notify(player, tprintf("Your Away message is: %s",
			     atr_get(player, A_AWAY)));
      return;
    }
    else
    {
      notify(player, "You have no Away message.");
      return;
    }
  }

  if (*away == '\0')
  {
    atr_clr(player, A_AWAY);
    notify(player, "Away message removed.");
    return;
  }
  atr_add(player, A_AWAY, away);
  notify(player, tprintf("Away message set as: %s", away));
}


void set_idle_command(dbref player, char *arg1, char *arg2)
{
  dbref target;
  time_t time = -1;

  if (arg2 && *arg2)
  {
    target = lookup_player(arg1);
    if (target == NOTHING)
    {
      set_idle(player, player, time, tprintf("%s = %s", arg1, arg2));
    }
    else
    {
      set_idle(target, player, time, arg2);
    }
  }
  else
  {
    set_idle(player, player, time, arg1);
  }
}

void set_idle(player, cause, time, msg)
dbref player;
dbref cause;
time_t time;
char *msg;
{
  char buf[4096];  /* big, in case someone's a retard. */
  char *buf2;


  if(is_pasting(player))
  {
    add_more_paste(player, "@pasteabort");
  }

  sprintf(buf,"%s idled ",db[player].name);


  if (cause == -1)
  {
    strcat(buf,tprintf("after %ld minutes inactivity",time));
    if(strlen(atr_get(player, A_IDLE)) > 0)
      atr_add(player, A_IDLE_CUR, atr_get(player, A_IDLE));
    else
      atr_add(player, A_IDLE_CUR, "inactivity idle - no default idle message.");
  }
  else if ( cause != player && (!controls(cause, player, POW_MODIFY) && !power(cause, POW_MODIFY)))
  {
    notify(cause, perm_denied());
    return;
  }
  else if (cause == player)
  {
    strcat(buf,"manually");
  }
  else
  {
    if(atr_get(player, A_IDLE))
    {
      strcat(buf,tprintf("- set by %s",db[cause].name));
    }
  }

  if (msg && *msg)
  {
    if(msg && *msg && (strlen(msg) > 512))
    {
      msg[512] = '\0';
      notify(player,"Idle message truncated.");
    }
    strcat(buf,tprintf(" (%s)",msg));
    atr_add(player, A_IDLE_CUR, msg);
  }
  else
    if(strlen(atr_get(player, A_IDLE)) > 0)
      atr_add(player, A_IDLE_CUR, atr_get(player, A_IDLE));
    else
      atr_add(player, A_IDLE_CUR, "");

  if ((strlen(atr_get(player, A_BLACKLIST))) || (strlen(atr_get(player, A_LHIDE))))
  {
    buf2 = tprintf("|R+(||R!+HIDDEN||R+)| %s", buf);
  }
  else
  {
    buf2 = tprintf("%s",buf);
  }

  log_io(buf2);
  com_send_as_hidden("pub_io",buf2,player);
  db[player].flags |= PLAYER_IDLE;
  did_it(player, player, NULL, 0, NULL, 0, A_AIDLE);
  return;
}


void set_unidle(player,lasttime)
dbref player;
time_t lasttime;
{

  long unidle_time;

  check_newday();

  if(player <= 0 || player > db_top)
  {
    log_io(tprintf("problem with set_unidle -- player = %d lasttime = %ld",(int)player, lasttime));
    return;
  }

/* if lasttime is MAXINT, we supress the message. */
  if(lasttime != MAXINT)
  {
    char *buf, *buf2;
    unidle_time = now - lasttime;
    db[player].flags &= ~PLAYER_IDLE;

    

    if(unidle_time)
      buf = tprintf("%s unidled after %s.", unparse_object(player, player), time_format_4(unidle_time));
    else
      buf = tprintf("%s unidled immediately. duh.", unparse_object(player, player));

    if ((strlen(atr_get(player, A_BLACKLIST))) || (strlen(atr_get(player, A_LHIDE))))
    {
      buf2 = tprintf("|R+(||R!+HIDDEN||R+)| %s", buf);
    }
    else
    {
      buf2 = buf;
    }

    log_io(buf2);
    com_send_as_hidden("pub_io",buf2,player);

  }

  /* check to see if "idle" is a command in the A_UNIDLE. */

  {
    char *aunidle;
    size_t x;

    aunidle = tprintf("%s", atr_get(player, A_AUNIDLE));

    for (x=0; x < strlen(aunidle); x++)
    {
      aunidle[x] = to_lower(aunidle[x]);
    }

    if (!strstr(aunidle, "idle"))
    {
      did_it_now(player, player, NULL, 0, NULL, 0, A_AUNIDLE);
    }
    else
    {
      notify(player, "idle found in AUNIDLE string. AUNIDLE ignored.");
    }
  } 

  if(lasttime != MAXINT)
  {
    if ((check_mail_internal(player, "")) > 0)
      check_mail(player, "");
  }
  return;
}

