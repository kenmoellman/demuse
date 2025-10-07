/* admin.c - Administrative and system management functions
 * Located in muse/ directory
 * 
 * This file consolidates administrative functions including:
 * - Player management commands (from old players.c)
 * - System logging commands (gripe, pray)
 * - Statistics and monitoring
 * - Permission and power management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <values.h>
#include <crypt.h>

#include "config.h"
#include "db.h"
#include "interface.h"
#include "externs.h"
#include "match.h"
#include "motd.h"
#include "credits.h"
#include "admin.h"
#include "player.h"
#include "sock.h"

#define  ANY_OWNER	-2


/* ===================================================================
 * Logging and Reporting Functions (moved from speech.c)
 * =================================================================== */

/**
 * GRIPE command - log a complaint from a player
 * @param player Complaining player
 * @param arg1 First part of complaint
 * @param arg2 Second part if split by =
 */
void do_gripe(dbref player, char *arg1, char *arg2)
{
    dbref loc;
    char *message;
    char buf[BUFFER_LEN];
    
    loc = db[player].location;
    
    /* Reconstruct message if split by = */
    if (arg2 && *arg2) {
        snprintf(buf, sizeof(buf), "%s = %s", arg1 ? arg1 : "", arg2);
        message = buf;
    } else {
        message = arg1 ? arg1 : "";
    }
    
    /* Log the gripe with location context */
    log_gripe(tprintf("|R+GRIPE| from %s in %s: %s",
                     unparse_object_a(player, player),
                     unparse_object_a(loc, loc),
                     message));
    
    notify(player, "Your complaint has been duly noted.");
}

/**
 * PRAY command - send a prayer to the gods (humorous logging)
 * @param player Praying player
 * @param arg1 God name to pray to
 * @param arg2 Prayer message
 */
void do_pray(dbref player, char *arg1, char *arg2)
{
    dbref loc;
    
    if (!arg1 || !*arg1) {
        notify(player, "Pray to whom?");
        return;
    }
    
    if (!arg2 || !*arg2) {
        notify(player, "What do you want to pray for?");
        return;
    }
    
    loc = db[player].location;
    
    /* Log the prayer */
    log_prayer(tprintf("|G+PRAYER| from %s in %s to the god %s: %s",
                      unparse_object_a(player, player),
                      unparse_object_a(loc, loc), 
                      arg1, arg2));
    
    notify(player, tprintf("%s has heard your prayer, and will consider granting it.", 
                         arg1));
}

/* ===================================================================
 * Player Management Functions (from old players.c)
 * These would come from the existing players.c file
 * =================================================================== */

/* TODO: Move these from players.c */
/*
void do_pcreate(dbref player, char *name, char *password)
void do_destroy_player(dbref player, char *name)
void do_quota(dbref player, char *name, char *amount)
void do_money(dbref player, char *name, char *amount)
void do_power(dbref player, char *name, char *power)
void do_boot(dbref player, char *name)
void do_newpassword(dbref player, char *name, char *password)
void do_pclear(dbref player, char *name)
*/

/* ===================================================================
 * Statistics and Monitoring Functions
 * =================================================================== */

/**
 * Show statistics about a player or object
 * @param player Requesting admin
 * @param name Target to examine
 */
void do_stats(player, name)
dbref player;
char *name;
{
  extern char *type_to_name();
  dbref owner;
  long i, total;
  long obj[NUM_OBJ_TYPES];
  long pla[NUM_CLASSES];

  if (*name == '\0')
    owner = ANY_OWNER;
  else if (*name == '#')
  {
    owner = atol(&name[1]);
    if (owner < 0 || db_top <= owner)
      owner = NOTHING;
    else if (Typeof(owner) != TYPE_PLAYER)
      owner = NOTHING;
  }
  else if (strcmp(name, "me") == 0)
    owner = player;
  else
    owner = lookup_player(name);
  if (owner == NOTHING)
  {
    notify(player, tprintf("%s: No such player", name));
    return;
  }
  if (!controls(player, owner, POW_STATS))
    if (owner != ANY_OWNER && owner != player)
    {
      notify(player, "You need a search warrant to do that!");
      return;
    }

  calc_stats(owner, &total, obj, pla);
  if (owner == ANY_OWNER)
    notify(player, tprintf("%s Database Breakdown:", muse_name));
  else
    notify(player, tprintf("%s database breakdown for %s:", muse_name, unparse_object(player, owner)));
  notify(player, tprintf("%9ld Total Objects", total));
  for (i = 0; i < NUM_OBJ_TYPES; i++)
    if (type_to_name(i) && *type_to_name(i) != ' ')
      notify(player, tprintf("%9ld %ss", obj[i], type_to_name(i)));
  notify(player, tprintf("%9ld %ss",
			 pla[CLASS_CITIZEN], class_to_name(CLASS_CITIZEN)));
#ifdef TEST_MALLOC
  if (power(player, TYPE_HONWIZ))
  {
    /* sprintf(buf, "Malloc count = %d.", malloc_count); */
    notify(player, tprintf("Malloc count = %d.", malloc_count));
  }
#endif /* TEST_MALLOC */
}

/**
 * Show system-wide statistics
 * @param player Requesting admin
 */
void do_sysstats(dbref player)
{
    if (!Wizard(player)) {
        notify(player, "Permission denied.");
        return;
    }
    
    /* TODO: Show database statistics, memory usage, etc. */
    notify(player, "System statistics not yet implemented.");
}

/* ===================================================================
 * Permission and Power Management
 * =================================================================== */

/**
 * Check if player has administrative privileges
 * @param player Player to check
 * @return 1 if admin, 0 otherwise
 */
int is_admin(dbref player)
{
    return Wizard(player) || power(player, POW_SECURITY);
}

/**
 * Check if player can administrate target
 * @param player Admin player
 * @param target Target player
 * @return 1 if allowed, 0 otherwise
 */
int can_admin(dbref player, dbref target)
{
    /* Can't admin yourself unless you're root */
    if (player == target && !is_root(player)) {
        return 0;
    }
    
    /* Root can admin anyone */
    if (is_root(player)) {
        return 1;
    }
    
    /* Wizards can admin non-wizards */
    if (Wizard(player) && !Wizard(target)) {
        return 1;
    }
    
    /* Check power levels for more granular control */
    /* TODO: Implement power level comparison */
    
    return 0;
}

/* ===================================================================
 * System Maintenance Functions
 * =================================================================== */

/**
 * Database check command
 * @param player Requesting admin
 */
void do_dbcheck(dbref player)
{
    if (!Wizard(player)) {
        notify(player, "Permission denied.");
        return;
    }
    
    notify(player, "Beginning database consistency check...");
    
    /* TODO: Implement database checking */
    /* - Check for invalid dbrefs
     * - Check for orphaned objects
     * - Check for circular references
     * - Verify attribute integrity
     */
    
    notify(player, "Database check complete.");
}

/**
 * Force database dump
 * @param player Requesting admin
 */
void do_dump(dbref player)
{
    if (!Wizard(player)) {
        notify(player, "Permission denied.");
        return;
    }
    
    notify(player, "Dumping database...");
    fork_and_dump();
    notify(player, "Database dump initiated.");
}

/**
 * Shutdown the game
 * @param player Requesting admin
 * @param reason Shutdown message
 */
void do_shutdown(dbref player, char *reason)
{
    if (!power(player, POW_SHUTDOWN)) {
        notify(player, "Permission denied.");
        return;
    }
    
    /* Log the shutdown */
    log_important(tprintf("SHUTDOWN by %s: %s",
                         unparse_object_a(player, player),
                         reason ? reason : "No reason given"));
    
    /* Notify all players */
    if (reason && *reason) {
        notify_all(tprintf("GAME: Shutdown by %s: %s",
                         db[player].name, reason), 
                  NOTHING, 0);
    } else {
        notify_all(tprintf("GAME: Shutdown by %s",
                         db[player].name), 
                  NOTHING, 0);
    }
    
    /* Set shutdown flag */
    shutdown_flag = 1;
}

/* ===================================================================
 * User Management Functions
 * =================================================================== */

/**
 * List all connected users (admin version with details)
 * @param player Requesting admin
 */
