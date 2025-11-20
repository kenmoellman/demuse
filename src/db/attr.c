/* attr.c - Object Attribute Operations
 *
 * CONSOLIDATED (2025 reorganization): comm/set.c → db/attr.c
 * Attribute operations belong in db/ as they manipulate database attributes.
 *
 * Functions:
 * - do_set() - @set command (attributes and flags)
 * - test_set() - Check for abbreviated set command
 * - parse_attrib() - Parse object/attribute specification
 * - do_edit() - @edit command (attribute editing)
 * - do_haven() - @haven command (haven flag)
 */

/* $Id: set.c,v 1.23 1994/01/26 22:28:27 nils Exp $ */

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

extern time_t muse_up_time;

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
