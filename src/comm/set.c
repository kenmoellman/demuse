/* set.c */
/* $Id: set.c,v 1.23 1994/01/26 22:28:27 nils Exp $ */
  
/* commands which set parameters */
#include <stdio.h>
#include <ctype.h>
#include <values.h>
#include <string.h>

#include "db.h"
#include "config.h"
#include "match.h"
#include "interface.h"
#include "externs.h"
#include "credits.h"

/* Invalid name prefix checking structure */
struct invalid_prefix {
  const char *prefix;
  int case_sensitive;  /* 1 = case-sensitive, 0 = case-insensitive */
};

static const struct invalid_prefix invalid_name_prefixes[] = {
  {"HTTP:", 0},  /* Case-insensitive check for HTTP: */
  {NULL, 0}      /* Sentinel */
};

/* Helper function for case-insensitive comparison */
static int strncasecmp_safe(const char *s1, const char *s2, size_t n)
{
  if (!s1 || !s2) return -1;
  
  while (n > 0 && *s1 && *s2) {
    char c1 = to_lower(*s1);
    char c2 = to_lower(*s2);
    if (c1 != c2) return c1 - c2;
    s1++;
    s2++;
    n--;
  }
  if (n == 0) return 0;
  return *s1 - *s2;
}

/* Check if name starts with any invalid prefix */
static int has_invalid_prefix(const char *name)
{
  int i;
  if (!name) return 1;
  
  for (i = 0; invalid_name_prefixes[i].prefix != NULL; i++) {
    size_t prefix_len = strlen(invalid_name_prefixes[i].prefix);
    int matches;
    
    if (invalid_name_prefixes[i].case_sensitive) {
      matches = (strncmp(name, invalid_name_prefixes[i].prefix, prefix_len) == 0);
    } else {
      matches = (strncasecmp_safe(name, invalid_name_prefixes[i].prefix, prefix_len) == 0);
    }
    
    if (matches) return 1;
  }
  return 0;
}

void do_destroy(dbref player, char *name)
{
  dbref thing;

  if (!name) {
    notify(player, "Destroy what?");
    return;
  }

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
    char *k;

    k = atr_get(thing, A_DOOMSDAY);
    if (k && *k)
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
      if (k && *k)
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

void destroy_obj(dbref obj, int no_seconds)
{
  if (!(db[obj].flags & QUIET))
    do_pose(obj, "shakes and starts to crumble", "", 0);
  atr_add(obj, A_DOOMSDAY, int_to_str(no_seconds + now));
  db[obj].flags |= GOING;
  do_halt(obj, "", "");
}

extern time_t muse_up_time;

void do_cname(dbref player, char *name, char *cname)
{
  dbref thing;

  if (!name || !cname) {
    notify(player, "Invalid parameters.");
    return;
  }

  if ((thing = match_controlled(player, name, POW_MODIFY)) != NOTHING)
  {
    if (Typeof(thing) == TYPE_EXIT)
    {
      char buf[2048];
      char *visname, *rest;

      /* Safe copy with null termination */
      if (db[thing].name) {
        strncpy(buf, db[thing].name, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
      } else {
        buf[0] = '\0';
      }
      
      visname = buf;
      rest = strchr(buf, ';');

      if (rest && *rest)
	*rest++ = '\0';
      else
	rest = "";

      if (strcmp(visname, strip_color(cname)) != 0)
      {
	notify(player, "Colorized name of exits must match visible name (the name before the first ';').");
	return;
      }
      else
      {
	char buf2[2048];

	snprintf(buf2, sizeof(buf2), "%s;%s", cname, rest);
	notify(player, tprintf("Okay, %s's colorized name is now %s.",
			       db[thing].cname ? db[thing].cname : "it", buf2));
	SET(db[thing].cname, buf2);
      }
    }
    else
    {
      if (!db[thing].name || strcmp(db[thing].name, strip_color(cname)) != 0)
      {
	notify(player, "Hey! Colorized name doesn't match real name!");
	return;
      }
      else
      {
	if (Typeof(thing) == TYPE_PLAYER)
	  log_important(tprintf("|G+COLOR CHANGE|: %s to %s",
				db[thing].cname ? db[thing].cname : "(null)", cname));
	notify(player, tprintf("Okay, %s's colorized name is now %s.",
			       db[thing].cname ? db[thing].cname : "it", cname));
	SET(db[thing].cname, cname);
      }
    }
  }
}

void do_name(dbref player, char *name, char *cname, int is_direct)
{
  dbref thing;
  char *password;
  char *newname;
  
  if (!name || !cname) {
    notify(player, "Invalid parameters.");
    return;
  }

  newname = strip_color_nobeep(cname);
  if (!newname) {
    notify(player, "Invalid name.");
    return;
  }
  
  if ((password = strrchr(cname, ' ')))
  {
    if (password && *password)
    {
      *password++ = '\0';
    }
  }
  
  if (db[player].name && strncmp(db[player].name, strip_color_nobeep(cname), strlen(db[player].name)+2) == 0)
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

    /* Check for invalid prefixes using new system */
    if (has_invalid_prefix(newname))
    {
      notify(player, "That name is not allowed.");
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
				muse_name ? muse_name : "MUSE"));
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
				guest_prefix ? guest_prefix : "Guest"));
	return;
      }

      if (string_prefix(newname, guest_alias_prefix) &&
	  isdigit(*(newname + strlen(guest_alias_prefix))))
      {
	notify(player, tprintf(
	     "Only guests may have names beginning with '%s' and a number.",
				guest_alias_prefix ? guest_alias_prefix : "Guest"));
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
      notify_in(db[thing].location, thing, tprintf("%s is now known as %s.", 
                                                    db[thing].name ? db[thing].name : "Someone", 
                                                    cname));
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
      notify_in(db[thing].location, thing, tprintf("%s is now known as %s.", 
                                                    db[thing].name ? db[thing].name : "Something",
                                                    newname));
    SET(db[thing].name, newname);
    SET(db[thing].cname, newname);
    notify(player, "Name set.");
  }
}