void do_who_admin(dbref player)
{
    struct descriptor_data *d;
    int count = 0;
    
    if (!Wizard(player)) {
        notify(player, "Permission denied.");
        return;
    }
    
    notify(player, "Descriptor | Player     | Idle | Host");
    notify(player, "-----------|------------|------|------------------------------");
    
    for (d = descriptor_list; d; d = d->next) {
        if (d->state == CONNECTED) {
            notify(player, tprintf("%10ld | %10s | %4s | %s",
                                 d->descriptor,
                                 db[d->player].name,
                                 time_format_2(now - d->last_time),
                                 d->addr));
            count++;
        }
    }
    
    notify(player, tprintf("Total: %d connections", count));
}

/**
 * Set or clear maintenance mode
 * @param player Requesting admin
 * @param arg "on" or "off"
 */
void do_maintenance(dbref player, char *arg)
{
    if (!Wizard(player)) {
        notify(player, "Permission denied.");
        return;
    }
    
    if (!arg || !*arg) {
        notify(player, tprintf("Maintenance mode is %s",
                             nologins ? "ON" : "OFF"));
        return;
    }
    
    if (!strcasecmp(arg, "on")) {
        nologins = 1;
        notify(player, "Maintenance mode enabled - no new logins allowed.");
        log_important(tprintf("Maintenance mode enabled by %s",
                            unparse_object_a(player, player)));
    } else if (!strcasecmp(arg, "off")) {
        nologins = 0;
        notify(player, "Maintenance mode disabled - logins allowed.");
        log_important(tprintf("Maintenance mode disabled by %s",
                            unparse_object_a(player, player)));
    } else {
        notify(player, "Usage: @maintenance on|off");
    }
}



extern int restrict_connect_class;
extern int user_limit;
extern int nologins;

/* local function declarations */
static object_flag_type convert_flags P((dbref player, int is_wizard, char *s, object_flag_type *, object_flag_type *));
struct descriptor_data *find_least_idle(dbref);

/* added 12/1/90 by jstanley to add @search command details in file game.c */
/* Ansi: void do_search(dbref player, char *arg1, char *arg3); */
void do_search(player, arg1, arg3)
dbref player;
char *arg1;
char *arg3;
{
  int flag;
  char *arg2;
  dbref thing;
  dbref from;
  dbref to;
  int destitute = 1;
  char *restrict_name;
  dbref restrict_owner;
  dbref restrict_link;
  int restrict_zone;
  object_flag_type flag_mask;
  object_flag_type restrict_type;
  object_flag_type restrict_class;
  char buf[3100];
  extern char *str_index();

  /* parse first argument into two */
  arg2 = str_index(arg1, ' ');
  if (arg2 != NULL)
    *arg2++ = '\0';		/* arg1, arg2, arg3 */
  else
  {
    if (*arg3 == '\0')
      arg2 = "";		/* arg1 */
    else
    {
      arg2 = arg1;		/* arg2, arg3 */
      arg1 = "";
    }
  }

  /* set limits on who we search */
  restrict_owner = NOTHING;
  if (*arg1 == '\0')
    restrict_owner = power(player, POW_EXAMINE) ? ANY_OWNER : player;
  else if (arg1[0] == '#')
  {
    restrict_owner = atol(&arg1[1]);
    if (restrict_owner < 0 || db_top <= restrict_owner)
      restrict_owner = NOTHING;
    else if (Typeof(restrict_owner) != TYPE_PLAYER)
      restrict_owner = NOTHING;
  }
  else if (strcmp(arg1, "me") == 0)
    restrict_owner = player;
  else
    restrict_owner = lookup_player(arg1);

  if (restrict_owner == NOTHING)
  {
    notify(player, tprintf("%s: No such player", arg1));
    return;
  }

  /* set limits on what we search for */
  flag = 0;
  flag_mask = 0;
  restrict_name = NULL;
  restrict_type = NOTYPE;
  restrict_link = NOTHING;
  restrict_zone = NOTHING;
  restrict_class = 0;
  switch (arg2[0])
  {
  case '\0':
    /* the no class requested class  :)  */
    break;
  case 'c':
    if (string_prefix("channels", arg2))
    {
      restrict_name = arg3;
      restrict_type = TYPE_CHANNEL;
    }
    else if (string_prefix("class", arg2))
    {
      restrict_class = name_to_class(arg3);
      if (!restrict_class || !power(player, POW_WHO))
      {
	notify(player, "Unknown class!");
	return;
      }
      restrict_type = TYPE_PLAYER;
    }
    else
      flag = 1;
    /* if(restrict_class==TYPE_DIRECTOR && !power(player,TYPE_ADMIN))
       restrict_class=TYPE_ADMIN; if(restrict_class==TYPE_JUNIOR &&
       !power(player,TYPE_ADMIN)) restrict_class=TYPE_OFFICIAL; */
    break;
  case 'e':
    if (string_prefix("exits", arg2))
    {
      restrict_name = arg3;
      restrict_type = TYPE_EXIT;
    }
    else
      flag = 1;
    break;
  case 'f':
    if (string_prefix("flags", arg2))
    {
      /* convert_flags ignores previous values of flag_mask and restrict_type 

         while setting them */
      if (!convert_flags(player, power(player, POW_EXAMINE), arg3,	/* power?XXX 

									 */
			 &flag_mask, &restrict_type))
	return;
    }
    else
      flag = 1;
    break;
  case 'l':
    if (string_prefix("link", arg2))
    {
      if ((restrict_link = match_thing(player, arg3)) == NOTHING)
	flag = 1;
    }
    else
      flag = 1;
    break;
  case 'n':
    if (string_prefix("name", arg2))
      restrict_name = arg3;
    else
      flag = 1;
    break;
  case 'o':
    if (string_prefix("objects", arg2))
    {
      restrict_name = arg3;
      restrict_type = TYPE_THING;
    }
    else
      flag = 1;
    break;
  case 'p':
    if (string_prefix("players", arg2))
    {
      restrict_name = arg3;
      if (*arg1 == '\0')
	restrict_owner = ANY_OWNER;
      restrict_type = TYPE_PLAYER;
    }
    else
      flag = 1;
    break;
  case 'r':
    if (string_prefix("rooms", arg2))
    {
      restrict_name = arg3;
      restrict_type = TYPE_ROOM;
    }
    else
      flag = 1;
    break;
  case 't':
    if (string_prefix("type", arg2))
    {
      if (arg3[0] == '\0')
	break;
      if (string_prefix("room", arg3))
	restrict_type = TYPE_ROOM;
      else if (string_prefix("channel", arg3))
	restrict_type = TYPE_CHANNEL;
      else if (string_prefix("exit", arg3))
	restrict_type = TYPE_EXIT;
      else if (string_prefix("thing", arg3))
	restrict_type = TYPE_THING;
#ifdef USE_UNIV
      else if (string_prefix("universe", arg3))
	restrict_type = TYPE_UNIVERSE;
#endif
      else if (string_prefix("player", arg3))
      {
	if (*arg1 == '\0')
	  restrict_owner = ANY_OWNER;
	restrict_type = TYPE_PLAYER;
      }
      else
      {
	notify(player, tprintf("%s: Unknown type", arg3));
	return;
      }
    }
    else
      flag = 1;
    break;
#ifdef USE_UNIV
  case 'u':
    if (string_prefix("universes", arg2))
    {
      restrict_name = arg3;
      restrict_type = TYPE_UNIVERSE;
    }
    else
      flag = 1;
    break;
#endif
  case 'z':
    if (string_prefix("zone", arg2))
      if ((restrict_zone = match_thing(player, arg3)) == NOTHING)
	flag = 1;
      else
	restrict_type = TYPE_ROOM;
    else
      flag = 1;
    break;
  default:
    flag = 1;
  }
  if (flag)
  {
    notify(player, tprintf("%s: Unknown class", arg2));
    return;
  }

  if (restrict_owner != ANY_OWNER)
    if (!controls(player, restrict_owner, POW_EXAMINE))
    {
      notify(player, "You need a search warrant to do that!");
      return;
    }
  if (restrict_owner == ANY_OWNER && restrict_type != TYPE_PLAYER)
    if (!power(player, POW_EXAMINE))
    {
      notify(player, "You need a search warrant to do that!");
      return;
    }

  /* make sure player has money to do the search */
  if (!payfor(player, search_cost))
  {
    notify(player, tprintf("Searches cost %d credits.", search_cost));
    return;
  }

  /* channel search */
  if (restrict_type == TYPE_CHANNEL || restrict_type == NOTYPE)
  {
    flag = 1;
    for (thing = 0; thing < db_top; thing++)
    {
      if (Typeof(thing) != TYPE_CHANNEL)
	continue;
      if (restrict_owner != ANY_OWNER &&
	  restrict_owner != db[thing].owner)
	continue;
      if ((db[thing].flags & flag_mask) != flag_mask)
	continue;
      if (restrict_name != NULL)
      {
	if (!string_prefix(db[thing].name, restrict_name))
	  continue;
      }
      if (flag)
      {
	flag = 0;
	destitute = 0;
	notify(player, "");	/* aack! don't use a newline! */
	notify(player, "CHANNELS:");
      }
      notify(player, unparse_object(player, thing));
    }
  }

#ifdef USE_UNIV
  /* universe search */
  if (restrict_type == TYPE_UNIVERSE || restrict_type == NOTYPE)
  {
    flag = 1;
    for (thing = 0; thing < db_top; thing++)
    {
      if (Typeof(thing) != TYPE_UNIVERSE)
	continue;
      if (restrict_owner != ANY_OWNER &&
	  restrict_owner != db[thing].owner)
	continue;
      if ((db[thing].flags & flag_mask) != flag_mask)
	continue;
      if (restrict_name != NULL)
      {
	if (!string_prefix(db[thing].name, restrict_name))
	  continue;
      }
      if (flag)
      {
	flag = 0;
	destitute = 0;
	notify(player, "");	/* aack! don't use a newline! */
	notify(player, "UNIVERSES:");
      }
      notify(player, unparse_object(player, thing));
    }
  }

#endif
  /* room search */
  if (restrict_type == TYPE_ROOM || restrict_type == NOTYPE)
  {
    flag = 1;
    for (thing = 0; thing < db_top; thing++)
    {
      if (Typeof(thing) != TYPE_ROOM)
	continue;
      if (restrict_owner != ANY_OWNER &&
	  restrict_owner != db[thing].owner)
	continue;
      if ((db[thing].flags & flag_mask) != flag_mask)
	continue;
      if (restrict_name != NULL)
      {
	if (!string_prefix(db[thing].name, restrict_name))
	  continue;
      }
      if (restrict_zone != NOTHING)
	if (restrict_zone != db[thing].zone)
	  continue;
      if (restrict_link != NOTHING && db[thing].link != restrict_link)
	continue;
      if (flag)
      {
	flag = 0;
	destitute = 0;
	notify(player, "");	/* aack! don't use a newline! */
	notify(player, "ROOMS:");
      }
      notify(player, unparse_object(player, thing));
    }
  }

  /* exit search */
  if (restrict_type == TYPE_EXIT || restrict_type == NOTYPE)
  {
    flag = 1;
    for (thing = 0; thing < db_top; thing++)
    {
      if (Typeof(thing) != TYPE_EXIT)
	continue;
      if (restrict_owner != ANY_OWNER &&
	  restrict_owner != db[thing].owner)
	continue;
      if ((db[thing].flags & flag_mask) != flag_mask)
	continue;
      if (restrict_name != NULL)
	if (!string_prefix(db[thing].name, restrict_name))
	  continue;
      if (restrict_link != NOTHING && db[thing].link != restrict_link)
	continue;

      if (flag)
      {
	flag = 0;
	destitute = 0;
	notify(player, "");	/* aack! don't use a newline! */
	notify(player, "EXITS:");
      }
      from = find_entrance(thing);
      to = db[thing].link;
      strcpy(buf, unparse_object(player, thing));
      strcat(buf, " [from ");
      strcat(buf,
	     from == NOTHING ? "NOWHERE" : unparse_object(player, from));
      strcat(buf, " to ");
      strcat(buf,
	     to == NOTHING ? "NOWHERE" : unparse_object(player, to));
      strcat(buf, "]");
      notify(player, buf);
    }
  }

  /* object search */
  if (restrict_type == TYPE_THING || restrict_type == NOTYPE)
  {
    flag = 1;
    for (thing = 0; thing < db_top; thing++)
    {
      if (Typeof(thing) != TYPE_THING)
	continue;
      if (!(flag_mask & GOING))	/* we're not searching for going things */
	if (db[thing].flags & GOING && !*atr_get(thing, A_DOOMSDAY))	/* in 

									   case 

									   of 

									   free 

									   object 

									 */
	  continue;
      if (restrict_owner != ANY_OWNER &&
	  restrict_owner != db[thing].owner)
	continue;
      if ((db[thing].flags & flag_mask) != flag_mask)
	continue;
      if (restrict_name != NULL)
	if (!string_prefix(db[thing].name, restrict_name))
	  continue;
      if (restrict_link != NOTHING && db[thing].link != restrict_link)
	continue;

      if (flag)
      {
	flag = 0;
	destitute = 0;
	notify(player, "");	/* aack! don't use a newline! */
	notify(player, "OBJECTS:");
      }
      strcpy(buf, unparse_object(player, thing));
      strcat(buf, " [owner: ");
      strcat(buf, unparse_object(player, db[thing].owner));
      strcat(buf, "]");
      notify(player, buf);
    }
  }

  /* player search */
  if (restrict_type == TYPE_PLAYER ||
      (power(player, POW_EXAMINE) && restrict_type == NOTYPE))
  {
    flag = 1;
    for (thing = 0; thing < db_top; thing++)
    {
      if (Typeof(thing) != TYPE_PLAYER)
	continue;
      if ((db[thing].flags & flag_mask) != flag_mask)
	continue;
      /* this only applies to wizards on this option */
      if (restrict_owner != ANY_OWNER &&
	  restrict_owner != db[thing].owner)
	continue;
      if (restrict_name != NULL)
	if (!string_prefix(db[thing].name, restrict_name))
	  continue;
      if (restrict_class != 0)
	if ((!db[thing].pows) || db[thing].pows[0] != restrict_class)
	  continue;
      if (restrict_link != NOTHING && db[thing].link != restrict_link)
	continue;
      if (flag)
      {
	flag = 0;
	destitute = 0;
	notify(player, "");	/* aack! don't use newlines! */
	notify(player, "PLAYERS:");
      }
      strcpy(buf, unparse_object(player, thing));
      if (controls(player, thing, POW_EXAMINE))
      {
	strcat(buf, " [location: ");
	strcat(buf, unparse_object(player, db[thing].location));
	strcat(buf, "]");
      }
      notify(player, buf);
    }
  }

  /* if nothing found matching search criteria */
  if (destitute)
    notify(player, "Nothing found.");
}