void do_describe(dbref player, char *name, char *description)
{
  dbref thing;

  if (!name) {
    notify(player, "Describe what?");
    return;
  }

  if ((thing = match_controlled(player, name, POW_MODIFY)) != NOTHING)
  {
    s_Desc(thing, description ? description : "");
    notify(player, "Description set.");
  }
}

void do_unlink(dbref player, char *name)
{
  dbref exit;

  if (!name) {
    notify(player, "Unlink what?");
    return;
  }

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

void do_chown(dbref player, char *name, char *newobj)
{
  dbref thing;
  dbref owner;

  if (!name) {
    notify(player, "Chown what?");
    return;
  }

  log_important(tprintf("%s attempts: @chown %s=%s",
			unparse_object_a(player, player), name, newobj ? newobj : ""));

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
  if (!newobj || !*newobj || !string_compare(newobj, "me"))
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

void mark_hearing(dbref obj)
{
  int i;
  struct hearing *mine;

  mine = malloc(sizeof(struct hearing));
  if (!mine) {
    return;  /* Memory allocation failed */
  }

  mine->next = hearing_list;
  mine->did_hear = Hearer(obj);
  mine->obj = obj;

  hearing_list = mine;
  for (i = 0; db[obj].children && db[obj].children[i] != NOTHING; i++)
  {
    mark_hearing(db[obj].children[i]);
  }
}

void check_hearing(void)
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
		tprintf("%s grows ears and can now hear.", db[obj].name ? db[obj].name : "Something"));
    if (mine->did_hear && !now_hear)
      notify_in(db[obj].location, obj,
		tprintf("%s loses its ears and is now deaf.", db[obj].name ? db[obj].name : "Something"));
    free(mine);
  }
}

void do_unlock(dbref player, char *name)
{
  dbref thing;
  ATTR *attr = A_LOCK;

  if (!name) {
    notify(player, "Unlock what?");
    return;
  }

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

void do_set(dbref player, char *name, char *flag, int allow_commands)
{
  dbref thing;
  char *p, *q;
  object_flag_type f;
  int her;
  int nice_value;  /* Fix #7: Store atoi result once */

  if (!name || !flag) {
    notify(player, "Invalid parameters.");
    return;
  }

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
      notify(player, tprintf("You can't set %s's alias to that.", 
                             db[thing].name ? db[thing].name : "that player"));
      return;
    }

    if (attr == A_NICE)
    {
      /* Fix #7: Evaluate atoi() once and store result */
      nice_value = atoi(p);
      
      if ((nice_value > 20) || (nice_value < -20))
      {
        notify(player, "@nice: Bad value (must be between -20 and 20).");
        return;
      } 
      if ((nice_value < 0) && !power(player, POW_SECURITY))
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
      if (x) {
        snprintf(x, strlen(p) + 2, "_%s", p);
        p = x;
      }
    }
    db[thing].mod_time = now;
    atr_add(thing, attr, p);

    if (attr == A_ALIAS)
      add_player(thing);

    if (!(db[player].flags & QUIET))
      notify(player, tprintf("%s - Set.", db[thing].cname ? db[thing].cname : "Object"));
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
    if (string_prefix("X_OK", p))
      f = THING_SACROK;
    break;
  case TYPE_PLAYER:
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
    if (string_prefix("FREEZE", p))
      f = PLAYER_FREEZE;
    if (string_prefix("SUSPECT", p))
      if (db[player].pows && db[player].pows[0] == CLASS_DIR)
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
		("%s loses its ears and becomes deaf.", db[thing].name ? db[thing].name : "Something"));
  }
  else
  {
    /* set the flag */
    db[thing].flags |= f;
    if ((f == PUPPET) && !her)
    {
      char *buff;

      buff = tprintf("%s grows ears and can now hear.",
		     db[thing].name ? db[thing].name : "Something");
      notify_in(db[thing].location, thing, buff);
    }
    notify(player, "Flag set.");
  }
}