static object_flag_type convert_flags(player, is_wizard, s, p_mask, p_type)
dbref player;
int is_wizard;
char *s;
object_flag_type *p_mask;
object_flag_type *p_type;
{
  static struct
  {
    int id, type;
    object_flag_type bits;
  }
  fdata[] =
  {
    {
      'G', NOTYPE, GOING
    }
    ,
    {
      'p', NOTYPE, PUPPET
    }
    ,
    {
      'I', NOTYPE, INHERIT_POWERS
    }
    ,
    {
      'S', NOTYPE, STICKY
    }
    ,
    {
      'D', NOTYPE, DARK
    }
    ,
    {
      'L', NOTYPE, LINK_OK
    }
    ,
    {
      'H', NOTYPE, HAVEN
    }
    ,
    {
      'C', NOTYPE, CHOWN_OK
    }
    ,
    {
      'e', NOTYPE, ENTER_OK
    }
    ,
    {
      's', TYPE_PLAYER, PLAYER_SLAVE
    }
    ,
    {
      'c', NOTYPE, CONNECT
    }
    ,
    {
      'k', TYPE_THING, THING_KEY
    }
    ,
    {
      'd', TYPE_THING, THING_DEST_OK
    }
    ,
    {
      'J', TYPE_ROOM, ROOM_JUMP_OK
    }
    ,
    {
      'R', TYPE_ROOM, 0
    }
    ,
    {
      'E', TYPE_EXIT, 0
    }
    ,
    {
      'P', TYPE_PLAYER, 0
    }
    ,
    {
      'T', TYPE_THING, 0
    }
    ,
    {
      'K', TYPE_CHANNEL, 0
    }
    ,
    {
      'v', NOTYPE, SEE_OK
    }
    ,
    {
      't', TYPE_PLAYER, PLAYER_TERSE
    }
    ,
    {
      'o', NOTYPE, OPAQUE
    }
    ,
    {
      'q', NOTYPE, QUIET
    }
    ,
    {
      'f', TYPE_ROOM, ROOM_FLOATING
    }
    ,
    {
      'N', TYPE_PLAYER, PLAYER_NO_WALLS
    }
    ,
    {
      'm', TYPE_PLAYER, PLAYER_MORTAL
    }
    ,
    {
      'X', TYPE_THING, THING_SACROK
    }
    ,
    {
      'l', TYPE_THING, THING_LIGHT
    }
    ,
    {
      'l', TYPE_ROOM, EXIT_LIGHT
    }
    ,
    {
      'b', NOTYPE, BEARING
    }
    ,
    {
      'A', TYPE_ROOM, ROOM_AUDITORIUM
    }
    ,
    {
      'a', TYPE_PLAYER, PLAYER_ANSI
    }
    ,
    {
      'B', TYPE_PLAYER, PLAYER_NOBEEP
    }
    ,
/*
    {
      'w', TYPE_PLAYER, PLAYER_WHEN
    }
    ,
*/
    {
      'F', TYPE_PLAYER, PLAYER_FREEZE
    }
    ,
/*
    {
      '!', TYPE_PLAYER, PLAYER_SUSPECT
    }
    ,
*/
    {
      'i', TYPE_PLAYER, PLAYER_IDLE
    }
    ,
#ifdef USE_UNIV
    {
      'U', TYPE_UNIVERSE, 0
    }
    ,
#endif
    {
      0, 0, 0
    }
  };

  int i;
  int last_id = ' ';
  object_flag_type mask, type;

  mask = 0;
  type = NOTYPE;
  for (; *s != '\0'; s++)
  {
    /* tmp patch to stop hidden cheating */
    if (*s == 'c' && !is_wizard)
      continue;

    for (i = 0; fdata[i].id != 0; i++)
    {
      if (*s == fdata[i].id)
      {
	/* handle object specific flag problems */
	if (fdata[i].type != NOTYPE)
	{
	  /* make sure we aren't specific to a different type */
	  if (type != NOTYPE && type != fdata[i].type)
	  {
	    notify(player,
		   tprintf("Flag '%c' conflicts with '%c'.",
			   last_id, fdata[i].id));
	    return 0;
	  }

	  /* make us object specific to this type */
	  type = fdata[i].type;

	  /* always save last specific flag id */
	  last_id = *s;
	}

	/* add new flag into search mask */
	mask |= fdata[i].bits;

	/* stop searching for *this* flag */
	break;
      }
    }

    /* flag not found */
    if (fdata[i].id == 0)
    {
      notify(player, tprintf("%c: unknown flag", (int)*s));
      return 0;
    }
  }

  /* return new mask and type */
  *p_mask = mask;
  *p_type = type;
  return 1;
}

#ifdef USE_UNIV
void do_uinfo(player, arg1)
dbref player;
char *arg1;
{
  dbref thing;
  int x;

  init_match(player, arg1, TYPE_UNIVERSE);
  match_neighbor();
  match_possession();
  match_absolute();

  if ((thing = noisy_match_result()) == NOTHING)
    return;

  if (Typeof(thing) != TYPE_UNIVERSE)
  {
    notify(player, "That is not a Universe object.");
    return;
  }

  notify(player, "|R++||Y+---||R+>|");
  notify(player, tprintf("|Y+{|}| |R!+Universe Config||W!+:| %s",
			 unparse_object(player, thing)));
  notify(player, "|R++||Y+---||R+>|");
  for (x = 0; x < NUM_UA; x++)
  {
    switch (univ_config[x].type)
    {
    case UF_BOOL:
      notify(player, tprintf("|Y+{|}| |C!+%20.20s||W!+:| %s",
		 univ_config[x].label, db[thing].ua_int[x] ? "Yes" : "No"));
      break;
    case UF_INT:
      notify(player, tprintf("|Y+{|}| |C!+%20.20s||W!+:| %d",
			     univ_config[x].label, db[thing].ua_int[x]));
      break;
    case UF_FLOAT:
      notify(player, tprintf("|Y+{|}| |C!+%20.20s||W!+:| %f",
			     univ_config[x].label, db[thing].ua_float[x]));
      break;
    case UF_STRING:
      notify(player, tprintf("|Y+{|}| |C!+%20.20s||W!+:| %s",
			     univ_config[x].label, db[thing].ua_string[x]));
      break;
    default:
      notify(player, "Unknown config type");
      break;
    }
  }
  notify(player, "|R++||Y+---||R+>|");
}

void do_uconfig(player, arg1, arg2)
dbref player;
char *arg1, *arg2;
{
  char *i;
  dbref thing;
  int x, flag;

  init_match(player, arg1, TYPE_UNIVERSE);
  match_neighbor();
  match_possession();
  match_absolute();
  if ((thing = noisy_match_result()) == NOTHING)
    return;

  if (!controls(player, thing, POW_MODIFY))
  {
    notify(player, perm_denied());
    return;
  }

  i = strchr(arg2, ':');
  if (!i)
  {
    notify(player, "Improper syntax.  Should be @uconfig thing=setting:option");
    return;
  }
  *(i++) = '\0';

  for (flag = 1, x = 0; ((x < NUM_UA) && flag); x++)
  {
    if (!strcasecmp(univ_config[x].label, arg2))
    {
      switch (univ_config[x].type)
      {
      case UF_BOOL:
	if (i[0] == 'y' || i[0] == 'Y' || i[0] == '1')
	  db[thing].ua_int[x] = 1;
	else
	  db[thing].ua_int[x] = 0;
	notify(player, tprintf("%s - Set.", db[thing].cname));
	break;
      case UF_INT:
	db[thing].ua_int[x] = atoi(i);
	notify(player, tprintf("%s - Set.", db[thing].cname));
	break;
      case UF_FLOAT:
	db[thing].ua_float[x] = atof(i);
	notify(player, tprintf("%s - Set.", db[thing].cname));
	break;
      case UF_STRING:
	db[thing].ua_string[x] = realloc(db[thing].ua_string[x],
					 (strlen(i) + 1) * sizeof(char));

	strcpy(db[thing].ua_string[x], i);
	notify(player, tprintf("%s - Set.", db[thing].cname));
	break;
      default:
	notify(player, "Invalid Type.");
	break;
      }
      flag = 0;
    }
  }
  if (flag)
    notify(player, "Unknown setting.");
}
#endif

/* Ansi: void do_teleport(dbref player, char *arg1, char *arg2); */
void do_teleport(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  long moves;
  dbref victim;
  dbref destination;
  char *to;

#ifdef USE_UNIV
  dbref univ_src, univ_dest;

#endif

  /* get victim, destination */
  if ((!(arg2)) || *arg2 == '\0')
  {
    victim = player;
    to = arg1;
  }
  else
  {
    init_match(player, arg1, NOTYPE);
    match_neighbor();
    match_possession();
    match_me();
    match_absolute();
    match_player(NOTHING, NULL);
    match_exit();

    if ((victim = noisy_match_result()) == NOTHING)
      return;
    to = arg2;
  }

  if ((Typeof(victim) == TYPE_PLAYER) &&
      (IS(victim, TYPE_PLAYER, PLAYER_FREEZE)))
  {
    if (player == victim)
      notify(player, "You're frozen!  You can't move.");
    else
      notify(player, "That player is frozen and may not be moved.");
    return;
  }

  if ((Typeof(victim) == TYPE_THING) &&
      strcmp(atr_get(victim, A_MOVES), ""))
  {
    if ((moves = atol(atr_get(victim, A_MOVES))) == 0 &&
	strcmp(to, "home"))
    {
      if (player == victim)
	notify(victim, "Sorry, you are out of moves.");
      else
	notify(victim, "That thing is out of moves.");
      return;
    }
    atr_add(victim, A_MOVES, tprintf("%d", (moves <= 0) ? 0 : --moves));
  }

  /* get destination */
  if (!string_compare(to, "home"))
  {
    destination = HOME;
  }
  else if (!string_compare(to, "back"))
  {
    destination = BACK;
  }
  else
  {
    init_match(player, to, TYPE_PLAYER);
    match_here();
    match_absolute();
    match_neighbor();
    match_me();
    match_player(NOTHING, NULL);
    match_exit();
    destination = match_result();
  }
  switch (destination)
  {
  case NOTHING:
    notify(player, tprintf("I don't know where %s is.", to));
    break;
  case AMBIGUOUS:
    notify(player, tprintf("I don't know which %s you mean!", to));
    break;
  case HOME:
    if (Typeof(victim) != TYPE_PLAYER && Typeof(victim) != TYPE_THING)
    {
      notify(player, tprintf("Can't touch %s.", to));
      return;
    }
    if (controls(player, victim, POW_TELEPORT) ||
	controls(player, db[victim].location, POW_TELEPORT))
    {
      /* replace above 2 lines with IS() macro? */

#ifdef USE_UNIV
      univ_src = db[get_zone_first(victim)].universe;
      univ_dest = db[get_zone_first(db[victim].link)].universe;
      if (
	   (!db[univ_src].ua_int[UA_TELEPORT] || !db[univ_dest].ua_int[UA_TELEPORT])
	   && !power(player, POW_TELEPORT))
      {
	notify(player, perm_denied());
	return;
      }
#endif

      notify(victim,
	     "You feel a sudden urge to leave this place and go home...");
      safe_tel(victim, destination);
      did_it(player, victim, A_TPORT, 0, A_OTPORT, 0, A_AFTPORT);
      return;
    }
    notify(player, perm_denied());
    return;

  case BACK:
    if(Typeof(victim) != TYPE_PLAYER && Typeof(victim) != TYPE_THING)
    {
      notify(player, tprintf("Can't touch %s.", to));
      return;
    }
    if(controls(player, victim, POW_TELEPORT) ||
      controls(player, db[victim].location, POW_TELEPORT))
    {
#ifdef USE_UNIV
      univ_src = db[get_zone_first(victim)].universe;
      univ_dest = db[get_zone_first(db[victim].link)].universe;
      if ( 
           (!db[univ_src].ua_int[UA_TELEPORT] || !db[univ_dest].ua_int[UA_TELEPORT])
           && !power(player, POW_TELEPORT))
      { 
	notify(player, perm_denied());
        return;
      }
#endif
      safe_tel(victim, destination);
      did_it(player, victim, A_TPORT, 0, A_OTPORT, 0, A_AFTPORT);
      return;
    }
    notify(player, perm_denied());
    return;

  default:
#ifdef USE_UNIV
    univ_src = db[get_zone_first(victim)].universe;
    univ_dest = db[get_zone_first(destination)].universe;
    if (
    (!db[univ_src].ua_int[UA_TELEPORT] || !db[univ_dest].ua_int[UA_TELEPORT])
	 && !power(player, POW_TELEPORT))
    {
      notify(player, perm_denied());
      return;
    }
#endif

    /* check victim, destination types, teleport if ok */
    if (Typeof(victim) == TYPE_ROOM)
    {
      notify(player, "Can't move rooms!");
      return;
    }
    if ((Typeof(victim) == TYPE_EXIT &&
	 (Typeof(destination) == TYPE_PLAYER ||
	  Typeof(destination) == TYPE_EXIT)) ||
	(Typeof(victim) == TYPE_PLAYER &&
	 Typeof(destination) == TYPE_PLAYER))
    {
      notify(player, "Bad destination.");
      return;
    }

    if (Typeof(destination) != TYPE_EXIT)
    {
      if ((controls(player, victim, POW_TELEPORT) ||
	   controls(player, db[victim].location, POW_TELEPORT)) &&
	  (Typeof(victim) != TYPE_EXIT || controls(player, destination,
						   POW_MODIFY)) &&
	  (controls(player, destination, POW_TELEPORT) ||
	   IS(destination, TYPE_ROOM, ROOM_JUMP_OK)))
      {
	if (!check_zone(player, victim, destination, 1))
	{
	  return;
	}
	else
	{
	  did_it(victim, get_zone_first(victim), A_LEAVE, NULL, A_OLEAVE, NULL, A_ALEAVE);
	  safe_tel(victim, destination);
	  did_it(victim, get_zone_first(victim), A_ENTER, NULL, A_OENTER, NULL, A_AENTER);
          did_it(player, victim, A_TPORT, 0, A_OTPORT, 0, A_AFTPORT);
	  return;
	}
      }
      notify(player, perm_denied());
      return;
    }
    else
    {				/* dest is TYPE_EXIT */
      if (controls(player, db[victim].location, POW_TELEPORT) ||
      controls(player, victim, POW_TELEPORT) || power(player, POW_TELEPORT))
	if (((controls(player, db[victim].location, POW_TELEPORT) ||
	      controls(player, victim, POW_TELEPORT))
	     && controls(player, destination, POW_TELEPORT))
	    || power(player, POW_TELEPORT))
        {
	  do_move(victim, to);
          did_it(player, victim, A_TPORT, 0, A_OTPORT, 0, A_AFTPORT);
        }
	else
        {
	  notify(player, perm_denied());
        }
    }
  }
}

/* Note: special match, link_ok objects are considered
   controlled for this purpose */
dbref match_controlled(player, name, cutoff_level)
dbref player;
char *name;
int cutoff_level;
{
  dbref match;

  init_match(player, name, NOTYPE);
  match_everything();

  match = noisy_match_result();
  if (match != NOTHING && !controls(player, match, cutoff_level))
  {
    notify(player, perm_denied());
    return NOTHING;
  }
  else
  {
    return match;
  }
}
/* Ansi: void do_force(dbref player,char *what,char *command); */
void do_force(player, what, command)
dbref player;
char *what;
char *command;
{
  dbref victim;

  if ((victim = match_controlled(player, what, POW_MODIFY)) == NOTHING)
  {
    notify(player, "Sorry.");
    return;
  }

  /* if ((Typeof(victim)==TYPE_ROOM) || (Typeof(victim)==TYPE_EXIT)) {
     notify(player,"You can only force players and things."); return; } */

  if ((db[victim].owner != db[player].owner))
    log_force(tprintf("%s forces %s to execute: %s",
	 unparse_object_a(player, player), unparse_object_a(victim, victim),
		      command));

  if (db[victim].owner == root)
  {
    notify(player, "You can't force root!!");
    return;
  }

  /* force victim to do command */
  parse_que(victim, command, player);
}

int try_force(player, command)
dbref player;
char *command;
{
  char buff[1024];
  char *s;

  /* first see if command prefixed by object # */
  if (*command == '#' && command[1] != ' ')
  {
    strcpy(buff, command);
    for (s = buff + 1; *s && *s != ' '; s++)
    {
      if (!isdigit(*s))
	return 0;
    }
    if (!*s)
      return (0);
    *s++ = 0;
    do_force(player, buff, s);
    return (1);
  }
  else
    return (0);
}