/* check for abbreviated set command */
int test_set(dbref player, char *command, char *arg1, char *arg2, int is_direct)
{
  extern ATTR *builtin_atr_str(char *);
  ATTR *a;
  char buff[2000];

  if (!command || command[0] != '@')
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
	snprintf(buff, sizeof(buff), "%s:%s", command + 1, arg2 ? arg2 : "");
	do_set(player, arg1, buff, is_direct);
	return (1);
      }
    }
  }
  else if (!(a->flags & AF_NOMOD))
  {
    snprintf(buff, sizeof(buff), "%s:%s", command + 1, arg2 ? arg2 : "");
    do_set(player, arg1, buff, is_direct);
    return 1;
  }
  return (0);
}

int parse_attrib(dbref player, char *s, dbref *thing, ATTR **atr, int withpow)
{
  char buff[1024];

  if (!s) return 0;

  strncpy(buff, s, sizeof(buff) - 1);
  buff[sizeof(buff) - 1] = '\0';
  
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

void do_edit(dbref player, char *it, char *argv[])
{
  dbref thing;
  int d, len;
  char *r, *s, *val;
  char dest[2048];  /* Fix #3: Increased to 2048 to match check */
  ATTR *attr;

  if (!it) {
    notify(player, "Edit what?");
    return;
  }

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
  if (!s) s = "";
  
  len = strlen(val);
  
  /* Fix #3: Use consistent bounds checking with buffer size */
  for (d = 0; (d < (int)(sizeof(dest) - 1)) && *s;)
  {
    if (strncmp(val, s, len) == 0)
    {
      /* Check if replacement will fit */
      if ((d + strlen(r)) < (sizeof(dest) - 1))
      {
        /* Safe to copy replacement string */
        size_t r_len = strlen(r);
        strncpy(dest + d, r, sizeof(dest) - d - 1);
        d += r_len;
        s += len;
      }
      else
      {
        /* Would overflow, copy what we can and break */
        if (d < (int)(sizeof(dest) - 1))
          dest[d++] = *s++;
        else
          break;
      }
    }
    else
      dest[d++] = *s++;
  }
  dest[d] = '\0';
  
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

void do_hide(dbref player)
{
  atr_add((Typeof(player) == TYPE_PLAYER) ? player : db[player].owner, A_LHIDE, "me&!me");
  if (Typeof(player) == TYPE_PLAYER)
    notify(player, "Your name is HIDDEN.");
  else
    notify(player, "Your owner's name is HIDDEN.");
  return;
}

void do_unhide(dbref play)
{
  atr_add((Typeof(play) == TYPE_PLAYER) ? play : db[play].owner, A_LHIDE, "");
  if (Typeof(play) == TYPE_PLAYER)
    notify(play, "Your name is back on the WHO list.");
  else
    notify(play, "Your owner's name is back on the WHO list.");
  return;
}

void do_haven(dbref player, char *haven)
{
  if (!haven) {
    notify(player, "Haven what?");
    return;
  }

  if (*haven == '?')
  {
    char *current_haven = atr_get(player, A_HAVEN);
    if (current_haven && *current_haven)
    {
      notify(player, tprintf("Your Haven message is: %s", current_haven));
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

void do_idle(dbref player, char *idle)
{
  if (!idle) {
    notify(player, "Idle what?");
    return;
  }

  if (*idle == '?')
  {
    char *current_idle = Idle(player);
    if (current_idle && *current_idle)
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

void do_away(dbref player, char *away)
{
  if (!away) {
    notify(player, "Away what?");
    return;
  }

  if (*away == '?')
  {
    char *current_away = Away(player);
    if (current_away && *current_away)
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

void set_idle(dbref player, dbref cause, time_t time, char *msg)
{
  char buf[8192];  /* Fix #1: Increased size and using snprintf */
  char *buf2;
  size_t buf_remaining;
  int result;

  /* Fix #6: Add null checks */
  if (player < 0 || player >= db_top || !db[player].name) {
    return;
  }

  if (is_pasting(player))
  {
    add_more_paste(player, "@pasteabort");
  }

  /* Fix #1: Use snprintf for safe string building */
  result = snprintf(buf, sizeof(buf), "%s idled ", db[player].name);
  if (result < 0 || result >= (int)sizeof(buf)) {
    notify(player, "Error setting idle status.");
    return;
  }
  buf_remaining = sizeof(buf) - result;

  if (cause == -1)
  {
    char *temp = tprintf("after %ld minutes inactivity", time);
    result = snprintf(buf + strlen(buf), buf_remaining, "%s", temp);
    if (result < 0 || result >= (int)buf_remaining) {
      notify(player, "Error setting idle status.");
      return;
    }
    
    if (strlen(atr_get(player, A_IDLE)) > 0)
      atr_add(player, A_IDLE_CUR, atr_get(player, A_IDLE));
    else
      atr_add(player, A_IDLE_CUR, "inactivity idle - no default idle message.");
  }
  else if (cause != player && (!controls(cause, player, POW_MODIFY) && !power(cause, POW_MODIFY)))
  {
    notify(cause, perm_denied());
    return;
  }
  else if (cause == player)
  {
    result = snprintf(buf + strlen(buf), buf_remaining, "manually");
    if (result < 0) return;
  }
  else
  {
    if (atr_get(player, A_IDLE))
    {
      char *temp = tprintf("- set by %s", db[cause].name ? db[cause].name : "someone");
      result = snprintf(buf + strlen(buf), buf_remaining, "%s", temp);
      if (result < 0) return;
    }
  }

  if (msg && *msg)
  {
    /* Truncate message if too long */
    char truncated_msg[513];
    if (strlen(msg) > 512)
    {
      strncpy(truncated_msg, msg, 512);
      truncated_msg[512] = '\0';
      msg = truncated_msg;
      notify(player, "Idle message truncated.");
    }
    
    char *temp = tprintf(" (%s)", msg);
    buf_remaining = sizeof(buf) - strlen(buf);
    result = snprintf(buf + strlen(buf), buf_remaining, "%s", temp);
    if (result < 0 || result >= (int)buf_remaining) {
      /* Message too long, truncate */
      buf[sizeof(buf) - 1] = '\0';
    }
    atr_add(player, A_IDLE_CUR, msg);
  }
  else
  {
    if (strlen(atr_get(player, A_IDLE)) > 0)
      atr_add(player, A_IDLE_CUR, atr_get(player, A_IDLE));
    else
      atr_add(player, A_IDLE_CUR, "");
  }

  if ((strlen(atr_get(player, A_BLACKLIST))) || (strlen(atr_get(player, A_LHIDE))))
  {
    buf2 = tprintf("|R+(||R!+HIDDEN||R+)| %s", buf);
  }
  else
  {
    buf2 = tprintf("%s", buf);
  }

  log_io(buf2);
  com_send_as_hidden("pub_io", buf2, player);
  db[player].flags |= PLAYER_IDLE;
  did_it(player, player, NULL, 0, NULL, 0, A_AIDLE);
  return;
}


void set_unidle(dbref player, time_t lasttime)
{
  long unidle_time;
  static int in_unidle = 0;  /* Fix #5: Recursion prevention flag */

  /* Fix #5: Prevent recursion */
  if (in_unidle) {
    return;
  }

  check_newday();

  /* Fix #6: Add bounds checking */
  if (player <= 0 || player >= db_top)
  {
    log_io(tprintf("problem with set_unidle -- player = %d lasttime = %ld", (int)player, lasttime));
    return;
  }

  /* if lasttime is MAXINT, we suppress the message. */
  if (lasttime != MAXINT)
  {
    char *buf, *buf2;
    unidle_time = now - lasttime;
    db[player].flags &= ~PLAYER_IDLE;

    if (unidle_time)
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
    com_send_as_hidden("pub_io", buf2, player);
  }

  /* Fix #5: Set flag before executing A_AUNIDLE to prevent recursion */
  in_unidle = 1;
  did_it_now(player, player, NULL, 0, NULL, 0, A_AUNIDLE);
  in_unidle = 0;

  if (lasttime != MAXINT)
  {
    if ((check_mail_internal(player, "")) > 0)
      check_mail(player, "");
  }
  return;
}