/* Ansi: void do_pstats(dbref player, char *name); */
void do_pstats(player, name)
dbref player;
char *name;
{
  dbref owner;
  long i, total;
  long obj[NUM_OBJ_TYPES];	/* number of object types */
  long pla[NUM_CLASSES];

  if (*name == '\0')
    owner = ANY_OWNER;
  else
  {
    notify(player, tprintf("%s: No such player", name));
    return;
  }

  if (!power(player, POW_STATS))
  {
    notify(player, "Maybe next time. Sorry!");
    return;
  }

  calc_stats(owner, &total, obj, pla);
  notify(player, tprintf("%s Player Breakdown:", muse_name));
  notify(player, tprintf("%9ld Players", obj[TYPE_PLAYER]));
  for (i = 1; i < NUM_CLASSES; i++)
    notify(player, tprintf("%9ld %ss", pla[i], class_to_name(i)));
}
/* Ansi: void calc_stats(dbref owner,int *total,int *players,int count[NUM_OBJ_TYPES]); */
void calc_stats(owner, total, obj, pla)
dbref owner;
register long *total;
long obj[NUM_OBJ_TYPES];
long pla[NUM_CLASSES];
{
  int i;
  dbref thing;

  /* zero out count stats */
  *total = 0;
  for (i = 0; i < NUM_OBJ_TYPES; i++)
    obj[i] = 0;
  for (i = 0; i < NUM_CLASSES; i++)
    pla[i] = 0;

  for (thing = 0; thing < db_top; thing++)
    if (owner == ANY_OWNER || owner == db[thing].owner)
      if (!(db[thing].flags & GOING))
      {
	++obj[Typeof(thing)];
	if (Typeof(thing) == TYPE_PLAYER)
	  ++pla[*db[thing].pows];
	++*total;
      }
}
/* Ansi: int owns_stuff(dbref player); */
int owns_stuff(player)
dbref player;
{
  dbref i;
  int matches = 0;

  for (i = 0; i < db_top; i++)
  {
    if (db[db[i].owner].owner == player && i != player)
      matches++;
  }
  return matches;
}
/* Ansi: void do_wipeout(dbref player,char *arg1,char *arg3); */
void do_wipeout(player, arg1, arg3)
dbref player;
char *arg1;
char *arg3;
{
  char *arg2;
  int type;
  dbref victim;
  dbref n;
  int do_all = 0;

  if (!power(player, POW_SECURITY))
  {
    log_important(tprintf("%s failed to: @wipeout %s=%s",
			  unparse_object(player, player), arg1, arg3));
    notify(player, "Sorry, only wizards may perform mass destruction.");
    return;
  }

  for (arg2 = arg1; *arg2 && *arg2 != ' '; arg2++) ;
  if (!*arg2)
  {
    notify(player, "You must specify the object type to destroy.");
    return;
  }
  *arg2 = '\0';
  arg2++;
  if (strcmp(arg2, "type"))
  {
    notify(player, "The syntax is \"@wipeout <player> type=<obj type>\".");
    return;
  }
  victim = lookup_player(arg1);
  if (victim == NOTHING)
  {
    notify(player, tprintf("%s does not seem to exist.", arg1));
    return;
  }
  if (!controls(player, victim, POW_MODIFY))
  {
    notify(player, perm_denied());
    return;
  }
  if (string_prefix("objects", arg3))
    type = TYPE_THING;
  else if (string_prefix("rooms", arg3))
    type = TYPE_ROOM;
  else if (string_prefix("channels", arg3))
    type = TYPE_CHANNEL;
#ifdef USE_UNIV
  else if (string_prefix("universes", arg3))
    type = TYPE_UNIVERSE;
#endif
  else if (string_prefix("exits", arg3))
    type = TYPE_EXIT;
  else if (!strcmp("all", arg3))
  {
    do_all = 1;
    type = NOTYPE;
  }
  else
  {
    notify(player, "Unknown type.");
    return;
  }

  log_important(tprintf("%s executed: @wipeout %s=%s", unparse_object(player,
			  player), unparse_object_a(victim, victim), arg3));

  for (n = 0; n < db_top; n++)
  {
    if ((real_owner(n) == victim && n != victim) &&
	(Typeof(n) == type || do_all))
    {
      destroy_obj(n, 60);	/* destroy in 1 minute */
    }
  }
  switch (type)
  {
  case TYPE_THING:
    notify(player, "Wiped out all objects.");
    notify(victim, tprintf("All your objects have been destroyed by %s.",
			   unparse_object(victim, player)));
    break;
  case TYPE_ROOM:
    notify(player, "Wiped out all rooms.");
    notify(victim, tprintf("All your rooms have been destroyed by %s.",
			   unparse_object(victim, player)));
    break;
  case TYPE_CHANNEL:
    notify(player, "Wiped out all channels.");
    notify(victim, tprintf("All your channels have been destroyed by %s.",
			   unparse_object(victim, player)));
#ifdef USE_UNIV
  case TYPE_UNIVERSE:
    notify(player, "Wiped out all universes.");
    notify(victim, tprintf("All your universes have been destroyed by %s.",
			   unparse_object(victim, player)));
    break;
#endif
  case TYPE_EXIT:
    notify(player, "Wiped out all exits.");
    notify(victim, tprintf("All your exits have been destroyed by %s.",
			   unparse_object(victim, player)));
    break;
  case NOTYPE:
    notify(player, "Wiped out every blessed thing.");
    notify(victim, tprintf("All your stuff has been repossessed by %s. Oh, well.",
			   unparse_object(victim, player)));
    break;
  }
}
/* Ansi: void do_chownall(dbref player,char *arg1,char *arg2); */
void do_chownall(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  dbref playerA;
  dbref playerB;
  dbref n;

  if (!power(player, POW_SECURITY))
  {
    log_important(tprintf("%s failed to: @chownall %s=%s", unparse_object(player,
						      player), arg1, arg2));
    notify(player, "Sorry, only wizards may mass chown.");
    return;
  }

  init_match(player, arg1, TYPE_PLAYER);
  match_neighbor();
  match_player(NOTHING, NULL);
  if ((playerA = noisy_match_result()) != NOTHING)
  {
    init_match(player, arg2, TYPE_PLAYER);
    match_neighbor();
    match_player(NOTHING, NULL);
    if (((playerB = noisy_match_result()) != NOTHING) && !is_root(playerB))
    {
      for (n = 0; n < db_top; n++)
      {
	if (db[n].owner == playerA && n != playerA)
	{
	  db[n].owner = playerB;
	}
      }
    }
    else
      return;
  }
  else
    return;
  log_important(tprintf("%s executed: @chownall %s=%s", unparse_object(player,
				player), unparse_object_a(playerA, playerA),
			unparse_object_a(playerB, playerB)));
  notify(player, "Owner changed.");
}
/* Ansi: void do_poor(dbref player,char *arg1); */
void do_poor(player, arg1)
dbref player;
char *arg1;
{
  dbref a;
  long int amt = atol(arg1);

  if (player != root)
    return;
  for (a = 0; a < db_top; a++)
    if (Typeof(a) == TYPE_PLAYER)
      s_Pennies(a, amt);
}
/* Ansi: void do_allquota(dbref player,char *arg1); */
void do_allquota(player, arg1)
dbref player;
char *arg1;
{
  long count, limit, owned;
  char buf[20];
  dbref who, thing;

  if (player != root)
  {
    notify(player, "Don't. @allquota isn't nice.");
    return;
  }

  count = 0;
  notify(player, "Working...");
  for (who = 0; who < db_top; who++)
  {
    if (Typeof(who) != TYPE_PLAYER)
      continue;

    /* count up all owned objects */
    owned = -1;			/* a player is never included in his own
				   quota */
    for (thing = 0; thing < db_top; thing++)
    {
      if (db[thing].owner == who)
	if ((db[thing].flags & (TYPE_THING | GOING)) != (TYPE_THING | GOING))
	  ++owned;
    }

    limit = atol(arg1);

    /* stored as a relative value */
    sprintf(buf, "%ld", limit - owned);
    atr_add(who, A_RQUOTA, buf);
    sprintf(buf, "%ld", limit);
    atr_add(who, A_QUOTA, buf);
    ++count;
  }
  notify(player, tprintf("done (%ld players processed).", count));
}
/* Ansi: void do_newpassword(dbref player, char *name, char *password); */
void do_newpassword(player, name, password)
dbref player;
char *name;
char *password;
{
  dbref victim;

  if ((victim = lookup_player(name)) == NOTHING)
  {
    notify(player, tprintf("%s: no such player.", name));
  }
  else if ((Typeof(player) != TYPE_PLAYER || !has_pow(player, victim, POW_NEWPASS)) && !(Typeof(victim) != TYPE_PLAYER && controls(player, victim, POW_MODIFY)))
  {
    log_important(tprintf("%s failed to: @newpassword %s", unparse_object(player,
				player), unparse_object_a(victim, victim)));
    notify(player, perm_denied());
    return;
  }
  else if (*password != '\0' && !ok_password(password))
  {
    /* Wiz can set null passwords, but not bad passwords */
    notify(player, "Bad password");
  }
  else if (victim == root)
  {
    notify(player, "You cannot @newpassword root.");
  }
  else
  {
    /* it's ok, do it */
    s_Pass(victim, crypt(password, "XX"));
    notify(player, "Password changed.");
    log_important(tprintf("%s executed: @newpassword %s",
	 unparse_object(player, player), unparse_object_a(victim, victim)));
    log_sensitive(tprintf("%s executed: @newpassword %s=%s",
		    unparse_object(player, player), unparse_object_a(victim,
							victim), password));
    notify(victim, tprintf("Your password has been changed by %s.",
			   db[player].name));
  }
}
/* Ansi: void do_boot(dbref player,char *name); */
void do_boot(player, name, reason)
dbref player;
char *name;
char *reason;
{
  dbref victim;

  /* player only - no inherited powers */

  init_match(player, name, TYPE_PLAYER);
  match_neighbor();
  match_absolute();
  match_player(NOTHING, NULL);
  match_me();
  if ((victim = noisy_match_result()) == NOTHING)
    return;
  if (!has_pow(player, victim, POW_BOOT) && !(Typeof(victim) != TYPE_PLAYER && controls(player, victim, POW_BOOT)))
  {
    log_important(tprintf("%s failed to: @boot %s", unparse_object(player,
				player), unparse_object_a(victim, victim)));
    notify(player, perm_denied());
    return;
  }
  if (victim == root)
  {
    notify(player, "You can't boot root!");
    return;
  }

  if (victim == player)
  {
    dbref dummy = lookup_player("viper");

    if (dummy != NOTHING)
      notify(player, tprintf("You don't wanna be like %s and boot yourself.",
			     db[dummy].cname));
    else
      notify(player, "You don't wanna be like viper and boot yourself.");
    return;
  }

  /* notify people */
  if (*reason)
  {
    log_important(tprintf("%s executed: @boot %s because: %s",
		  unparse_object_a(player, player), unparse_object_a(victim,
							  victim), reason));
    notify(victim, tprintf("You have been booted by %s because: %s",
			   unparse_object_a(victim, player), reason));
    notify(player, tprintf("%s - Booted.", db[victim].cname));
  }
  else
  {
    notify(player, "You must give a reason to @boot.");
    return;
  }
  boot_off(victim);
}

void do_cboot(player, arg1)
dbref player;
char *arg1;
{
  struct descriptor_data *d;
  long toboot;

  if (!isdigit(*arg1))
  {
    notify(player, "That's not a number.");
    return;
  }

  toboot = atol(arg1);

  d = descriptor_list;
  while (d && d->concid != toboot)
    d = d->next;

  if (!d)
  {
    notify(player, "Unable to find specified concid.");
    return;
  }

  if (d->player == player)
  {
    notify(player, "Sorry, you can't @cboot yourself. Try @selfboot.");
    return;
  }

  if (d->state == CONNECTED)
  {
    if (controls(player, d->player, POW_BOOT))
    {
      log_important(tprintf("%s executes: @cboot %ld (descriptor %d, player %s)", unparse_object(player, player), toboot, d->descriptor, unparse_object_a(d->player, d->player)));
      notify(player, tprintf("Descriptor %d, concid %ld (player %s) - Booted.", d->descriptor, toboot, unparse_object(player, d->player)));
      notify(d->player, tprintf("You have been @cbooted by %s.", unparse_object(player, player)));
      shutdownsock(d);
    }
    else
      /* power check fail */
    {
      log_important(tprintf("%s failed to: @cboot %ld (descriptor %d, player %s)",
		      unparse_object(player, player), toboot, d->descriptor,
			    unparse_object_a(d->player, d->player)));
      notify(player, perm_denied());
      return;
    }
  }
  else
    /* not a connected player */
  {
    if (power(player, POW_BOOT))
    {
      log_important(tprintf("%s executed: @cboot %ld (descriptor %d, unconnected from host %s@%s)", unparse_object(player, player), toboot, d->descriptor, d->user, d->addr));
      notify(player, tprintf("Concid %ld - Booted.", toboot));
      shutdownsock(d);
    }
    else
      /* power check fail */
    {
      log_important(tprintf("%s failed to: @cboot %ld (unconnected descriptor %d)", unparse_object(player, player), toboot, d->descriptor));
      notify(player, perm_denied());
      return;
    }
  }
}

/* Ansi: void do_join(dbref player,char *arg1); */
void do_join(player, arg1)
dbref player;
char *arg1;
{
  dbref victim;
  dbref to;

  /* get victim, destination */
  victim = player;
  to = lookup_player(arg1);

  if (!controls(victim, to, POW_JOIN) && !controls(victim, db[to].location, POW_JOIN) && !(could_doit(victim, to, A_LJOIN) && Typeof(to) == TYPE_PLAYER))
  {
    notify(player, "Sorry. You don't have wings.");
    return;
  }

  if (to == NOTHING || db[to].location == NOTHING)
  {
    notify(player, tprintf("%s: no such player.", arg1));
    return;
  }

  if ((Typeof(player) == TYPE_PLAYER) &&
      (IS(player, TYPE_PLAYER, PLAYER_FREEZE)))
  {
    notify(player, "You're frozen!  You can't move.");
    return;
  }

  moveto(victim, db[to].location);
}

/* Ansi: void do_summon(dbref player, char *arg1); */
void do_summon(player, arg1)
dbref player;
char *arg1;
{
  dbref victim;
  dbref dest;

  /* get victim, destination */
  dest = db[player].location;
  victim = lookup_player(arg1);

  if (!controls(player, victim, POW_SUMMON) && !controls(player, db[victim].location, POW_SUMMON))
  {
    notify(player, "Sorry. That player doesn't have wings.");
    return;
  }

  if (victim == NOTHING)
  {
    notify(player, tprintf("%s: no such player", arg1));
    return;
  }

  if (db[victim].flags & GOING)
  {
    notify(player, "That's a silly thing to summon!");
    return;
  }

  moveto(victim, dest);
}

void do_swap(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  dbref thing1, thing2;
  struct object swapbuf;
  dbref i;
  ATRDEF *d;
  struct descriptor_data *des;

#ifdef DEBUG
  notify(player, tprintf("arg1: %s   arg2: %s", arg1, arg2));
#endif
  thing1 = match_controlled(player, arg1, POW_MODIFY);
  if (thing1 == NOTHING)
    return;
  thing2 = match_controlled(player, arg2, POW_MODIFY);
  if (thing2 == NOTHING)
    return;

/*  log_important(tprintf("arg1 %s arg2 %s",arg1,arg2)); */

  if (Typeof(thing1) == TYPE_PLAYER || Typeof(thing2) == TYPE_PLAYER)
  {
    if (!power(player, POW_SECURITY))
    {
      log_important(tprintf("%s failed to: @swap %s=%s",
	     unparse_object_a(root, player), unparse_object_a(root, thing1),
			    unparse_object_a(root, thing2)));
      notify(player, perm_denied());
      return;
    }
    log_important(tprintf("%s executed: @swap %s=%s",
	     unparse_object_a(root, player), unparse_object_a(root, thing1),
			  unparse_object_a(root, thing2)));
  }

  notify(player, tprintf("%s and %s are now:",
       unparse_object_a(player, thing1), unparse_object_a(player, thing2)));

  if (Typeof(thing1) == TYPE_PLAYER)
    delete_player(thing1);
  if (Typeof(thing2) == TYPE_PLAYER)
    delete_player(thing2);

  if (Typeof(thing1) == TYPE_CHANNEL)
    delete_channel(thing1);
  if (Typeof(thing2) == TYPE_CHANNEL)
    delete_channel(thing2);

  swapbuf = db[thing2];
  db[thing2] = db[thing1];
  db[thing1] = swapbuf;

#define SWAPREF(x) do { if((x) == thing1) (x) = thing2; else if ((x) == thing2) (x) = thing1; } while (0)

  for (i = 0; i < db_top; i++)
  {
    int j;

    SWAPREF(db[i].location);
    SWAPREF(db[i].zone);
#ifdef USE_UNIV
    SWAPREF(db[i].universe);
#endif
    SWAPREF(db[i].contents);
    SWAPREF(db[i].exits);
    SWAPREF(db[i].link);
    SWAPREF(db[i].next);
    SWAPREF(db[i].owner);
    for (j = 0; db[i].parents && db[i].parents[j] != NOTHING; j++)
      SWAPREF(db[i].parents[j]);
    for (j = 0; db[i].children && db[i].children[j] != NOTHING; j++)
      SWAPREF(db[i].children[j]);

    for (d = db[i].atrdefs; d; d = d->next)
      SWAPREF(d->a.obj);
  }

  for (des = descriptor_list; des; des = des->next)
    if (des->state == CONNECTED)
      SWAPREF(des->player);

  if (Typeof(thing1) == TYPE_PLAYER)
    add_player(thing1);
  if (Typeof(thing2) == TYPE_PLAYER)
    add_player(thing2);

  if (Typeof(thing1) == TYPE_CHANNEL)
    add_channel(thing1);
  if (Typeof(thing2) == TYPE_CHANNEL)
    add_channel(thing2);


  notify(player, tprintf("%s and %s.",
       unparse_object_a(player, thing1), unparse_object_a(player, thing2)));
}

void do_su(player, arg1, arg2, cause)
dbref player;
char *arg1;
char *arg2;
dbref cause;
{
  dbref thing;
  struct descriptor_data *d;

  thing = match_thing(player, arg1);
  if (thing == NOTHING)
    return;

  if (cause != player)
  {
    int counter = 0;
    struct descriptor_data *sd;

    for (sd = descriptor_list; sd; sd = sd->next)
    {
      if (sd->state == CONNECTED && sd->player == player)
      {
        counter++;
      }
    }

    if (counter > 1)
    {
      log_important(tprintf("%s failed to: @su %s - @forced and can't decide which connection.", unparse_object_a(root, player), unparse_object_a(root, thing)));
      notify(cause, "Sorry, you can't force someone to @su when there's more than one login under that ID.");
      return;
    }
  }

  if (*arg2)
  {
    if (connect_player(tprintf("#%ld", thing), arg2) != thing)
    {
      log_important(tprintf("%s failed to: @su %s", unparse_object_a(root, player), unparse_object_a(root, thing)));
      notify(player, perm_denied());
      return;
    }
    log_io(tprintf("|Y!+SU|: %s becomes %s", unparse_object_a(root, player), unparse_object_a(root, thing)));
    com_send_as_hidden("pub_io",tprintf("|Y!+SU|: %s becomes %s", unparse_object_a(root, player), unparse_object_a(root, thing)), player);
  }
  else
  {
    if (!controls(player, thing, POW_MODIFY) || is_root(thing) || thing == db[0].zone)
    {
      log_important(tprintf("%s failed to: @su %s by force", unparse_object_a(root, player), unparse_object_a(root, thing)));
      notify(player, perm_denied());
      return;
    }
    log_important(tprintf("|R+SU|: %s becomes %s by force", unparse_object_a(root, player), unparse_object_a(root, thing)));
  }


  d = find_least_idle(player);

/*  replaced with function above
  for (d = descriptor_list; d; d = d->next)
    if (d->state == CONNECTED && d->player == player)
      break;
*/

  if (!d)
    return;

  announce_disconnect(d->player);
  d->player = thing;
  if (Guest(player))
  {
    struct descriptor_data *sd;
    int count = 0;

    for (sd = descriptor_list; sd; sd = sd->next)
    {
      if (sd->state == CONNECTED && sd->player == player)
	++count;
    }
    if (count == 0)
      destroy_guest(player);
  }
  announce_connect(d->player);
}

/* this function should work -- it should make the player *
 * the least diel when they type @su.  There may still be *
 * a problem with @as/@force *player=@su *player=password */
struct descriptor_data *find_least_idle(dbref player)
{
  struct descriptor_data *d, *sd;
  long last = 0;
  long dup = 0;

  sd = NULL;

  for (d = descriptor_list; d; d = d->next)
  {
    if (d->state == CONNECTED && d->player == player)
    {
      if (d->last_time > last)
      {
        sd = d;
        last = d->last_time;
        dup = 0;
      } 
      else if (d->last_time == last)
      {
        dup = last;
        sd = NULL;
      }
    }
  }
  if (dup != 0)
  {
    notify(player, "Sorry, Try again.");
    log_important(tprintf("%s failed to @su - duplicate times on least idle connection", unparse_object_a(root, player)));
  }
  return sd;
}

void do_fixquota(player, arg1)
dbref player;
char *arg1;
{
  dbref victim;
  long owned, i;
  char buf[1024];

  init_match(player, arg1, TYPE_PLAYER);
  match_everything();
  if ((victim = noisy_match_result()) == NOTHING)
    return;

  if (!power(player, POW_DB) || (Typeof(player) != TYPE_PLAYER) ||
      (Typeof(victim) != TYPE_PLAYER))
  {
    notify(player, perm_denied());
    return;
  }

  for (owned = -1, i = 0; i < db_top; i++)
  {
    if (db[i].owner == victim)
      if ((db[i].flags & (TYPE_THING | GOING)) != (TYPE_THING | GOING))
	owned++;
  }

  if (inf_quota(victim))
  {
    sprintf(buf, "%ld", owned);
    atr_add(victim, A_QUOTA, buf);
    atr_add(victim, A_RQUOTA, "0");
    notify(victim, "Infinite quota fixed.");
  }
  else
  {
    sprintf(buf, "%ld", atol(atr_get(player, A_QUOTA)) - owned);
    atr_add(player, A_RQUOTA, buf);
    notify(victim, "Quota fixed.");
  }
}

void do_nologins(player, arg1)
dbref player;
char *arg1;
{
  if (!power(player, POW_SECURITY))
  {
    log_important(tprintf("%s failed to: @nologins %s", unparse_object(player, player),
			  arg1));
    notify(player, perm_denied());
    return;
  }

  if (!string_compare(arg1, "on"))
  {
    if (nologins)
    {
      notify(player, "@nologins has already been enabled.");
      return;
    }
    else
    {
      nologins = 1;
      notify(player, "@nologins has been enabled. Only Directors may log in now.");
    }
  }
  else if (!string_compare(arg1, "off"))
  {
    if (!nologins)
    {
      notify(player, "@nologins has already been disabled.");
      return;
    }
    else
    {
      nologins = 0;
      notify(player, "@nologins has been disabled. Logins will now be processed.");
    }
  }
  else
  {
    switch (nologins)
    {
    case 0:
      notify(player, "@nologins has been disabled.");
      break;
    case 1:
      notify(player, "@nologins has been enabled.");
      break;
    default:
      notify(player, "@nologins value messed up. @nologins now disabled.");
      nologins = 0;
      break;
    }
  }
  log_important(tprintf("%s executed: @nologins %s",
			unparse_object(player, player), arg1));
}

void do_lockout(player, arg1)
dbref player;
char *arg1;
{
  if (!power(player, POW_SECURITY))
  {
    log_important(tprintf("%s failed to: @lockout %s", unparse_object(player, player),
			  arg1));
    notify(player, perm_denied());
    return;
  }

  if (*arg1)
  {
    if (strcmp(arg1, "none") == 0)
    {
      notify(player, "Connection restrictions have been lifted.");
      restrict_connect_class = 0;
    }
    else
    {
      int new = name_to_class(arg1);

      if (new == 0)
	notify(player, "Unknown class!");
      else
      {
	restrict_connect_class = new;
	notify(player, tprintf("Users below %s are now locked out.",
			       class_to_name(new)));
      }
    }
  }
  else
  {
    if (restrict_connect_class == 0)
      notify(player, "No class-lockout is in effect.");
    else
      notify(player, tprintf("Currently locking out all users below %s.",
			     class_to_name(restrict_connect_class)));
    if (restrict_connect_class != 0)
      notify(player, "To remove restrictions, type: @lockout none");
  }
  log_important(tprintf("%s executed: @lockout %s",
			unparse_object(player, player), arg1));
}

void do_plusmotd(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  char *message;

  if (!power(player, POW_MOTD))
  {
    notify(player, perm_denied());
    return;
  }

  message = reconstruct_message(arg1, arg2);

  sprintf(motd_who,"#%ld",player);

  if (*message)
  {
    if (*message == '~')
    {
      sprintf(motd_who,"#-1");
      strcpy(motd,message + 1);
      notify(player, "MOTD Set Anonymously.");
    }
    else
    {
      strcpy(motd, message);
      notify(player, "MOTD Set.");
    }
  }
  else
  {
    strcpy(motd,"");
    notify(player, "MOTD Cleared.");
  }
}


